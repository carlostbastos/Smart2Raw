// Smart2Raw Go port
// Copyright (C) 2026 Carlos Alberto Terencio Bastos
// SPDX-License-Identifier: AGPL-3.0-or-later

package smart2raw

import (
	"encoding/binary"
	"errors"
	"hash/crc32"
	"io"
	"os"
	"sort"
)

const (
	Magic         = "SR33"
	FormatVersion = byte(1)
	FlagSigned    = byte(1)
)

// maxSliceLen is the largest value representable as an int on this platform.
// Used to reject a declared element count that could not be indexed anyway.
const maxSliceLen = uint64(^uint(0) >> 1)

type Class int8

const (
	U8  Class = 8
	U16 Class = 16
	U32 Class = 32
	U64 Class = 64
	I8  Class = -8
	I16 Class = -16
	I32 Class = -32
	I64 Class = -64
)

func (c Class) Signed() bool { return c < 0 }

func (c Class) WidthBits() int {
	if c < 0 {
		return int(-c)
	}
	return int(c)
}

func (c Class) ElemBytes() int { return c.WidthBits() / 8 }

func ClassifyUint(v uint64) Class {
	switch {
	case v <= 0xff:
		return U8
	case v <= 0xffff:
		return U16
	case v <= 0xffffffff:
		return U32
	default:
		return U64
	}
}

func ClassifyIntRange(minv, maxv int64) Class {
	switch {
	case minv >= -128 && maxv <= 127:
		return I8
	case minv >= -32768 && maxv <= 32767:
		return I16
	case minv >= -2147483648 && maxv <= 2147483647:
		return I32
	default:
		return I64
	}
}

var (
	ErrWrongSignedness  = errors.New("smart2raw: wrong signedness")
	ErrIndexOutOfBounds = errors.New("smart2raw: index out of bounds")
	ErrBadFormat        = errors.New("smart2raw: bad s2r format")
	ErrCRCMismatch      = errors.New("smart2raw: crc mismatch")
)

type Pool struct {
	class Class
	u8    []uint8
	u16   []uint16
	u32   []uint32
	u64   []uint64
	i8    []int8
	i16   []int16
	i32   []int32
	i64   []int64
}

func NewUnsigned() *Pool { return &Pool{class: U8} }
func NewSigned() *Pool   { return &Pool{class: I8} }

func (p *Pool) Class() Class { return p.class }
func (p *Pool) Signed() bool { return p.class.Signed() }

func (p *Pool) Len() int {
	switch p.class {
	case U8:
		return len(p.u8)
	case U16:
		return len(p.u16)
	case U32:
		return len(p.u32)
	case U64:
		return len(p.u64)
	case I8:
		return len(p.i8)
	case I16:
		return len(p.i16)
	case I32:
		return len(p.i32)
	case I64:
		return len(p.i64)
	default:
		return 0
	}
}

func (p *Pool) BytesUsed() int { return p.Len() * p.class.ElemBytes() }

func (p *Pool) PushUint(v uint64) error {
	if p.Signed() {
		return ErrWrongSignedness
	}
	target := ClassifyUint(v)
	if target.WidthBits() > p.class.WidthBits() {
		p.reclassUnsigned(target)
	}
	switch p.class {
	case U8:
		p.u8 = append(p.u8, uint8(v))
	case U16:
		p.u16 = append(p.u16, uint16(v))
	case U32:
		p.u32 = append(p.u32, uint32(v))
	case U64:
		p.u64 = append(p.u64, v)
	}
	return nil
}

func (p *Pool) PushInt(v int64) error {
	if !p.Signed() {
		return ErrWrongSignedness
	}
	target := ClassifyIntRange(v, v)
	if target.WidthBits() > p.class.WidthBits() {
		p.reclassSigned(target)
	}
	switch p.class {
	case I8:
		p.i8 = append(p.i8, int8(v))
	case I16:
		p.i16 = append(p.i16, int16(v))
	case I32:
		p.i32 = append(p.i32, int32(v))
	case I64:
		p.i64 = append(p.i64, v)
	}
	return nil
}

