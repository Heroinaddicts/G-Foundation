package main

// Drop the completed workload's references only after its drained snapshot.
func (m *TaskTest) MemoryReset() { *m = TaskTest{} }
