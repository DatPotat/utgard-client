#!/bin/sh
set -eu
mkdir -p build
cc -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc -Ivendor/parson tests/awg_test.c src/awg.c src/link.c src/profiles.c src/genconf.c vendor/parson/parson.c \
    -o build/awg-test
ASAN_OPTIONS=detect_leaks=1 ./build/awg-test
cd vendor/sing-box
go test -mod=readonly -race -tags with_gvisor,with_wireguard,with_quic,with_utls ./protocol/amneziawg
go test -mod=readonly github.com/amnezia-vpn/amneziawg-go/device -run TestUtgardMagicHeaderFullRange
