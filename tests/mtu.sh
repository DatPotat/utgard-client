#!/bin/sh
# Requires NET_ADMIN; run ONLY in an isolated Docker container, not on the host.
set -eu
test -f /.dockerenv || { echo 'Run this test inside Docker.' >&2; exit 1; }
ip link set dev lo mtu 1300
python3 scripts/prepare-core.py
ROOT=$(pwd)
cd core
UTGARD_TEST_MTU=1300 go test -mod=readonly -modfile "$ROOT/build/core-build.mod" -tags with_gvisor,with_wireguard,with_quic,with_utls \
    -run TestAWGOuterPacketMTU -count=1 -v ./amneziawg
