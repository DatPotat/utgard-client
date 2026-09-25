// Utgard integration; GPL-3.0-or-later.
package amneziawg

import (
	"encoding/hex"
	"fmt"
	"strconv"
	"strings"
)

func unsigned(s string, max uint64) (uint64, error) {
	for _, c := range s {
		if c < '0' || c > '9' {
			return 0, fmt.Errorf("invalid unsigned integer")
		}
	}
	v, err := strconv.ParseUint(s, 10, 32)
	if err != nil || v > max {
		return 0, fmt.Errorf("integer out of range")
	}
	return v, nil
}

func signatureSize(spec string) (uint64, error) {
	var size uint64
	for spec = strings.TrimSpace(spec); spec != ""; spec = strings.TrimSpace(spec) {
		if spec[0] != '<' {
			return 0, fmt.Errorf("expected signature tag")
		}
		end := strings.IndexByte(spec, '>')
		if end < 0 {
			return 0, fmt.Errorf("unterminated signature tag")
		}
		fields := strings.Fields(spec[1:end])
		spec = spec[end+1:]
		if len(fields) == 1 && fields[0] == "t" {
			size += 4
		} else {
			if len(fields) != 2 {
				return 0, fmt.Errorf("invalid signature tag")
			}
			switch fields[0] {
			case "b":
				if !strings.HasPrefix(fields[1], "0x") {
					return 0, fmt.Errorf("expected hex bytes")
				}
				bytes, err := hex.DecodeString(fields[1][2:])
				if err != nil || len(bytes) == 0 {
					return 0, fmt.Errorf("invalid hex bytes")
				}
				size += uint64(len(bytes))
			case "r", "rc", "rd":
				n, err := unsigned(fields[1], 65507)
				if err != nil {
					return 0, err
				}
				size += n
			default:
				return 0, fmt.Errorf("unsupported AWG 2.0 signature tag")
			}
		}
		if size > 65507 {
			return 0, fmt.Errorf("signature packet too large")
		}
	}
	if size == 0 {
		return 0, fmt.Errorf("empty signature packet")
	}
	return size, nil
}

func validateParameters(config string, mtu uint32) (string, error) {
	if len(config) >= 8192 {
		return "", fmt.Errorf("AmneziaWG parameters too long")
	}
	seen := make(map[string]bool)
	values := make(map[string]uint64)
	ranges := [4][2]uint64{{1, 1}, {2, 2}, {3, 3}, {4, 4}}
	var result strings.Builder
	for _, line := range strings.Split(config, "\n") {
		if line == "" {
			continue
		}
		key, value, err := splitParameter(line)
		if err != nil {
			return "", err
		}
		if seen[key] {
			return "", fmt.Errorf("duplicate AmneziaWG parameter %s", key)
		}
		seen[key] = true
		switch key {
		case "jc", "jmin", "jmax", "s1", "s2", "s3", "s4":
			max := uint64(65507)
			if key == "jc" {
				max = 128
			}
			n, err := unsigned(value, max)
			if err != nil {
				return "", fmt.Errorf("invalid AmneziaWG %s", key)
			}
			values[key] = n
			// Zero means disabled. The upstream UAPI rejects explicit J*=0, while
			// a newly constructed device already has the required zero defaults.
			if n == 0 && key[0] == 'j' {
				continue
			}
		case "h1", "h2", "h3", "h4":
			low, high, hasRange := strings.Cut(value, "-")
			a, err := unsigned(low, 0xffffffff)
			if err != nil {
				return "", fmt.Errorf("invalid AmneziaWG %s", key)
			}
			b := a
			if hasRange {
				b, err = unsigned(high, 0xffffffff)
			}
			if err != nil || b < a {
				return "", fmt.Errorf("invalid AmneziaWG %s range", key)
			}
			ranges[key[1]-'1'] = [2]uint64{a, b}
		case "i1", "i2", "i3", "i4", "i5":
			if _, err := signatureSize(value); err != nil {
				return "", fmt.Errorf("invalid AmneziaWG %s: %w", key, err)
			}
		default:
			return "", fmt.Errorf("unsupported AmneziaWG device parameter")
		}
		result.WriteString(key + "=" + value + "\n")
	}
	if values["jmin"] > values["jmax"] || (values["jc"] > 0 && (values["jmin"] == 0 || values["jmax"] == 0)) {
		return "", fmt.Errorf("invalid AmneziaWG junk sizes")
	}
	for i, pair := range ranges {
		for j := i + 1; j < 4; j++ {
			if pair[0] <= ranges[j][1] && ranges[j][0] <= pair[1] {
				return "", fmt.Errorf("overlapping AmneziaWG headers")
			}
		}
	}
	for i, base := range []uint64{148, 92, 64, uint64(mtu) + 32} {
		if values[fmt.Sprintf("s%d", i+1)]+base > 65507 {
			return "", fmt.Errorf("AmneziaWG packet too large")
		}
	}
	return result.String(), nil
}
