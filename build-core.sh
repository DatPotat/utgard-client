#!/bin/sh
# Build our pinned, vendored sing-box + AmneziaWG, then bind its hash into the UI.
set -eu
mkdir -p build bin/sing-box bin/licenses
ROOT=$(pwd)
(
    cd vendor/sing-box
    CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -mod=readonly -buildvcs=false -trimpath \
        -tags with_gvisor,with_quic,with_wireguard,with_utls \
        -ldflags '-s -w -buildid= -X github.com/sagernet/sing-box/constant.Version=1.14.1-utgard-awg2' \
        -o "$ROOT/bin/sing-box/sing-box.exe" ./cmd/sing-box
)
HASH=$(sha256sum bin/sing-box/sing-box.exe | cut -d ' ' -f 1)
printf '#define UTGARD_CORE_SHA256 L"%s"\n' "$HASH" > build/core_hash.h
cp vendor/sing-box/LICENSE bin/sing-box/LICENSE
cp vendor/sing-box/LICENSE bin/licenses/SING-BOX-GPL-3.0.txt
cp vendor/amneziawg-go/LICENSE bin/licenses/AMNEZIAWG-MIT.txt
python3 scripts/core-licenses.py
echo "Собрано ядро sing-box 1.14.1-utgard-awg2 (SHA-256: $HASH)"
