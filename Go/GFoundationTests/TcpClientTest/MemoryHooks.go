package main

// Drop the completed workload's references only after its drained snapshot.
func (m *TcpClientTest) MemoryReset() { *m = TcpClientTest{} }