func (p *Pool) GetInt64(i int) (int64, error) {
	if i < 0 || i >= p.Len() {
		return 0, ErrIndexOutOfBounds
	}
	switch p.class {
	case U8:
		return int64(p.u8[i]), nil
	case U16:
		return int64(p.u16[i]), nil
	case U32:
		return int64(p.u32[i]), nil
	case U64:
		if p.u64[i] > uint64(^uint64(0)>>1) {
			return 0, errors.New("smart2raw: value exceeds int64")
		}
		return int64(p.u64[i]), nil
	case I8:
		return int64(p.i8[i]), nil
	case I16:
		return int64(p.i16[i]), nil
	case I32:
		return int64(p.i32[i]), nil
	case I64:
		return p.i64[i], nil
	default:
		return 0, ErrBadFormat
	}
}

func (p *Pool) SumInt64() (int64, error) {
	var s int64
	for i := 0; i < p.Len(); i++ {
		v, err := p.GetInt64(i)
		if err != nil {
			return 0, err
		}
		s += v
	}
	return s, nil
}

func (p *Pool) RemoveSwap(i int) error {
	if i < 0 || i >= p.Len() {
		return ErrIndexOutOfBounds
	}
	switch p.class {
	case U8:
		p.u8[i] = p.u8[len(p.u8)-1]
		p.u8 = p.u8[:len(p.u8)-1]
	case U16:
		p.u16[i] = p.u16[len(p.u16)-1]
		p.u16 = p.u16[:len(p.u16)-1]
	case U32:
		p.u32[i] = p.u32[len(p.u32)-1]
		p.u32 = p.u32[:len(p.u32)-1]
	case U64:
		p.u64[i] = p.u64[len(p.u64)-1]
		p.u64 = p.u64[:len(p.u64)-1]
	case I8:
		p.i8[i] = p.i8[len(p.i8)-1]
		p.i8 = p.i8[:len(p.i8)-1]
	case I16:
		p.i16[i] = p.i16[len(p.i16)-1]
		p.i16 = p.i16[:len(p.i16)-1]
	case I32:
		p.i32[i] = p.i32[len(p.i32)-1]
		p.i32 = p.i32[:len(p.i32)-1]
	case I64:
		p.i64[i] = p.i64[len(p.i64)-1]
		p.i64 = p.i64[:len(p.i64)-1]
	}
	return nil
}

func (p *Pool) FitClass() error {
	if p.Len() == 0 {
		if p.Signed() {
			p.class = I8
		} else {
			p.class = U8
		}
		p.clearDataExceptClass()
		return nil
	}
	if p.Signed() {
		minv, maxv, err := p.minMaxInt64()
		if err != nil {
			return err
		}
		target := ClassifyIntRange(minv, maxv)
		if target != p.class {
			p.reclassSigned(target)
		}
	} else {
		maxv, err := p.maxUint64()
		if err != nil {
			return err
		}
		target := ClassifyUint(maxv)
		if target != p.class {
			p.reclassUnsigned(target)
		}
	}
	return nil
}


// Sort orders the pool in place while preserving its current Smart2Raw class.
func (p *Pool) Sort() error {
	if p.Signed() {
		switch p.class {
		case I8:
			sort.Slice(p.i8, func(i, j int) bool { return p.i8[i] < p.i8[j] })
		case I16:
			sort.Slice(p.i16, func(i, j int) bool { return p.i16[i] < p.i16[j] })
		case I32:
			sort.Slice(p.i32, func(i, j int) bool { return p.i32[i] < p.i32[j] })
		case I64:
			sort.Slice(p.i64, func(i, j int) bool { return p.i64[i] < p.i64[j] })
		default:
			return ErrBadFormat
		}
		return nil
	}
	switch p.class {
	case U8:
		sort.Slice(p.u8, func(i, j int) bool { return p.u8[i] < p.u8[j] })
	case U16:
		sort.Slice(p.u16, func(i, j int) bool { return p.u16[i] < p.u16[j] })
	case U32:
		sort.Slice(p.u32, func(i, j int) bool { return p.u32[i] < p.u32[j] })
	case U64:
		sort.Slice(p.u64, func(i, j int) bool { return p.u64[i] < p.u64[j] })
	default:
		return ErrBadFormat
	}
	return nil
}

