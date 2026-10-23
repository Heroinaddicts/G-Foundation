module GFoundationTests/Regression

go 1.27.1

require (
	GFoundation v0.0.0
	GFoundationApi v0.0.0
)

require (
	GFoundationUtils v0.0.0 // indirect
	filippo.io/edwards25519 v1.2.0 // indirect
	github.com/go-sql-driver/mysql v1.10.1 // indirect
	github.com/gomodule/redigo v1.9.3 // indirect
)

replace GFoundation => ../../GFoundation

replace GFoundationApi => ../../GFoundationApi

replace GFoundationUtils => ../../GFoundationUtils
