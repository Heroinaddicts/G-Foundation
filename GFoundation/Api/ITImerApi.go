package Api

const (
	TimerStateStart  uint8 = 0
	TimerStateBeat   uint8 = 1
	TimerStateEnd    uint8 = 2
	TimerStatePause  uint8 = 3
	TimerStateResume uint8 = 4

	Unlimited int = -1
)

type TimerCallback func(state uint8, count int, data any, context any, notmurder bool)

type ITimerApi interface {
	StartTimer(fun TimerCallback, target any, context any, delay int, count int, interval int)
	StopTimer(fun TimerCallback, target any)
	PauseTimer(fun TimerCallback, target any)
	ResumeTimer(fun TimerCallback, target any)
}
