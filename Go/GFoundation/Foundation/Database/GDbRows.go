package Database

import (
	"bytes"
	"database/sql"
	"database/sql/driver"
	"fmt"
	"reflect"
	"strconv"
	"time"
)

// Results are detached from the connection on the worker. Next and Scan are
// memory-only operations; another queued query cannot overwrite these values.
type dbRows struct {
	values [][]driver.Value
	index  int
}

func (r *dbRows) Next() bool {
	if r.index+1 >= len(r.values) {
		r.index = len(r.values)
		return false
	}
	r.index++
	return true
}
func (r *dbRows) Scan(dest ...any) error {
	if r.index < 0 || r.index >= len(r.values) {
		return fmt.Errorf("database Scan requires a successful Next")
	}
	row := r.values[r.index]
	if len(dest) != len(row) {
		return fmt.Errorf("database Scan: got %d destinations for %d columns", len(dest), len(row))
	}
	for i, value := range row {
		if err := scanValue(dest[i], value); err != nil {
			return fmt.Errorf("database Scan column %d: %w", i, err)
		}
	}
	return nil
}

func scanValue(dest, value any) error {
	dst := reflect.ValueOf(dest)
	if !dst.IsValid() || dst.Kind() != reflect.Pointer || dst.IsNil() {
		return fmt.Errorf("destination must be a non-nil pointer")
	}
	// A Scanner owns any bytes it retains, just as with database/sql.Rows.Scan.
	if scanner, ok := dest.(sql.Scanner); ok {
		return scanner.Scan(value)
	}
	dst = dst.Elem()
	if value == nil {
		switch dst.Kind() {
		case reflect.Pointer, reflect.Interface, reflect.Slice:
			dst.SetZero()
			return nil
		}
		return fmt.Errorf("cannot convert NULL to %s", dst.Type())
	}
	if dst.Kind() == reflect.Pointer {
		if dst.IsNil() {
			dst.Set(reflect.New(dst.Type().Elem()))
		}
		return scanValue(dst.Interface(), value)
	}
	if b, ok := value.([]byte); ok {
		value = bytes.Clone(b)
	}
	src := reflect.ValueOf(value)
	if src.Type().AssignableTo(dst.Type()) {
		dst.Set(src)
		return nil
	}
	text := ""
	switch v := value.(type) {
	case []byte:
		text = string(v)
	case string:
		text = v
	case time.Time:
		text = v.Format(time.RFC3339Nano)
	case int64:
		text = strconv.FormatInt(v, 10)
	case uint64:
		text = strconv.FormatUint(v, 10)
	case float64:
		text = strconv.FormatFloat(v, 'g', -1, 64)
	case bool:
		text = strconv.FormatBool(v)
	default:
		return fmt.Errorf("unsupported source %T", value)
	}
	switch dst.Kind() {
	case reflect.String:
		dst.SetString(text)
		return nil
	case reflect.Slice:
		if dst.Type().Elem().Kind() == reflect.Uint8 {
			dst.SetBytes([]byte(text))
			return nil
		}
	case reflect.Bool:
		b, err := driver.Bool.ConvertValue(value)
		if err != nil {
			return err
		}
		dst.SetBool(b.(bool))
		return nil
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		n, err := strconv.ParseInt(text, 10, dst.Type().Bits())
		if err != nil {
			return err
		}
		dst.SetInt(n)
		return nil
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		n, err := strconv.ParseUint(text, 10, dst.Type().Bits())
		if err != nil {
			return err
		}
		dst.SetUint(n)
		return nil
	case reflect.Float32, reflect.Float64:
		n, err := strconv.ParseFloat(text, dst.Type().Bits())
		if err != nil {
			return err
		}
		dst.SetFloat(n)
		return nil
	}
	return fmt.Errorf("cannot scan %T into %s", value, dst.Type())
}
