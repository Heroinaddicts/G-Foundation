package main

import (
	"G-Foundation/Utils"
	"fmt"
)

func main() {
	q := Utils.NewSPSCQueue[int](
		1024,
	)

	go func() {
		for i := 0; i <= 10_000_000; i++ {
			q.Push(i)
		}
	}()

	last := 0
	for {
		v, ok := q.Pop()
		if ok {
			fmt.Printf("Pop: %d\n", v)
			if v != last {
				panic(fmt.Sprintf("expected %d, got %d", last, v))
			}
			last++
		}
	}
}
