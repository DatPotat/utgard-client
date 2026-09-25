#!/bin/sh
# Build pinned sing-box with our adapter and verified AWG; pin its hash in the UI.
set -eu
DIST_DIR=${DIST_DIR:-bin}
export DIST_DIR
mkdir -p build "$DIST_DIR/sing-box" "$DIST_DIR/licenses"
ROOT=$(pwd)
DIST_DIR=$(cd "$DIST_DIR" && pwd)
export DIST_DIR
python3 scripts/prepare-core.py
(
    cd core
    CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -mod=readonly -modfile "$ROOT/build/core-build.mod" -buildvcs=false -trimpath \
        -overlay "$ROOT/build/core-overlay.json" \
        -tags with_gvisor,with_quic,with_wireguard,with_utls \
        -ldflags '-s -w -buildid= -X github.com/sagernet/sing-box/constant.Version=1.14.1-utgard-awg3' \
        -o "$DIST_DIR/sing-box/sing-box.exe" github.com/sagernet/sing-box/cmd/sing-box
)
HASH=$(sha256sum "$DIST_DIR/sing-box/sing-box.exe" | cut -d ' ' -f 1)
printf '#define UTGARD_CORE_SHA256 L"%s"\n' "$HASH" > build/core_hash.h
cp core/LICENSE "$DIST_DIR/sing-box/LICENSE"
cp licenses/SING-BOX-GPL-3.0.txt "$DIST_DIR/licenses/SING-BOX-GPL-3.0.txt"
cp build/amneziawg-source/LICENSE "$DIST_DIR/licenses/AMNEZIAWG-MIT.txt"
python3 scripts/core-licenses.py
echo "Собрано ядро sing-box 1.14.1-utgard-awg3 (SHA-256: $HASH)"
