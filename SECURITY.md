# Security Policy

## Supported versions

Only the latest release of Utgard receives security fixes. Earlier releases are not patched; if you are on an older version, update to the [latest release](https://github.com/DatPotat/utgard-client/releases/latest) before reporting.

| Version | Supported |
| --- | --- |
| Latest release | Yes |
| Any earlier release | No |

## Reporting a vulnerability

Please report vulnerabilities privately. Do not open a public issue, discussion or pull request for a security problem.

Use GitHub private vulnerability reporting: open the [Security tab](https://github.com/DatPotat/utgard-client/security) of this repository, go to **Advisories** and click **Report a vulnerability**. The report is visible only to the maintainers.

A useful report contains:

- the affected Utgard version and the Windows version and architecture (x64 or arm64);
- the component involved (for example: link or subscription parsing, PAC, the sing-box or AmneziaWG integration, the update check, the build pipeline);
- steps to reproduce, and a proof of concept if you have one;
- the impact you expect (what an attacker gains, and from which position);
- whether you know of the issue being exploited.

Reports may be written in English or Russian. Do not include real server addresses, keys or subscription links; use placeholders.

## What to expect

- **Acknowledgement:** within 48 hours of the report.
- **Assessment:** we confirm or reject the issue, agree on its severity with you, and keep you informed of progress until it is resolved.
- **Fix:** released in a new version of Utgard. Issues that are being actively exploited are handled first.
- **Disclosure:** coordinated with you. The default is to publish a GitHub Security Advisory when the fix is released, and no later than 90 days after the report. The date can be moved by mutual agreement, for example when a fix needs more time or a third-party component has to be fixed first.
- **Credit:** reporters are credited in the advisory unless they ask to stay anonymous.

There is no bug bounty.

## Scope

In scope:

- the code in this repository;
- the release build (`.github/workflows/`) and the published release archives;
- how Utgard downloads, verifies and runs third-party cores, stores profiles and PAC files, and exposes local listeners.

Out of scope, please report upstream:

- vulnerabilities in sing-box itself: [SagerNet/sing-box](https://github.com/SagerNet/sing-box);
- vulnerabilities in AmneziaWG or Wintun themselves: [amnezia-vpn/amneziawg-windows-client](https://github.com/amnezia-vpn/amneziawg-windows-client), [wintun.net](https://www.wintun.net/);
- vulnerabilities in zapret, which Utgard only controls.

If a flaw in a third-party core can be reached or made worse through the way Utgard uses it, report it here as well.

Also out of scope: attacks that require an attacker who already has administrator rights on the machine, or physical access to an unlocked session.

## Verifying a release

Releases built by GitHub Actions list the SHA-256 of each archive in `SHA256SUMS`, and each of their archives has a build provenance attestation. With the [GitHub CLI](https://cli.github.com/) you can check that an archive was built from this repository:

```
gh attestation verify utgard-client-<version>-windows-<arch>.zip --repo DatPotat/utgard-client
```
