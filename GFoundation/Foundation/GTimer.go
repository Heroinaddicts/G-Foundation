// GTimer.go
package Foundation

import (
	"GFoundation/Api"
	"container/list"
	"reflect"
	"time"
)

const (
	timerJiffMillis int64 = 1

	timerGear0Size = 256
	timerGearNSize = 64
	timerGearCount = 5
)

type timerKey struct {
	target   uintptr
	callback uintptr
}

type timerHandle struct {
	// target 保留强引用，避免计时期间被 GC 回收。
	target   any
	context  any
	callback Api.TimerCallback

	interval     int64 // jiff
	count        int
	currentCount int
	fired        int

	expire  int64 // 绝对 jiff
	pauseAt int64

	started bool
	valid   bool
	paused  bool
	polling bool

	list *timerList
	elem *list.Element
}

// timerList 使用 timer 自身保存 list 和 element，便于从任意队列摘除。
type timerList struct {
	items list.List
}

func (l *timerList) pushBack(t *timerHandle) {
	t.list = l
	t.elem = l.items.PushBack(t)
}

func (l *timerList) remove(t *timerHandle) {
	if t.list != l || t.elem == nil {
		return
	}
	l.items.Remove(t.elem)
	t.list = nil
	t.elem = nil
}

func (l *timerList) popFront() *timerHandle {
	elem := l.items.Front()
	if elem == nil {
		return nil
	}

	t := elem.Value.(*timerHandle)
	l.items.Remove(elem)
	t.list = nil
	t.elem = nil
	return t
}

type timerGear struct {
	slots []timerList
}

func newTimerGear(size int) timerGear {
	return timerGear{slots: make([]timerList, size)}
}

// GTimer 是单线程时间轮。StartTimer、StopTimer 和 Update 应由同一主线程调用。
type GTimer struct {
	timers map[timerKey]*timerHandle

	// 对应 C++ 的 5 级时间轮：256/64/64/64/64。
	gears [timerGearCount]timerGear

	running   timerList
	suspended timerList

	lastTick time.Time
	jiff     int64
}

func NewGTimer() *GTimer {
	now := time.Now()
	g := &GTimer{
		timers:   make(map[timerKey]*timerHandle),
		lastTick: now,
	}
	g.gears[0] = newTimerGear(timerGear0Size)
	for i := 1; i < timerGearCount; i++ {
		g.gears[i] = newTimerGear(timerGearNSize)
	}
	return g
}

func timerTargetPointer(target any) (uintptr, bool) {
	if target == nil {
		return 0, false
	}

	value := reflect.ValueOf(target)
	if value.Kind() != reflect.Ptr || value.IsNil() {
		return 0, false
	}
	return value.Pointer(), true
}

func makeTimerKey(fun Api.TimerCallback, target any) (timerKey, bool) {
	targetPtr, ok := timerTargetPointer(target)
	if !ok || fun == nil {
		return timerKey{}, false
	}
	return timerKey{
		target:   targetPtr,
		callback: reflect.ValueOf(fun).Pointer(),
	}, true
}

// StartTimer 创建或替换同一 target + callback 对应的 timer。
// delay 和 interval 单位为毫秒。
func (g *GTimer) StartTimer(
	fun Api.TimerCallback,
	target any,
	context any,
	delay int,
	count int,
	interval int,
) {
	key, ok := makeTimerKey(fun, target)
	if !ok {
		return
	}

	if old := g.timers[key]; old != nil {
		g.killTimer(old, true)
	}

	if delay < 0 {
		delay = 0
	}
	if interval < int(timerJiffMillis) {
		interval = int(timerJiffMillis)
	}

	delayJiff := int64(delay) / timerJiffMillis
	if delayJiff < 1 {
		// 零延迟 timer 在下一次 Update tick 触发。
		delayJiff = 1
	}

	timer := &timerHandle{
		target:       target,
		context:      context,
		callback:     fun,
		interval:     ceilTimerDiv(int64(interval), timerJiffMillis),
		count:        count,
		currentCount: 0,
		expire:       g.jiff + delayJiff,
		valid:        true,
	}
	g.timers[key] = timer
	g.schedule(timer)
}

// StopTimer 强制结束指定 timer。可在该 timer 的 callback 中调用。
func (g *GTimer) StopTimer(fun Api.TimerCallback, target any) {
	key, ok := makeTimerKey(fun, target)
	if !ok {
		return
	}
	if timer := g.timers[key]; timer != nil {
		g.killTimer(timer, true)
	}
}

func (g *GTimer) killTimer(timer *timerHandle, notify bool) {
	if timer == nil || !timer.valid {
		return
	}

	timer.valid = false
	g.detach(timer)
	delete(g.timers, g.keyOf(timer))

	if notify && !timer.polling {
		g.call(timer, Api.TimerStateEnd, false)
	}
}

// PauseTimer 暂停 timer。暂停期间不消耗剩余时间。
func (g *GTimer) PauseTimer(fun Api.TimerCallback, target any) {
	key, ok := makeTimerKey(fun, target)
	if !ok {
		return
	}

	timer := g.timers[key]
	if timer == nil || !timer.valid || timer.paused {
		return
	}

	timer.paused = true
	timer.pauseAt = g.jiff
	g.call(timer, Api.TimerStatePause, false)

	if !timer.polling {
		g.detach(timer)
		g.suspended.pushBack(timer)
	}
}

// ResumeTimer 恢复已暂停的 timer。
func (g *GTimer) ResumeTimer(fun Api.TimerCallback, target any) {
	key, ok := makeTimerKey(fun, target)
	if !ok {
		return
	}

	timer := g.timers[key]
	if timer == nil || !timer.valid || !timer.paused {
		return
	}

	timer.expire += g.jiff - timer.pauseAt
	timer.paused = false
	g.call(timer, Api.TimerStateResume, false)

	if !timer.polling && timer.valid {
		g.detach(timer)
		g.schedule(timer)
	}
}

