// Utgard regression test; MIT, like the surrounding vendored package.
package device

import "testing"

func TestUtgardMagicHeaderFullRange(t *testing.T) {
 header, err := newMagicHeader("0-4294967295")
 if err != nil { t.Fatal(err) }
 for i := 0; i < 32; i++ {
  if !header.Validate(header.Generate()) { t.Fatal("generated value outside range") }
 }
}