// IsSorted reports whether values are in ascending order.
func (p *Pool) IsSorted() bool {
	for i := 1; i < p.Len(); i++ {
		a, ea := p.GetInt64(i - 1)
		b, eb := p.GetInt64(i)
		if ea != nil || eb != nil || a > b {
			return false
		}
	}
	return true
}

// UniqueSorted removes adjacent duplicates from an already sorted pool.
func (p *Pool) UniqueSorted() error {
	if p.Len() < 2 {
		return nil
	}
	if p.Signed() {
		vals := p.valuesInt64()
		w := 1
		for r := 1; r < len(vals); r++ {
			if vals[r] != vals[w-1] {
				vals[w] = vals[r]
				w++
			}
		}
		p.clearDataExceptClass()
		p.class = I64
		p.i64 = vals[:w]
		return p.FitClass()
	}
	vals := p.valuesUint64()
	w := 1
	for r := 1; r < len(vals); r++ {
		if vals[r] != vals[w-1] {
			vals[w] = vals[r]
			w++
		}
	}
	p.clearDataExceptClass()
	p.class = U64
	p.u64 = vals[:w]
	return p.FitClass()
}

// NUnique counts distinct values without modifying the pool.
func (p *Pool) NUnique() (int, error) {
	seen := make(map[int64]struct{}, p.Len())
	for i := 0; i < p.Len(); i++ {
		v, err := p.GetInt64(i)
		if err != nil {
			return 0, err
		}
		seen[v] = struct{}{}
	}
	return len(seen), nil
}

// ValueCounts returns a value -> frequency map. It is intended for analytics and tests;
// hot u8 paths should prefer specialized histograms in the C core.
func (p *Pool) ValueCounts() (map[int64]uint64, error) {
	out := make(map[int64]uint64)
	for i := 0; i < p.Len(); i++ {
		v, err := p.GetInt64(i)
		if err != nil {
			return nil, err
		}
		out[v]++
	}
	return out, nil
}

func (p *Pool) Save(path string) error {
	payload := p.payloadLE()
	crc := crc32.ChecksumIEEE(payload)
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err := f.Write([]byte(Magic)); err != nil {
		return err
	}
	flags := byte(0)
	if p.Signed() {
		flags = FlagSigned
	}
	header := []byte{byte(int8(p.class)), flags, FormatVersion, 0}
	if _, err := f.Write(header); err != nil {
		return err
	}
	var count [8]byte
	binary.LittleEndian.PutUint64(count[:], uint64(p.Len()))
	if _, err := f.Write(count[:]); err != nil {
		return err
	}
	if _, err := f.Write(payload); err != nil {
		return err
	}
	var cbuf [4]byte
	binary.LittleEndian.PutUint32(cbuf[:], crc)
	_, err = f.Write(cbuf[:])
	return err
}

func Load(path string) (*Pool, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	b, err := io.ReadAll(f)
	if err != nil {
		return nil, err
	}
	if len(b) < 20 || string(b[:4]) != Magic {
		return nil, ErrBadFormat
	}
	class := Class(int8(b[4]))
	if !validClass(class) {
		return nil, ErrBadFormat
	}
	flags, fmt, rsvd := b[5], b[6], b[7]
	if fmt != FormatVersion {
		return nil, ErrBadFormat
	}
	if rsvd != 0 {
		return nil, ErrBadFormat
	}
	if class.Signed() != ((flags & FlagSigned) != 0) {
		return nil, ErrBadFormat
	}
	// The declared count is attacker-controlled. Converting it to a signed int
	// and multiplying by the element size wrapped: a header claiming
	// count=0x8000000000000002 with class 16 produced payloadLen==4, so a 24-byte
	// file passed the length check and decoded as a 2-element pool.
	//
	// Derive the count from the file instead of trusting it: the payload is
	// exactly len(b)-20 bytes, so divide rather than multiply. No overflow is
	// reachable, and the declared count must agree exactly.
	declared := binary.LittleEndian.Uint64(b[8:16])
	elemBytes := uint64(class.ElemBytes())
	if elemBytes == 0 {
		return nil, ErrBadFormat
	}
	payloadBytes := uint64(len(b)) - 20
	if payloadBytes%elemBytes != 0 || payloadBytes/elemBytes != declared {
		return nil, ErrBadFormat
	}
	if declared > uint64(maxSliceLen) {
		return nil, ErrBadFormat
	}
	count := int(declared)
	payloadLen := count * class.ElemBytes()
	payload := b[16 : 16+payloadLen]
	stored := binary.LittleEndian.Uint32(b[16+payloadLen:])
	if crc32.ChecksumIEEE(payload) != stored {
		return nil, ErrCRCMismatch
	}
	return poolFromPayload(class, payload)
}

