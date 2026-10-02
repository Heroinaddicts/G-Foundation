package main

import (
	"GFoundation/Api"
	"GFoundation/Foundation"
)

func GetGFoundationApi() Api.GFoundationApi {
	return Foundation.NewGFoundation()
}
