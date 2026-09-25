"""Package only build products and explicit source paths, never runtime profiles."""
from pathlib import Path
import tarfile
import zipfile
import json
import os

root = Path(__file__).resolve().parent.parent
dist = root / os.environ.get("DIST_DIR", "bin")
for required in ("utgard.exe", "sing-box/sing-box.exe", "licenses/core-dependencies/modules.json"):
    if not (dist / required).is_file():
        raise SystemExit("Run build.sh first: missing " + required)
source = root / "build/utgard-awg3-source.tar.gz"
def source_filter(info):
    if "__pycache__" in Path(info.name).parts or info.name.endswith(".pyc"):
        return None
    return info

with tarfile.open(source, "w:gz") as archive:
    for name in ("src", "core", "vendor", "res", "licenses", "scripts", "tests", "image",
                 "README.md", "LICENSE", "build.sh", "build-core.sh", "Dockerfile.build", ".gitattributes"):
        archive.add(root / name, arcname="utgard/" + name, filter=source_filter)
    archive.add(root / "sing-box/config.default.json", arcname="utgard/sing-box/config.default.json")
    # Corresponding upstream core source accompanies its binary, but is not
    # vendored into the repository. Go downloaded and verified this module.
    upstream = json.loads((root / "build/core-source.json").read_text())
    if upstream["Version"] != "v1.14.1":
        raise SystemExit("Unexpected sing-box source version")
    archive.add(root / "build/sing-box-source", arcname="utgard/dependencies/sing-box")
    archive.add(root / "build/amneziawg-source", arcname="utgard/dependencies/amneziawg-go")
bundle = root / "build/utgard-awg3-windows-amd64.zip"
with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as archive:
    # Explicit allowlist: a developer may have private profiles/logs in bin/.
    for name in ("utgard.exe", "sing-box/sing-box.exe", "sing-box/LICENSE"):
        archive.write(dist / name, name)
    for file in sorted((dist / "licenses").rglob("*")):
        if file.is_file():
            archive.write(file, file.relative_to(dist))
    archive.write(root / "README.md", "README.md")
    archive.write(source, source.name)
print(bundle)
