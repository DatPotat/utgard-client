"""Collect license/notice files for Go modules linked into the Windows core."""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
env = dict(os.environ, GOOS="windows", GOARCH="amd64", CGO_ENABLED="0")
data = subprocess.check_output([
    "go", "list", "-mod=readonly", "-modfile", str(root / "build/core-build.mod"), "-deps", "-json", "-overlay", str(root / "build/core-overlay.json"),
    "-tags", "with_gvisor,with_quic,with_wireguard,with_utls", "github.com/sagernet/sing-box/cmd/sing-box",
], cwd=root / "core", env=env, text=True)
decoder = json.JSONDecoder()
modules = {}
while data.strip():
    item, pos = decoder.raw_decode(data.lstrip())
    data = data.lstrip()[pos:]
    module = item.get("Module")
    if module:
        modules[module["Path"]] = module
destination = root / "bin/licenses/core-dependencies"
destination.mkdir(parents=True, exist_ok=True)
manifest = []
for name, module in sorted(modules.items()):
    source = Path(module.get("Replace", module)["Dir"])
    directory = destination / name.replace("/", "_")
    found = []
    for file in source.rglob("*"):
        if not file.is_file():
            continue
        # Include notices in subdirectories too, e.g. assembly and third_party.
        if not file.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "PATENTS", "COPYRIGHT")):
            continue
        relative = file.relative_to(source)
        target = directory / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        found.append(str(relative))
    if not found:
        raise SystemExit(f"Missing license for linked module: {name}")
    manifest.append({"module": name, "version": module.get("Version", "1.14.1-utgard-awg3"),
                     "licenses": found})
# Go's runtime and standard library are also distributed in the core.
goroot = Path(subprocess.check_output(["go", "env", "GOROOT"], text=True).strip())
shutil.copyfile(goroot / "LICENSE", destination / "GO-LICENSE.txt")
(destination / "modules.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"Collected licenses for {len(modules)} core modules and Go")
