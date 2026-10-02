package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Foundation/Net"
	"sync"
)

type GFoundation struct {
	neter *Net.Neter
	logic *GLogic
	timer *GTimer
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
			timer: NewGTimer(),
		}
	})

	return gfinstance
}

func (f *GFoundation) GetNetApi() Api.INetApi {
	return f.neter
}

func (f *GFoundation) GetTimerApi() Api.ITimerApi {
	return f.timer
}

func (f *GFoundation) Launch() {
	f.logic.Launch()
}

func (f *GFoundation) Update() {
	f.neter.Update()
	f.timer.Update()
	f.logic.Update()
}
