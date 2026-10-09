package main

import (
	"GFoundation/Api"
	"fmt"
)

type DbTest struct {
	api Api.GFoundationApi
}

func (m *DbTest) Initialize(api Api.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *DbTest) Launch(api Api.GFoundationApi) bool {
	return true
}

func (m *DbTest) LaunchFinished(api Api.GFoundationApi) {
	api.GetDbProxyApi().CreateDatabaseSession(
		Api.DatabaseTypeMysql,
		"172.16.0.20",
		3306,
		"root",
		"peanut0Mao",
		"GoProject",
		func(err error, session Api.IDatabaseSession) {
			if nil == err {
				session.Query(
					Api.UnorderQuery,
					func(err error, rows Api.IDbRows) {
						if nil == err {
							for rows.Next() {
								var guid uint64
								var username string
								var password string
								e := rows.Scan(&guid, &username, &password)
								if nil == e {
									fmt.Printf("Guid %d Username %s Password %s\n", guid, username, password)
								} else {
									fmt.Printf("Scan Error %d\n", err)
								}
							}
						} else {
							fmt.Printf("Session Query Error %d\n", err)
						}
					},
					"Select * from Account where Guid = ?",
					0,
				)
			} else {
				fmt.Printf("Create Database Session error %d\n", err)
			}
		},
	)
}

func (m *DbTest) Release(api Api.GFoundationApi) {

}

func (m *DbTest) Update(api Api.GFoundationApi) {

}

func GetModule() Api.IModule {
	return &DbTest{}
}
