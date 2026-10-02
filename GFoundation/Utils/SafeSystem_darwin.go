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
import (
	"os"
	"path/filepath"
)

func GetThreadID() uint64 {
	return uint64(C.XGetThreadID())
}

func GetCurrentExeDir() string {
	exe, err := os.Executable()
	if err != nil {
		panic(err)
	}

	return filepath.Dir(exe)
}
