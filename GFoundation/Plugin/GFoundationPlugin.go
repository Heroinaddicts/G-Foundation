package main

import (
	"GFoundation/Api"
	"GFoundation/Foundation"
)

func CreateGFoundationApi() Api.GFoundationApi {
	return Foundation.NewGFoundation()
}
