package Database

import (
	"GFoundationApi"
	"bytes"
	"context"
	"database/sql"
	"database/sql/driver"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"sync"
	"time"

	"github.com/go-sql-driver/mysql"
)

var ErrDatabaseSessionClosed = errors.New("database session is closed")

const databaseTimeout = 10 * time.Second

type dbRequest struct {
	query    string
	args     []any
	callback GFoundationApi.OnQueryCallback
}

// API methods belong to the main thread. A single worker owns the physical
// driver connection, including fetching rows; callbacks never perform I/O.
// No sql.DB, connection pool, automatic retry or reconnection is involved.
type GDbSession struct {
	handle      driver.Conn
	gdbproxy    *GDbProxy
	requestchan chan *dbRequest
	isclosed    bool
	loopOnce    sync.Once
	done        chan struct{}
}

func (s *GDbSession) Query(callback GFoundationApi.OnQueryCallback, query string, args ...any) {
	snapshot := append([]any(nil), args...)
	for i, arg := range snapshot {
		switch v := arg.(type) {
		case []byte:
			snapshot[i] = bytes.Clone(v)
		case sql.NamedArg:
			if b, ok := v.Value.([]byte); ok {
				v.Value = bytes.Clone(b)
			}
			snapshot[i] = v
		}
	}
	req := &dbRequest{query: query, args: snapshot, callback: callback}
	if s.isclosed {
		s.gdbproxy.queue.Push(&dbevent{ev: dbevent_query, err: ErrDatabaseSessionClosed, queryCallback: callback})
		return
	}
	s.requestchan <- req
}

// Release is idempotent and nonblocking. Already accepted requests finish on
// the same connection before the worker closes it. An idle session stays open.
func (s *GDbSession) Release() {
	if s.isclosed {
		return
	}
	s.isclosed = true
	close(s.requestchan)
}

func (s *GDbSession) OptLoop() {
	s.loopOnce.Do(func() {
		defer func() { s.handle.Close(); close(s.done) }()
		for req := range s.requestchan {
			s.dealRequest(req)
		}
	})
}

func (s *GDbSession) dealRequest(req *dbRequest) {
	ctx, cancel := context.WithTimeout(context.Background(), databaseTimeout)
	defer cancel()
	rows, err := s.query(ctx, req.query, req.args)
	s.gdbproxy.queue.Push(&dbevent{ev: dbevent_query, err: err, rows: rows, queryCallback: req.callback})
}

func (s *GDbSession) query(ctx context.Context, query string, args []any) (*dbRows, error) {
	values := make([]driver.NamedValue, len(args))
	checker, _ := s.handle.(driver.NamedValueChecker)
	for i, arg := range args {
		nv := driver.NamedValue{Ordinal: i + 1, Value: arg}
		if named, ok := arg.(sql.NamedArg); ok {
			nv.Name, nv.Value = named.Name, named.Value
		}
		var err error
		if checker != nil {
			err = checker.CheckNamedValue(&nv)
		} else {
			err = driver.ErrSkip
		}
		if errors.Is(err, driver.ErrSkip) {
			nv.Value, err = driver.DefaultParameterConverter.ConvertValue(nv.Value)
		}
		if err != nil {
			return nil, fmt.Errorf("database argument %d: %w", i+1, err)
		}
		values[i] = nv
	}
	rows, err := s.handle.(driver.QueryerContext).QueryContext(ctx, query, values)
	// Large parameter packets can make MySQL decline interpolation. Fall back
	// to a prepared statement on THIS connection, never to another connection.
	var stmt driver.Stmt
	if errors.Is(err, driver.ErrSkip) {
		stmt, err = s.handle.(driver.ConnPrepareContext).PrepareContext(ctx, query)
		if err != nil {
			return nil, err
		}
		defer stmt.Close()
		rows, err = stmt.(driver.StmtQueryContext).QueryContext(ctx, values)
	}
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	result := &dbRows{index: -1}
	columns := len(rows.Columns())
	for {
		row := make([]driver.Value, columns)
		err = rows.Next(row)
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		for i, value := range row {
			if b, ok := value.([]byte); ok {
				row[i] = bytes.Clone(b)
			}
		}
		result.values = append(result.values, row)
	}
	if err = rows.Close(); err != nil {
		return nil, err
	}
	return result, nil
}

func NewGDbSession(proxy *GDbProxy, dbtype uint8, host string, port uint16, username, password, dbname string) (*GDbSession, error) {
	if dbtype != GFoundationApi.DatabaseTypeMysql {
		return nil, fmt.Errorf("unsupported database type %d", dbtype)
	}
	cfg := mysql.NewConfig()
	cfg.Net, cfg.Addr = "tcp", net.JoinHostPort(host, strconv.Itoa(int(port)))
	cfg.User, cfg.Passwd, cfg.DBName = username, password, dbname
	cfg.ParseTime, cfg.InterpolateParams = true, true
	cfg.Timeout, cfg.ReadTimeout, cfg.WriteTimeout = databaseTimeout, databaseTimeout, databaseTimeout
	connector, err := mysql.NewConnector(cfg)
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), databaseTimeout)
	defer cancel()
	conn, err := connector.Connect(ctx)
	if err != nil {
		return nil, err
	}
	if err = conn.(driver.Pinger).Ping(ctx); err != nil {
		conn.Close()
		return nil, err
	}
	s := &GDbSession{handle: conn, gdbproxy: proxy, requestchan: make(chan *dbRequest, 4096), done: make(chan struct{})}
	go s.OptLoop()
	return s, nil
}
