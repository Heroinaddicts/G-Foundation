package StressKit

import (
	"encoding/binary"
	"fmt"
)

// Each TCP frame carries a size, request ID and deterministic binary body.
// Receivers must retain incomplete frames and return only consumed bytes.
func Payload(id uint64, n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte((id*31 + uint64(i)*17) % 251)
	}
	return b
}
func Frame(id uint64, n int) []byte {
	if n < 24 {
		n = 24
	}
	b := Payload(id, n)
	binary.BigEndian.PutUint32(b, uint32(n))
	binary.BigEndian.PutUint64(b[4:], id)
	return b
}
func CheckFrame(b []byte) error {
	if len(b) < 24 || int(binary.BigEndian.Uint32(b)) != len(b) {
		return fmt.Errorf("invalid frame size")
	}
	id := binary.BigEndian.Uint64(b[4:])
	for i := 12; i < len(b); i++ {
		if b[i] != byte((id*31+uint64(i)*17)%251) {
			return fmt.Errorf("frame checksum mismatch id=%d offset=%d", id, i)
		}
	}
	return nil
}
