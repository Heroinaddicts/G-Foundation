package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Foundation/Net"
	"sync"
)

type GFoundation struct {
	neter *Net.Neter
	logic *GLogic
}

var (
	gfinstance *GFoundation
	gfonce     sync.Once
)

func GFoundationInstance() *GFoundation {
	gfonce.Do(func() {
		gfinstance = &GFoundation{
			neter: Net.NewNeter(),
			logic: GLogicInstance(),
		}
	})

	return gfinstance
}

func (f *GFoundation) GetNetApi() Api.INetApi {
	return f.neter
}

func (f *GFoundation) Launch() {
	f.logic.Launch()
}

func (f *GFoundation) Update() {
	f.neter.Update()
	f.logic.Update()
}
