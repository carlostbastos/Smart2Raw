// Smart2Raw Go port tests
// Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
// SPDX-License-Identifier: AGPL-3.0-or-later

package smart2raw

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestUnsignedPromotesAndSums(t *testing.T) {
	p := NewUnsigned()
	for _, v := range []uint64{25, 255, 256} {
		if err := p.PushUint(v); err != nil {
			t.Fatal(err)
		}
	}
	if p.Class() != U16 {
		t.Fatalf("class = %v, want U16", p.Class())
	}
	if p.Len() != 3 {
		t.Fatalf("len = %d", p.Len())
	}
	if p.BytesUsed() != 6 {
		t.Fatalf("bytes = %d", p.BytesUsed())
	}
	s, err := p.SumInt64()
	if err != nil {
		t.Fatal(err)
	}
	if s != 536 {
		t.Fatalf("sum = %d", s)
	}
}

func TestSignedPromotesAndSums(t *testing.T) {
	p := NewSigned()
	for _, v := range []int64{-10, 35, 128} {
		if err := p.PushInt(v); err != nil {
			t.Fatal(err)
		}
	}
	if p.Class() != I16 {
		t.Fatalf("class = %v, want I16", p.Class())
	}
	s, err := p.SumInt64()
	if err != nil {
		t.Fatal(err)
	}
	if s != 153 {
		t.Fatalf("sum = %d", s)
	}
}

func TestFitClassDemotesAfterOutlierRemoval(t *testing.T) {
	p := NewUnsigned()
	for _, v := range []uint64{1, 2, 5_000_000_000} {
		if err := p.PushUint(v); err != nil {
			t.Fatal(err)
		}
	}
	if p.Class() != U64 {
		t.Fatalf("class = %v, want U64", p.Class())
	}
	if err := p.RemoveSwap(2); err != nil {
		t.Fatal(err)
	}
	if err := p.FitClass(); err != nil {
		t.Fatal(err)
	}
	if p.Class() != U8 {
		t.Fatalf("class = %v, want U8", p.Class())
	}
	if p.BytesUsed() != 2 {
		t.Fatalf("bytes = %d", p.BytesUsed())
	}
}

func TestSaveLoadUnsigned(t *testing.T) {
	p := NewUnsigned()
	for _, v := range []uint64{25, 30, 40, 10, 5, 60, 20, 50} {
		if err := p.PushUint(v); err != nil {
			t.Fatal(err)
		}
	}
	path := t.TempDir() + "/unsigned.s2r"
	if err := p.Save(path); err != nil {
		t.Fatal(err)
	}
	q, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if q.Class() != U8 {
		t.Fatalf("class = %v", q.Class())
	}
	s, err := q.SumInt64()
	if err != nil {
		t.Fatal(err)
	}
	if s != 240 {
		t.Fatalf("sum = %d", s)
	}
}

func TestSaveLoadSigned(t *testing.T) {
	p := NewSigned()
	for _, v := range []int64{-10, -3, 20, 35} {
		if err := p.PushInt(v); err != nil {
			t.Fatal(err)
		}
	}
	path := t.TempDir() + "/signed.s2r"
	if err := p.Save(path); err != nil {
		t.Fatal(err)
	}
	q, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if q.Class() != I8 {
		t.Fatalf("class = %v", q.Class())
	}
	s, err := q.SumInt64()
	if err != nil {
		t.Fatal(err)
	}
	if s != 42 {
		t.Fatalf("sum = %d", s)
	}
}

func TestCrcMismatch(t *testing.T) {
	p := NewUnsigned()
	if err := p.PushUint(7); err != nil {
		t.Fatal(err)
	}
	path := t.TempDir() + "/bad.s2r"
	if err := p.Save(path); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	b[16] ^= 0xff
	if err := os.WriteFile(path, b, 0600); err != nil {
		t.Fatal(err)
	}
	_, err = Load(path)
	if !errors.Is(err, ErrCRCMismatch) {
		t.Fatalf("err = %v, want crc mismatch", err)
	}
}

func TestWrongSignedness(t *testing.T) {
	p := NewUnsigned()
	if err := p.PushInt(-1); !errors.Is(err, ErrWrongSignedness) {
		t.Fatalf("err = %v", err)
	}
}


func TestConformanceFixtures(t *testing.T) {
	type fixture struct {
		File   string  `json:"file"`
		Class  Class   `json:"class"`
		Count  int     `json:"count"`
		Sum    int64   `json:"sum"`
		Values []int64 `json:"values"`
	}
	root := filepath.Join("..", "..", "conformance", "fixtures")
	b, err := os.ReadFile(filepath.Join(root, "manifest.json"))
	if err != nil {
		t.Fatal(err)
	}
	var fixtures []fixture
	if err := json.Unmarshal(b, &fixtures); err != nil {
		t.Fatal(err)
	}
	for _, fx := range fixtures {
		fx := fx
		t.Run(fx.File, func(t *testing.T) {
			p, err := Load(filepath.Join(root, fx.File))
			if err != nil {
				t.Fatal(err)
			}
			if p.Class() != fx.Class || p.Len() != fx.Count {
				t.Fatalf("class/count = %v/%d, want %v/%d", p.Class(), p.Len(), fx.Class, fx.Count)
			}
			s, err := p.SumInt64()
			if err != nil {
				t.Fatal(err)
			}
			if s != fx.Sum {
				t.Fatalf("sum = %d, want %d", s, fx.Sum)
			}
			for i, want := range fx.Values {
				got, err := p.GetInt64(i)
				if err != nil {
					t.Fatal(err)
				}
				if got != want {
					t.Fatalf("value[%d] = %d, want %d", i, got, want)
				}
			}
		})
	}
	if _, err := Load(filepath.Join(root, "corrupted_crc.s2r")); !errors.Is(err, ErrCRCMismatch) {
		t.Fatalf("corrupted fixture err = %v, want crc mismatch", err)
	}
}


func TestAnalyticsV2SortUniqueCounts(t *testing.T) {
	p := NewSigned()
	for _, v := range []int64{10, -1, -128, 10, 0, -1} {
		if err := p.PushInt(v); err != nil {
			t.Fatal(err)
		}
	}
	if p.IsSorted() {
		t.Fatal("pool unexpectedly sorted")
	}
	if err := p.Sort(); err != nil {
		t.Fatal(err)
	}
	if !p.IsSorted() {
		t.Fatal("pool not sorted")
	}
	want := []int64{-128, -1, -1, 0, 10, 10}
	for i, w := range want {
		got, err := p.GetInt64(i)
		if err != nil || got != w {
			t.Fatalf("value[%d]=%d err=%v want %d", i, got, err, w)
		}
	}
	n, err := p.NUnique()
	if err != nil || n != 4 {
		t.Fatalf("nunique=%d err=%v want 4", n, err)
	}
	counts, err := p.ValueCounts()
	if err != nil {
		t.Fatal(err)
	}
	if counts[-1] != 2 || counts[10] != 2 || counts[-128] != 1 {
		t.Fatalf("bad counts: %#v", counts)
	}
	if err := p.UniqueSorted(); err != nil {
		t.Fatal(err)
	}
	if p.Len() != 4 || p.Class() != I8 {
		t.Fatalf("len/class = %d/%v, want 4/I8", p.Len(), p.Class())
	}
}
