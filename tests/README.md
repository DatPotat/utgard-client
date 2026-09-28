# Tests

Portable tests of the client's core, built for the host (Linux, MSYS2, or
llvm-mingw on Windows) with AddressSanitizer and UBSan when the compiler can
link them; mingw-w64 gcc cannot, and the tests then run without them.
The compiler is `$CC`, else clang, else gcc. They carry the checks from one
development session to the next.

    sh tests/run.sh
    SINGBOX=/path/to/sing-box sh tests/run.sh   # also runs "sing-box check"

`SINGBOX` must be sing-box 1.14.1 built for the host (on Linux, the official
`sing-box-1.14.1-linux-amd64.tar.gz`).

| test | what it covers |
|---|---|
| `test_link` | wg-quick configs with AmneziaWG (bounds, H overlap, S >= 12 with header protection, keepalive ranges, BOM), `vpn://` in every form the Amnezia client accepts, files, subscriptions; values that are not well-formed UTF-8 refused |
| `test_awgconf` | the config handed to the AmneziaWG service: IPv4 only, Table = off, no DNS or scripts, round trip, refusals |
| `test_vpnswitch` | the order of a profile switch and its rollback, every branch |
| `test_profiles` | profiles.dat version 4, and a version 3 file from before AmneziaWG |
| `test_genconf` | the sing-box config for an AmneziaWG profile (bind_interface, route_exclude_address, process rule, fixed TUN name); the default config; with PAC: DNS inbound rules first, the PAC rule last, overlays before it; without PAC: no PAC trace |
| `test_pac` | select() results in relay loops, a full guard table, PAC decisions (DIRECT on error, shared IPs, IP-only), SOCKS UDP address restore, the DNS recursion guard, the status record, the `pac.json` blob and its SHA-256 |
| `test_lists` | the site list as a rule-set: collapsing, repeats, single labels refused, URLs reduced to hosts, the placeholder for an empty list, tidy with Windows line ends, zapret rules |
| `test_update` | the release tag from GitHub's redirect, version comparison, overflow never "newer" |
| `fuzz_link` | a short mutation run over the parsers |

Fixtures in `fixtures/` use synthetic keys and documentation addresses only.
Windows-only code (services, adapters, permissions, the UI) is not covered
here; it is checked under Wine and on a live system.

`win/` holds Windows-only test programs for the PAC path (the helper process,
the relay, DNS routing through sing-box, the SOCKS address type sing-box
sends). They are not built by `run.sh`: build them with the Windows toolchain
from the repository root and run them by hand with a sing-box 1.14.1
Windows binary.
