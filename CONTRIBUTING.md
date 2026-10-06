# Contributing to Utgard

## Security issues

Do not report vulnerabilities in public issues or pull requests. Follow [SECURITY.md](SECURITY.md).

## License of contributions

Utgard is licensed under the [MIT License](licenses/UTGARD-MIT.txt). By contributing, you agree that your contribution is licensed under the same license.

## Developer Certificate of Origin (DCO)

Every commit must be signed off. The sign-off certifies that you wrote the change or otherwise have the right to submit it under the project's license, as stated in the [Developer Certificate of Origin 1.1](https://developercertificate.org/).

Add the sign-off when committing:

```
git commit -s -m "Short description of the change"
```

This appends a line with the name and email from your git configuration:

```
Signed-off-by: Your Name <your.email@example.com>
```

To add a missing sign-off to your last commit, run `git commit --amend -s`. A sign-off (`-s`) is not the same as a cryptographic commit signature (`-S`).

## Before opening a pull request

- Build: `sh build.sh` (see `README.md`, section «Сборка из исходников»).
- Test: `sh tests/run.sh`. All tests must pass; the generated sing-box configs must not change unless the change is intended (see `tests/README.md`).
- No absolute paths in code, scripts or configs.
- Never commit real server addresses, keys, passwords or subscription links, including in tests and issue descriptions. Use documentation addresses (RFC 5737, RFC 3849, `example.com`) and synthetic keys.

## Third-party code

Only code under a license compatible with MIT may be added. When you add or update a third-party component, update `licenses/THIRD-PARTY-NOTICES.txt` and the SBOM in the same pull request.

## Writing about Utgard

In the README, release notes, issue and pull request titles, the repository description and any other public text, describe Utgard in neutral, technical terms: a client for selective routing of traffic through user-supplied servers. Describe what a feature does, not what it can be used to get around. Do not promote or advertise the project.
