package Utils

/*
#cgo LDFLAGS: -lpthread
#include <pthread.h>
#include <stdint.h>

uint64_t XGetThreadID(void)
{
	uint64_t ThreadID = 0;
	pthread_threadid_np(NULL, &ThreadID);
	return ThreadID;
}
*/
import "C"

func GetThreadID() uint64 {
	return uint64(C.XGetThreadID())
}
