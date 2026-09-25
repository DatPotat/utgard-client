"""Register the local AWG adapter through a Go build overlay.

sing-box is a pinned Go dependency, not a vendored tree. The module cache is
never modified. The overlay changes only the endpoint registration at build time.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
from core_sources import stage_source, verify_awg

root = Path(__file__).resolve().parent.parent
module = json.loads(subprocess.check_output(
    ["go", "mod", "download", "-json", "github.com/sagernet/sing-box"],
    cwd=root / "core", text=True))
if module.get("Error") or module.get("Version") != "v1.14.1":
    raise SystemExit("Expected sing-box v1.14.1: " + str(module.get("Error", module.get("Version"))))
build = root / "build"
build.mkdir(exist_ok=True)
# Go forbids overlays inside GOMODCACHE. Stage the verified dependency in the
# ignored build directory and use a temporary modfile; never edit core/go.mod.
source = build / "sing-box-source"
lock = json.loads((root / "core/amneziawg.lock.json").read_text(encoding="utf-8"))
awg = json.loads(subprocess.check_output(
    ["go", "mod", "download", "-json", lock["module"] + "@" + lock["version"]],
    cwd=root / "core", text=True))
verify_awg(awg, lock)
# Check cached archive AND extracted sources before staging; download metadata
# alone must not allow a locally modified module-cache directory through.
subprocess.run(["go", "mod", "verify"], cwd=root / "core", check=True)
awg_source = build / "amneziawg-source"
stage_source(module["Dir"], source, build)
stage_source(awg["Dir"], awg_source, build)
range_file = awg_source / "device/noise-types.go"
range_text = range_file.read_text(encoding="utf-8")
original_range = "\treturn lo + fastrandn(hi-lo+1)"
if range_text.count(original_range) != 1:
    raise SystemExit("AWG range implementation changed; review the patch")
range_text = range_text.replace(original_range, (root / "core/patches/uint-range.txt").read_text(encoding="utf-8"))
range_file.write_text(range_text, encoding="utf-8", newline="\n")
shutil.copyfile(root / "core/patches/range_utgard_test.go", awg_source / "device/range_utgard_test.go")
(build / "amneziawg-source.json").write_text(json.dumps(awg, indent=2), encoding="utf-8")
print("Verified AmneziaWG " + lock["version"] + " at " + lock["commit"])

registration = source / "include/wireguard.go"
original = registration.read_text(encoding="utf-8")
if hashlib.sha256(original.encode("utf-8")).hexdigest() != "8a816bf9cd4e841588348f92832a8153fd9f74da9d957358f8766f12b66bed82":
    raise SystemExit("Upstream registration changed; review the AWG overlay")
if original.count('"github.com/sagernet/sing-box/protocol/wireguard"') != 1 or original.count("wireguard.RegisterEndpoint(registry)") != 1:
    raise SystemExit("Unexpected upstream WireGuard registration; review the AWG overlay")
modified = original.replace('"github.com/sagernet/sing-box/protocol/wireguard"',
    '"github.com/sagernet/sing-box/protocol/wireguard"\n "github.com/DatPotat/utgard-client/core/amneziawg"')
modified = modified.replace("wireguard.RegisterEndpoint(registry)",
    "wireguard.RegisterEndpoint(registry)\n amneziawg.RegisterEndpoint(registry)")
modfile = (root / "core/go.mod").read_text(encoding="utf-8")
modfile += "\nreplace " + lock["module"] + " => " + json.dumps(str(awg_source)) + "\n"
modfile += "\nreplace github.com/sagernet/sing-box => " + json.dumps(str(source)) + "\n"
(build / "core-build.mod").write_text(modfile, encoding="utf-8", newline="\n")
shutil.copyfile(root / "core/go.sum", build / "core-build.sum")
replacement = build / "awg-registration.go"
replacement.write_text(modified, encoding="utf-8", newline="\n")
(build / "core-overlay.json").write_text(json.dumps({"Replace": {str(registration): str(replacement)}}), encoding="utf-8")
(build / "core-source.json").write_text(json.dumps(module, indent=2), encoding="utf-8")
print("Prepared AWG registration overlay for sing-box " + module["Version"])
