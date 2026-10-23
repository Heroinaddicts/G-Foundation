package GFoundationApi

type IDbRows interface {
	Scan(dest ...any) error
	Next() bool
}

type OnQueryCallback func(err error, result IDbRows)
type IDatabaseSession interface {
	Query(callback OnQueryCallback, query string, args ...any)
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
