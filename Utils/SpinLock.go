package Utils

import (
	"sync/atomic"
)

type SpinLock struct {
	lock int32
}

func (s *SpinLock) Lock() {
	for !atomic.CompareAndSwapInt32(&s.lock, 0, 1) {
	}
}

func (s *SpinLock) Unlock() {
	atomic.StoreInt32(&s.lock, 0)
}

func NewSpinLock() *SpinLock {
	return &SpinLock{}
}