func (p *Pool) reclassUnsigned(target Class) {
	vals := p.valuesUint64()
	p.clearDataExceptClass()
	p.class = target
	switch target {
	case U8:
		p.u8 = make([]uint8, len(vals))
		for i, v := range vals {
			p.u8[i] = uint8(v)
		}
	case U16:
		p.u16 = make([]uint16, len(vals))
		for i, v := range vals {
			p.u16[i] = uint16(v)
		}
	case U32:
		p.u32 = make([]uint32, len(vals))
		for i, v := range vals {
			p.u32[i] = uint32(v)
		}
	case U64:
		p.u64 = vals
	}
}

func (p *Pool) reclassSigned(target Class) {
	vals := p.valuesInt64()
	p.clearDataExceptClass()
	p.class = target
	switch target {
	case I8:
		p.i8 = make([]int8, len(vals))
		for i, v := range vals {
			p.i8[i] = int8(v)
		}
	case I16:
		p.i16 = make([]int16, len(vals))
		for i, v := range vals {
			p.i16[i] = int16(v)
		}
	case I32:
		p.i32 = make([]int32, len(vals))
		for i, v := range vals {
			p.i32[i] = int32(v)
		}
	case I64:
		p.i64 = vals
	}
}

func (p *Pool) valuesUint64() []uint64 {
	switch p.class {
	case U8:
		out := make([]uint64, len(p.u8))
		for i, v := range p.u8 {
			out[i] = uint64(v)
		}
		return out
	case U16:
		out := make([]uint64, len(p.u16))
		for i, v := range p.u16 {
			out[i] = uint64(v)
		}
		return out
	case U32:
		out := make([]uint64, len(p.u32))
		for i, v := range p.u32 {
			out[i] = uint64(v)
		}
		return out
	case U64:
		return append([]uint64(nil), p.u64...)
	default:
		return nil
	}
}

func (p *Pool) valuesInt64() []int64 {
	switch p.class {
	case I8:
		out := make([]int64, len(p.i8))
		for i, v := range p.i8 {
			out[i] = int64(v)
		}
		return out
	case I16:
		out := make([]int64, len(p.i16))
		for i, v := range p.i16 {
			out[i] = int64(v)
		}
		return out
	case I32:
		out := make([]int64, len(p.i32))
		for i, v := range p.i32 {
			out[i] = int64(v)
		}
		return out
	case I64:
		return append([]int64(nil), p.i64...)
	default:
		return nil
	}
}

func (p *Pool) clearDataExceptClass() {
	p.u8, p.u16, p.u32, p.u64 = nil, nil, nil, nil
	p.i8, p.i16, p.i32, p.i64 = nil, nil, nil, nil
}

func (p *Pool) minMaxInt64() (int64, int64, error) {
	if p.Len() == 0 {
		return 0, 0, ErrIndexOutOfBounds
	}
	first, err := p.GetInt64(0)
	if err != nil {
		return 0, 0, err
	}
	minv, maxv := first, first
	for i := 1; i < p.Len(); i++ {
		v, err := p.GetInt64(i)
		if err != nil {
			return 0, 0, err
		}
		if v < minv {
			minv = v
		}
		if v > maxv {
			maxv = v
		}
	}
	return minv, maxv, nil
}

