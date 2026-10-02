package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Foundation/Net"
)

type GFoundation struct {
	neter *Net.Neter
}

func NewGFoundation() *GFoundation {
	return &GFoundation{
		neter: Net.NewNeter(),
	}
}

func (f *GFoundation) GetNetApi() Api.INetApi {
	return f.neter
}

func (f *GFoundation) Update() {
	f.neter.Update()
}
