package Utils

import (
	"runtime"
	"sync/atomic"
)

const CacheLineSize = 64

type queueNode[T any] struct {
	buffer []T
	next   atomic.Pointer[queueNode[T]]
}

type SPSCQueue[T any] struct {
	writeNode *queueNode[T]
	writePos  uint64
	_pad1     [CacheLineSize - 16]byte

	readNode *queueNode[T]
	readPos  uint64
	_pad2    [CacheLineSize - 16]byte

	writeCount uint64
	_pad3      [CacheLineSize - 8]byte

	readCount uint64
	_pad4     [CacheLineSize - 8]byte

	capacity uint64
	mask     uint64
}

func NewSPSCQueue[T any](capacity uint64) *SPSCQueue[T] {
	if capacity == 0 {
		panic("capacity must be greater than 0")
	}

	if capacity&(capacity-1) != 0 {
		panic("capacity must be a power of two")
	}

	node := &queueNode[T]{
		buffer: make([]T, capacity),
	}

	return &SPSCQueue[T]{
		writeNode: node,
		readNode:  node,
		capacity:  capacity,
		mask:      capacity - 1,
	}
}

func (q *SPSCQueue[T]) Push(v T) {
	node := q.writeNode
	pos := q.writePos

	if pos == q.capacity {
		next := node.next.Load()

		if next == nil {
			next = &queueNode[T]{
				buffer: make([]T, q.capacity),
			}

			node.next.Store(next)
		}

		node = next
		pos = 0

		q.writeNode = node
		q.writePos = 0
	}

	node.buffer[pos] = v

	q.writePos = pos + 1

	atomic.StoreUint64(
		&q.writeCount,
		atomic.LoadUint64(&q.writeCount)+1,
	)
}

func (q *SPSCQueue[T]) Pop() (T, bool) {
	var zero T

	readCount := atomic.LoadUint64(&q.readCount)
	writeCount := atomic.LoadUint64(&q.writeCount)

	if readCount == writeCount {
		return zero, false
	}

	node := q.readNode
	pos := q.readPos

	if pos == q.capacity {
		next := node.next.Load()

		if next == nil {
			return zero, false
		}

		node = next
		pos = 0

		q.readNode = node
		q.readPos = 0
	}

	v := node.buffer[pos]
	node.buffer[pos] = zero

	q.readPos = pos + 1

	atomic.StoreUint64(
		&q.readCount,
		readCount+1,
	)

	return v, true
}

func (q *SPSCQueue[T]) TryPush(v T) bool {
	q.Push(v)
	return true
}

func (q *SPSCQueue[T]) TryPop() (T, bool) {
	return q.Pop()
}

func (q *SPSCQueue[T]) SpinPush(v T) {
	for {
		if q.TryPush(v) {
			return
		}

		runtime.Gosched()
	}
}

func (q *SPSCQueue[T]) SpinPop() T {
	for {
		if v, ok := q.TryPop(); ok {
			return v
		}

		runtime.Gosched()
	}
}

func (q *SPSCQueue[T]) Len() int {
	write := atomic.LoadUint64(&q.writeCount)
	read := atomic.LoadUint64(&q.readCount)

	return int(write - read)
}

func (q *SPSCQueue[T]) Empty() bool {
	return atomic.LoadUint64(&q.writeCount) ==
		atomic.LoadUint64(&q.readCount)
}

func (q *SPSCQueue[T]) Capacity() int {
	return int(q.capacity)
}
