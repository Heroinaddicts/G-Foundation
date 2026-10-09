package Database

import (
	"GFoundation/Api"
	"database/sql"
	"fmt"

	_ "github.com/go-sql-driver/mysql"
)

type GDbSession struct {
	dbtype   uint8
	host     string
	port     uint16
	username string
	dbname   string

	handle *sql.DB

	gdbproxy *GDbProxy
}

func (s *GDbSession) Query(mask int64, callback Api.OnQueryCallback, query string, args ...any) {
	if Api.UnorderQuery == mask {
		go func() {
			rows, err := s.handle.Query(query, args...)

			var rowProxy *dbRows = nil
			if rows != nil {
				rowProxy = &dbRows{rows: rows}
			}

			s.gdbproxy.queue.Push(&dbevent{
				ev:                    dbevent_query,
				err:                   err,
				session:               s,
				rows:                  rowProxy,
				queryCallback:         callback,
				createSessionCallback: nil,
			})
		}()
	}
}

func (s *GDbSession) Release() {
	s.handle.Close()
}

func NewGDbSession(proxy *GDbProxy, dbtype uint8, host string, port uint16, username string, password string, dbname string) (*GDbSession, error) {
	dsn := fmt.Sprintf(
		"%s:%s@tcp(%s:%d)/%s?parseTime=true",
		username,
		password,
		host,
		port,
		dbname,
	)

	str := "unknown"
	if dbtype == Api.DatabaseTypeMysql {
		str = "mysql"
	} else {
		fmt.Printf("Unkown database %d\n", dbtype)
	}

	db, err := sql.Open(str, dsn)
	if err != nil {
		fmt.Printf("sql.Open error %d\n", err)
		return nil, err
	}

	return &GDbSession{
		dbtype:   dbtype,
		host:     host,
		port:     port,
		username: username,
		dbname:   dbname,
		handle:   db,
		gdbproxy: proxy,
	}, nil
}