func (p *Pool) maxUint64() (uint64, error) {
	vals := p.valuesUint64()
	if len(vals) == 0 {
		return 0, ErrIndexOutOfBounds
	}
	m := vals[0]
	for _, v := range vals[1:] {
		if v > m {
			m = v
		}
	}
	return m, nil
}

func (p *Pool) payloadLE() []byte {
	out := make([]byte, 0, p.BytesUsed())
	switch p.class {
	case U8:
		out = append(out, p.u8...)
	case I8:
		for _, v := range p.i8 {
			out = append(out, byte(v))
		}
	case U16:
		for _, v := range p.u16 {
			var b [2]byte
			binary.LittleEndian.PutUint16(b[:], v)
			out = append(out, b[:]...)
		}
	case I16:
		for _, v := range p.i16 {
			var b [2]byte
			binary.LittleEndian.PutUint16(b[:], uint16(v))
			out = append(out, b[:]...)
		}
	case U32:
		for _, v := range p.u32 {
			var b [4]byte
			binary.LittleEndian.PutUint32(b[:], v)
			out = append(out, b[:]...)
		}
	case I32:
		for _, v := range p.i32 {
			var b [4]byte
			binary.LittleEndian.PutUint32(b[:], uint32(v))
			out = append(out, b[:]...)
		}
	case U64:
		for _, v := range p.u64 {
			var b [8]byte
			binary.LittleEndian.PutUint64(b[:], v)
			out = append(out, b[:]...)
		}
	case I64:
		for _, v := range p.i64 {
			var b [8]byte
			binary.LittleEndian.PutUint64(b[:], uint64(v))
			out = append(out, b[:]...)
		}
	}
	return out
}

func poolFromPayload(class Class, payload []byte) (*Pool, error) {
	p := &Pool{class: class}
	switch class {
	case U8:
		p.u8 = append([]uint8(nil), payload...)
	case I8:
		p.i8 = make([]int8, len(payload))
		for i, v := range payload {
			p.i8[i] = int8(v)
		}
	case U16:
		if len(payload)%2 != 0 {
			return nil, ErrBadFormat
		}
		p.u16 = make([]uint16, len(payload)/2)
		for i := range p.u16 {
			p.u16[i] = binary.LittleEndian.Uint16(payload[i*2:])
		}
	case I16:
		if len(payload)%2 != 0 {
			return nil, ErrBadFormat
		}
		p.i16 = make([]int16, len(payload)/2)
		for i := range p.i16 {
			p.i16[i] = int16(binary.LittleEndian.Uint16(payload[i*2:]))
		}
	case U32:
		if len(payload)%4 != 0 {
			return nil, ErrBadFormat
		}
		p.u32 = make([]uint32, len(payload)/4)
		for i := range p.u32 {
			p.u32[i] = binary.LittleEndian.Uint32(payload[i*4:])
		}
	case I32:
		if len(payload)%4 != 0 {
			return nil, ErrBadFormat
		}
		p.i32 = make([]int32, len(payload)/4)
		for i := range p.i32 {
			p.i32[i] = int32(binary.LittleEndian.Uint32(payload[i*4:]))
		}
	case U64:
		if len(payload)%8 != 0 {
			return nil, ErrBadFormat
		}
		p.u64 = make([]uint64, len(payload)/8)
		for i := range p.u64 {
			p.u64[i] = binary.LittleEndian.Uint64(payload[i*8:])
		}
	case I64:
		if len(payload)%8 != 0 {
			return nil, ErrBadFormat
		}
		p.i64 = make([]int64, len(payload)/8)
		for i := range p.i64 {
			p.i64[i] = int64(binary.LittleEndian.Uint64(payload[i*8:]))
		}
	default:
		return nil, ErrBadFormat
	}
	return p, nil
}

func validClass(c Class) bool {
	switch c {
	case U8, U16, U32, U64, I8, I16, I32, I64:
		return true
	}
	return false
}
