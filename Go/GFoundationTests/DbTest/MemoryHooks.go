package main

// Drop the completed workload's references only after its drained snapshot.
func (m *DbTest) MemoryReset() { *m = DbTest{} }