// IsExistsTimer 查询指定 timer 是否存在。
func (g *GTimer) IsExistsTimer(fun Api.TimerCallback, target any) bool {
	key, ok := makeTimerKey(fun, target)
	if !ok {
		return false
	}
	return g.timers[key] != nil
}

// Update 根据经过的时间推进时间轮，并触发到期 timer。
func (g *GTimer) Update() {
	now := time.Now()
	elapsedMillis := now.Sub(g.lastTick).Milliseconds()
	ticks := elapsedMillis / timerJiffMillis

	for i := int64(0); i < ticks; i++ {
		g.jiff++
		g.updateGears()
	}

	g.lastTick = g.lastTick.Add(time.Duration(ticks*timerJiffMillis) * time.Millisecond)

	for timer := g.running.popFront(); timer != nil; timer = g.running.popFront() {
		g.fire(timer)
	}
}

func (g *GTimer) fire(timer *timerHandle) {
	if !timer.valid {
		return
	}

	timer.polling = true

	if !timer.started {
		g.call(timer, Api.TimerStateStart, false)
		timer.started = true
	}

	if timer.valid {
		g.call(timer, Api.TimerStateBeat, false)
		if timer.valid {
			timer.fired++
		}
	}

	timer.polling = false

	if !timer.valid {
		// StopTimer 在 Start/Beat 回调中调用时，延后到回调返回后通知 End。
		g.call(timer, Api.TimerStateEnd, false)
		return
	}

	if timer.count != Api.Unlimited && timer.count == timer.currentCount+1 {
		timer.valid = false
		delete(g.timers, g.keyOf(timer))
		g.call(timer, Api.TimerStateEnd, true)
		return
	}

	timer.currentCount++

	if timer.paused {
		g.suspended.pushBack(timer)
		return
	}

	timer.expire += timer.interval
	g.adjustExpire(timer)
	g.schedule(timer)
}

func (g *GTimer) call(timer *timerHandle, state uint8, murder bool) {
	if timer.callback != nil {
		timer.callback(state, timer.currentCount, timer.target, timer.context, murder)
	}
}

func (g *GTimer) adjustExpire(timer *timerHandle) {
	if timer.interval <= 0 || timer.expire > g.jiff {
		return
	}

	// Move to the first interval boundary strictly after the current jiff.
	// Integer division without the +1 leaves expire in the past (or exactly
	// at the current jiff); schedule() then inserts it into a slot that has
	// already been visited, delaying it until the gear wraps around.
	missed := (g.jiff-timer.expire)/timer.interval + 1
	timer.expire += missed * timer.interval
}

func (g *GTimer) keyOf(timer *timerHandle) timerKey {
	targetPtr, _ := timerTargetPointer(timer.target)
	return timerKey{
		target:   targetPtr,
		callback: reflect.ValueOf(timer.callback).Pointer(),
	}
}

func (g *GTimer) detach(timer *timerHandle) {
	if timer.list != nil {
		timer.list.remove(timer)
	}
}

func (g *GTimer) schedule(timer *timerHandle) {
	if !timer.valid || timer.paused {
		return
	}
	g.detach(timer)

	delta := timer.expire - g.jiff
	if delta < 0 {
		delta = 0
	}

	span := int64(1)
	for level := 0; level < timerGearCount; level++ {
		size := int64(len(g.gears[level].slots))
		levelRange := span * size

		if level == timerGearCount-1 || delta < levelRange {
			slot := int((timer.expire / span) % size)
			g.gears[level].slots[slot].pushBack(timer)
			return
		}
		span = levelRange
	}
}

func (g *GTimer) updateGears() {
	g.drainSlot(0, g.jiff%timerGear0Size)

	if g.jiff%timerGear0Size == 0 {
		g.cascade(1)
	}
	if g.jiff%(timerGear0Size*timerGearNSize) == 0 {
		g.cascade(2)
	}
	if g.jiff%(timerGear0Size*timerGearNSize*timerGearNSize) == 0 {
		g.cascade(3)
	}
	if g.jiff%(timerGear0Size*timerGearNSize*timerGearNSize*timerGearNSize) == 0 {
		g.cascade(4)
	}
}

func (g *GTimer) cascade(level int) {
	if level >= timerGearCount {
		return
	}

	span := int64(timerGear0Size)
	for i := 1; i < level; i++ {
		span *= timerGearNSize
	}

	size := int64(len(g.gears[level].slots))
	slot := (g.jiff / span) % size

	if slot == 0 && level+1 < timerGearCount {
		g.cascade(level + 1)
	}
	g.drainSlot(level, slot)
}

func (g *GTimer) drainSlot(level int, slot int64) {
	timerList := &g.gears[level].slots[int(slot)]

	for timer := timerList.popFront(); timer != nil; timer = timerList.popFront() {
		if !timer.valid || timer.paused {
			continue
		}

		if timer.expire <= g.jiff {
			g.running.pushBack(timer)
			continue
		}

		// 最高层回绕或 slot 提前命中时，检查绝对到期时间并重新调度。
		g.schedule(timer)
	}
}

// Clear 取消所有 timer 并清空队列，不触发 callback。
func (g *GTimer) Clear() {
	for _, timer := range g.timers {
		timer.valid = false
		g.detach(timer)
	}
	clear(g.timers)

	for g.running.popFront() != nil {
	}
	for g.suspended.popFront() != nil {
	}
}

func ceilTimerDiv(n, d int64) int64 {
	return (n + d - 1) / d
}
