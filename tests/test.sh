#!/bin/sh
set -eu
mkdir -p build
cc -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc -Ivendor/parson tests/awg_test.c src/awg.c src/link.c src/profiles.c src/genconf.c vendor/parson/parson.c \
    -o build/awg-test
ASAN_OPTIONS=detect_leaks=1 ./build/awg-test
python3 scripts/prepare-core.py
ROOT=$(pwd)
cd core
go test -mod=readonly -modfile "$ROOT/build/core-build.mod" -race -tags with_gvisor,with_wireguard,with_quic,with_utls ./amneziawg
go test -mod=readonly -modfile "$ROOT/build/core-build.mod" github.com/amnezia-vpn/amneziawg-go/v3/device -run TestUtgardUintRangeFullRange
