package Database

import (
	"GFoundationApi"
	"GFoundationUtils"
	"database/sql"
)

const (
	dbevent_connect uint8 = 0
	dbevent_query   uint8 = 1
)

type dbevent struct {
	ev                    uint8
	err                   error
	session               *GDbSession
	rows                  *dbRows
	queryCallback         GFoundationApi.OnQueryCallback
	createSessionCallback GFoundationApi.OnCreateDatabaseSessionCallback
}

type GDbProxy struct {
	queue *GFoundationUtils.SPSCQueue[*dbevent]
}

func NewGDbProxy() *GDbProxy {
	return &GDbProxy{
		queue: GFoundationUtils.NewSPSCQueue[*dbevent](16384),
	}
}

type dbRows struct {
	rows *sql.Rows
}

func (r *dbRows) Scan(dest ...any) error {
	return r.rows.Scan(dest...)
}

func (r *dbRows) Next() bool {
	return r.rows.Next()
}

func (g *GDbProxy) Update() {
	for {
		p, ret := g.queue.Pop()
		if ret {
			switch p.ev {
			case dbevent_connect:
				p.createSessionCallback(p.err, p.session)
			case dbevent_query:
				p.queryCallback(p.err, p.rows)
				if nil != p.rows && nil != p.rows.rows {
					p.rows.rows.Close()
				}
			}
		} else {
			return
		}
	}
}

func (g *GDbProxy) CreateDatabaseSession(
	dbtype uint8,
	host string,
	port uint16,
	username string,
	password string,
	dbname string,
	createCallback GFoundationApi.OnCreateDatabaseSessionCallback,
) {
	go func() {
		session, err := NewGDbSession(g, dbtype, host, port, username, password, dbname)
		g.queue.Push(&dbevent{
			ev:                    dbevent_connect,
			err:                   err,
			session:               session,
			rows:                  nil,
			queryCallback:         nil,
			createSessionCallback: createCallback,
		})
	}()
}
