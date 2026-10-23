package GFoundationUtils

import (
	"os"
	"path/filepath"
)

func GetCurrentExeDir() string {
	exe, err := os.Executable()
	if err != nil {
		panic(err)
	}
	return filepath.Dir(exe)
}
