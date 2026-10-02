package Utils

import (
	"fmt"
	"runtime"
	"sync/atomic"
)

type ReadCallback func(data []byte, offset int, length int) int

type SPSCBuffer struct {
	bufPtr atomic.Pointer[[]byte]

	writePos atomic.Int64
	readPos  atomic.Int64

	growthSize int

	resizeLock SpinLock

	readers atomic.Int32

	reallocing atomic.Bool
}

func NewSPSCBuffer(initialSize, growthSize int) *SPSCBuffer {
	if initialSize <= 0 {
		panic("initialSize must be greater than 0")
	}

	if growthSize <= 0 {
		panic("growthSize must be greater than 0")
	}

	buf := make([]byte, initialSize)

	r := &SPSCBuffer{
		growthSize: growthSize,
	}

	r.bufPtr.Store(&buf)

	return r
}

func (r *SPSCBuffer) Write(data []byte, offset int, length int) {
	if length <= 0 {
		return
	}

	if offset < 0 || offset+length > len(data) {
		panic(fmt.Sprintf(
			"invalid Write range: offset=%d length=%d dataLen=%d",
			offset,
			length,
			len(data),
		))
	}

	buf := r.bufPtr.Load()
	currentBuf := *buf

	write := r.writePos.Load()
	read := r.readPos.Load()

	capacity := int64(len(currentBuf))

	if capacity-write >= int64(length) {
		copy(
			currentBuf[int(write):int(write)+length],
			data[offset:offset+length],
		)

		r.writePos.Store(write + int64(length))
		return
	}

	r.resizeLock.Lock()
	defer r.resizeLock.Unlock()
	r.reallocing.Store(true)
	for r.readers.Load() != 0 {
		runtime.Gosched()
	}

	buf = r.bufPtr.Load()
	currentBuf = *buf

	write = r.writePos.Load()
	read = r.readPos.Load()

	unread := write - read
	capacity = int64(len(currentBuf))

	if unread+int64(length) <= capacity {
		if unread > 0 {
			copy(
				currentBuf[:int(unread)],
				currentBuf[int(read):int(write)],
			)
		}

		r.readPos.Store(0)
		write = unread

		copy(
			currentBuf[int(write):int(write)+length],
			data[offset:offset+length],
		)

		r.writePos.Store(write + int64(length))
		r.reallocing.Store(false)
		return
	}

	required := unread + int64(length)

	newCapacity := capacity

	for newCapacity < required {
		newCapacity += int64(r.growthSize)
	}

	newBuf := make([]byte, int(newCapacity))

	if unread > 0 {
		copy(
			newBuf[:int(unread)],
			currentBuf[int(read):int(write)],
		)
	}

	copy(
		newBuf[int(unread):int(required)],
		data[offset:offset+length],
	)

	r.bufPtr.Store(&newBuf)

	r.readPos.Store(0)
	r.writePos.Store(required)

	r.reallocing.Store(false)
}

// Read 只能由 Consumer 调用。
func (r *SPSCBuffer) Read(callback ReadCallback) bool {
	if callback == nil {
		panic("callback must not be nil")
	}

	for {
		if r.reallocing.Load() {
			runtime.Gosched()
			continue
		}

		r.readers.Add(1)

		if r.reallocing.Load() {
			r.readers.Add(-1)
			runtime.Gosched()
			continue
		}

		break
	}
	defer r.readers.Add(-1)

	bufPtr := r.bufPtr.Load()
	buf := *bufPtr

	read := r.readPos.Load()
	write := r.writePos.Load()

	if write <= read {
		return false
	}

	available := write - read

	used := callback(
		buf,
		int(read),
		int(available),
	)

	if used < 0 || int64(used) > available {
		panic(fmt.Sprintf(
			"invalid ReadCallback result: used=%d available=%d",
			used,
			available,
		))
	}

	if used == 0 {
		return false
	}

	r.readPos.Store(read + int64(used))
	return true
}

func (r *SPSCBuffer) Size() int {
	return int(r.writePos.Load() - r.readPos.Load())
}
