package device

import "testing"

func TestUtgardUintRangeFullRange(t *testing.T) {
	var r UintRange
	if err := r.FromString("0-4294967295"); err != nil {
		t.Fatal(err)
	}
	var different bool
	first := r.PickOne()
	for i := 0; i < 100; i++ {
		if r.PickOne() != first {
			different = true
		}
	}
	if !different {
		t.Fatal("full range collapsed to a constant")
	}
}
