package smart2raw

import (
	"encoding/binary"
	"hash/crc32"
	"os"
	"path/filepath"
	"testing"
)

// writeRaw builds a .s2r file whose header fields can be poisoned individually.
// The CRC is always correct, so every rejection below is due to the field under
// test and not to a checksum mismatch.
func writeRaw(t *testing.T, name string, cls int8, flags, fmtb, rsvd byte, count uint64, payload []byte) string {
	t.Helper()
	b := append([]byte(Magic), byte(cls), flags, fmtb, rsvd)
	var c [8]byte
	binary.LittleEndian.PutUint64(c[:], count)
	b = append(b, c[:]...)
	b = append(b, payload...)
	var crc [4]byte
	binary.LittleEndian.PutUint32(crc[:], crc32.ChecksumIEEE(payload))
	b = append(b, crc[:]...)
	p := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(p, b, 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

// A declared count of 0x8000000000000002 with a 2-byte class used to wrap when
// converted to int and multiplied: payloadLen came out as 4, the 24-byte file
// passed the length check, the CRC over the 4 real bytes matched, and Load
// returned a 2-element pool built from an absurd header.
func TestLoadRejectsCountOverflow(t *testing.T) {
	p := writeRaw(t, "ovf.s2r", 16, 0, FormatVersion, 0, 0x8000000000000002, []byte{1, 0, 2, 0})
	if pool, err := Load(p); err == nil {
		t.Fatalf("accepted overflowing count: got pool with %d elements", pool.Len())
	}
}

func TestLoadRejectsCountMismatch(t *testing.T) {
	// 4 payload bytes at class 16 is 2 elements; the header claims 3.
	p := writeRaw(t, "mismatch.s2r", 16, 0, FormatVersion, 0, 3, []byte{1, 0, 2, 0})
	if _, err := Load(p); err == nil {
		t.Fatal("accepted a count that disagrees with the payload length")
	}
}

func TestLoadRejectsRaggedPayload(t *testing.T) {
	// 3 payload bytes cannot be a whole number of 2-byte elements.
	p := writeRaw(t, "ragged.s2r", 16, 0, FormatVersion, 0, 1, []byte{1, 0, 2})
	if _, err := Load(p); err == nil {
		t.Fatal("accepted a payload length that is not a multiple of the element size")
	}
}

func TestLoadRejectsNonzeroReserved(t *testing.T) {
	p := writeRaw(t, "rsvd.s2r", 8, 0, FormatVersion, 7, 3, []byte{1, 2, 3})
	if _, err := Load(p); err == nil {
		t.Fatal("accepted a nonzero reserved byte")
	}
}

func TestLoadRejectsBadFormatVersion(t *testing.T) {
	p := writeRaw(t, "fmt.s2r", 8, 0, 99, 0, 3, []byte{1, 2, 3})
	if _, err := Load(p); err == nil {
		t.Fatal("accepted an unsupported format version")
	}
}

// The hardening must not have broken the happy path.
func TestLoadAcceptsValidFile(t *testing.T) {
	p := writeRaw(t, "ok.s2r", 8, 0, FormatVersion, 0, 3, []byte{10, 20, 30})
	pool, err := Load(p)
	if err != nil {
		t.Fatalf("rejected a valid file: %v", err)
	}
	if pool.Len() != 3 {
		t.Fatalf("count = %d, want 3", pool.Len())
	}
	sum, err := pool.SumInt64()
	if err != nil || sum != 60 {
		t.Fatalf("sum = %d (err %v), want 60", sum, err)
	}
}

func TestLoadAcceptsSignedFile(t *testing.T) {
	p := writeRaw(t, "signed.s2r", -8, FlagSigned, FormatVersion, 0, 2, []byte{0xFF, 0x80})
	pool, err := Load(p)
	if err != nil {
		t.Fatalf("rejected a valid signed file: %v", err)
	}
	sum, err := pool.SumInt64()
	if err != nil || sum != -129 {
		t.Fatalf("sum = %d (err %v), want -129", sum, err)
	}
}

func TestLoadAcceptsEmptyPool(t *testing.T) {
	p := writeRaw(t, "empty.s2r", 8, 0, FormatVersion, 0, 0, nil)
	pool, err := Load(p)
	if err != nil {
		t.Fatalf("rejected an empty pool: %v", err)
	}
	if pool.Len() != 0 {
		t.Fatalf("count = %d, want 0", pool.Len())
	}
}
