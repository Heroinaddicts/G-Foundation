package Api

type IDbRows interface {
	Scan(dest ...any) error
	Next() bool
}

const (
	UnorderQuery int64 = -1
)

type OnQueryCallback func(err error, result IDbRows)
type IDatabaseSession interface {
	Query(mast int64, callback OnQueryCallback, query string, args ...any)
	Release()
}

const (
	DatabaseTypeMysql uint8 = 0
)

type OnCreateDatabaseSessionCallback func(err error, session IDatabaseSession)
type IDbProxyApi interface {
	CreateDatabaseSession(
		dbtype uint8,
		host string,
		port uint16,
		username string,
		password string,
		dbname string,
		createCallback OnCreateDatabaseSessionCallback,
	)
}
