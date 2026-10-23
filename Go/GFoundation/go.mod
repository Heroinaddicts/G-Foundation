module GFoundation

go 1.27.1

replace GFoundationApi => ../GFoundationApi

require (
	GFoundationApi v0.0.0
	GFoundationUtils v0.0.0
	github.com/go-sql-driver/mysql v1.10.1
	github.com/gomodule/redigo v1.9.3
)

replace GFoundationUtils => ../GFoundationUtils

require filippo.io/edwards25519 v1.2.0 // indirect
