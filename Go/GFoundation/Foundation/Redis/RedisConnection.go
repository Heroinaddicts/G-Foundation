package Redis

import (
	"context"
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/gomodule/redigo/redis"
)

// redisConnection owns exactly one TCP connection for its entire lifetime.
// Only the dial goroutine, then the session worker, may execute commands on it.
// There is no pool, background maintenance, automatic retry, or redial.
type redisConnection struct {
	socket net.Conn
	handle redis.Conn
}

func dialRedisConnection(ctx context.Context, address, password string, dbindex int) (*redisConnection, error) {
	if dbindex < 0 {
		return nil, errors.New("redis database index must not be negative")
	}
	dialer := net.Dialer{KeepAlive: 30 * time.Second}
	socket, err := dialer.DialContext(ctx, "tcp", address)
	if err != nil {
		return nil, err
	}
	// Set one absolute deadline around both writing and reading each command.
	// Redigo's context helper creates a goroutine per command; NewConn with
	// external socket deadlines keeps execution entirely on the session worker.
	c := &redisConnection{socket: socket, handle: redis.NewConn(socket, 0, 0)}
	deadline, _ := ctx.Deadline()
	if password != "" {
		_, err = c.do(deadline, "AUTH", password)
	}
	if err == nil && dbindex != 0 {
		_, err = c.do(deadline, "SELECT", dbindex)
	}
	if err == nil {
		_, err = c.do(deadline, "PING")
	}
	if err != nil {
		c.Close()
		return nil, err
	}
	return c, nil
}

func (c *redisConnection) do(deadline time.Time, command string, args ...any) (any, error) {
	if err := c.handle.Err(); err != nil {
		return nil, redisConnectionError(err)
	}
	if err := c.socket.SetDeadline(deadline); err != nil {
		c.handle.Close()
		return nil, redisConnectionError(err)
	}
	if err := c.handle.Send(command, args...); err != nil {
		return nil, redisConnectionError(err)
	}
	if err := c.handle.Flush(); err != nil {
		return nil, redisConnectionError(err)
	}
	// ReceiveWithTimeout replaces the read deadline, so calculate the remaining
	// budget AFTER writing instead of starting a second full execution timeout.
	var remaining time.Duration
	if !deadline.IsZero() {
		remaining = time.Until(deadline)
		if remaining <= 0 {
			c.handle.Close()
			return nil, context.DeadlineExceeded
		}
	}
	result, err := redis.ReceiveWithTimeout(c.handle, remaining)
	if err == nil && result == nil {
		// Keep the existing API's missing/null reply error for GET and EVAL.
		err = ErrRedisNil
	}
	return result, redisConnectionError(err)
}

func redisConnectionError(err error) error {
	var networkError net.Error
	if errors.As(err, &networkError) && networkError.Timeout() {
		return fmt.Errorf("%w: %v", context.DeadlineExceeded, err)
	}
	return err
}

func (c *redisConnection) Close() error {
	// Fatal I/O errors already close the socket and are reported per command.
	// Do not report that previous operation again as a failed explicit Close.
	if c.handle.Err() != nil {
		c.handle.Close()
		return nil
	}
	return c.handle.Close()
}

// RESP2 bulk strings arrive as []byte. Preserve the existing JSON representation
// for aggregate Eval results instead of encoding nested strings as base64.
func redisEvalAggregate(value any) any {
	switch value := value.(type) {
	case []byte:
		return string(value)
	case []any:
		for i := range value {
			value[i] = redisEvalAggregate(value[i])
		}
	}
	return value
}
