package Database

import (
	"GFoundationApi"
	"GFoundationUtils"
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

func (g *GDbProxy) Update() {
	for {
		p, ret := g.queue.Pop()
		if ret {
			switch p.ev {
			case dbevent_connect:
				if p.createSessionCallback != nil {
					if p.session == nil {
						p.createSessionCallback(p.err, nil)
					} else {
						p.createSessionCallback(p.err, p.session)
					}
				} else if p.session != nil {
					p.session.Release()
				}
			case dbevent_query:
				if p.queryCallback != nil {
					if p.rows == nil {
						p.queryCallback(p.err, nil)
					} else {
						p.queryCallback(p.err, p.rows)
					}
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
