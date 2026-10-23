package main

// Drop the completed workload's references only after its drained snapshot.
func (m *RedisTest) MemoryReset() { *m = RedisTest{} }

func (m *RedisTest) MemoryPrepare()           { m.memoryArmed = true }
func (m *RedisTest) MemoryDone() (bool, bool) { return m.finished, m.failed != 0 }
