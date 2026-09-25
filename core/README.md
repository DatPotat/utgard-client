# AmneziaWG integration

`amneziawg/` is the local sing-box endpoint adapter. Both upstream engines are
remote Go dependencies, not vendor trees. AmneziaWG is pinned to
`v3.1.20260828`, commit `b5928efb6ca19f0153958460c3d141f04abc5c2e`.
`amneziawg.lock.json` pins its Go module SHA-256 checksum and go.mod checksum.
The preparation script checks version, commit and both checksums before staging
source in ignored `build/amneziawg-source/`. A mismatch aborts the build.
`go.sum` additionally enables Go's normal checksum verification.

The minimal `patches/uint-range.txt` patch avoids uint32 overflow when sampling
the full range; its regression test is copied into the staged source. No
upstream source is stored in this repository. The adapter supports a single-peer
userspace client, IPv4/IPv6 TCP and UDP, and AWG 1.x/2.0/3.1 parameters.
Keepalive ranges are stored in the existing profile parameter block and moved
to the peer section when generating UAPI; existing profile files remain readable.

sing-box is **not vendored**. `go.mod` pins `github.com/sagernet/sing-box v1.14.1`
(upstream commit `1ac1a339cb1223e9c70eae14c44411c75033c02d`) and `go.sum` verifies
the downloaded source. `scripts/prepare-core.py` stages this dependency under
the ignored `build/sing-box-source/` directory. It generates a temporary modfile
and Go build overlay registering our endpoint beside ordinary WireGuard. The
upstream registration file's hash is checked before applying the overlay.
The module cache is never modified. `build-core.sh` then builds the upstream CLI
with this adapter and embeds its hash in the C client as before.

The overlay replaces only `include/wireguard.go` during compilation. It does not
replace the normal WireGuard transport or alter upstream routing. Build tags:
`with_gvisor,with_quic,with_wireguard,with_utls`.

`device*.go` adapts the gVisor device/TCP dialer from sing-box v1.14.1; the adapter
is GPL-3.0-or-later (see LICENSE). `endpoint.go`, `bind.go` and `parameters.go`
provide lifecycle, routing-aware UDP sockets, validation and AWG configuration.
The UDP bind preserves AWG headers and allows IP fragmentation by default,
matching the upstream AWG bind. Explicit `udp_fragment=false` remains supported.
Send errors include packet length, not contents or key material.

`tests/test.sh` checks C import/storage/generation and Go encrypted traffic.
`tests/mtu.sh`, run inside Docker with NET_ADMIN, reproduces physical MTU 1300
versus inner MTU 1280 plus AWG overhead, using the production dialer. It verifies
both successful packet reassembly and rejection when fragmentation is disabled.

The packaged source archive includes this integration and the verified upstream
dependencies under `dependencies/sing-box/` and `dependencies/amneziawg-go/`; these sources are staged from verified Go modules during the build,
then included in the release archive. They are not kept in the vendor tree.
The complete corresponding source and licenses accompany the distributed core.
