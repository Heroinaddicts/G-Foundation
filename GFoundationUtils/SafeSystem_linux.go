package GFoundationUtils

import "syscall"

func GetThreadID() uint64 {
	return uint64(syscall.Gettid())
}
