# Vendored VPN sources

The source trees are ordinary files, not Git submodules. Both VPN engines are
built from these trees by `build-core.sh`; no prebuilt VPN engine is downloaded.

| Tree | Upstream | Revision | License |
| --- | --- | --- | --- |
| `amneziawg-go` | https://github.com/amnezia-vpn/amneziawg-go | `v0.2.19`, `1cc94272ca8e9e223a5fe76382f5880f09d3c12d` | MIT |
| `sing-box` | https://github.com/SagerNet/sing-box | `v1.14.1`, `1ac1a339cb1223e9c70eae14c44411c75033c02d` | GPL-3.0-or-later |
| `parson` | https://github.com/kgabis/parson | 1.5.4 (existing vendored library) | MIT |

Snapshots retain upstream source, documentation, tests and licenses; Git metadata
and GitHub workflows are omitted. `upstream-sha256.json` records the original
snapshot files for reviewing local modifications and future updates (text
line endings normalized to LF before hashing; binary files unchanged).

Local changes:

- `sing-box/go.mod` and `go.sum`: depend on AWG via a local `replace` pointing at
  `../amneziawg-go`; other Go dependencies retain pinned module versions/checksums.
- `sing-box/include/wireguard.go`: register the additional `amneziawg` endpoint.
- `sing-box/protocol/amneziawg/`: Utgard integration, GPL-3.0-or-later.
  `device*.go` adapts sing-box's gVisor device and TCP dialer. The connected UDP
  bind uses sing-box's routed dialer without modifying AWG header bytes.
  The endpoint is a single-peer TCP/UDP client, with userspace IPv4/IPv6 stack.
- `amneziawg-go/device/magic-header.go`: widen arithmetic before calculating
  header range size to avoid uint32 overflow in the full range.
- `amneziawg-go/device/magic_header_utgard_test.go`: regression test for that fix.

Build tags are `with_gvisor,with_quic,with_wireguard,with_utls`. The normal
WireGuard implementation remains separate and unchanged. AmneziaWG v0.2.19
implements AWG 1.x/2.0; its module version is not the wire protocol version.
AWG 3.x parameters and Amnezia `vpn://` container links are not supported.

Transitive Go modules are fetched by Go on the first build and verified with
`go.sum`; the Docker build uses persistent module/compiler caches. Upstream Go
module versions are not auto-upgraded. `scripts/core-licenses.py` collects the
licenses/notices of the modules linked into the Windows executable.

When distributing the GPL core, make this complete corresponding source tree,
the local modifications and build scripts available with the matching binary.
Do not substitute a stock upstream binary: the UI pins the hash of the core
built alongside it. Changing vendor source requires rebuilding the full bundle.
