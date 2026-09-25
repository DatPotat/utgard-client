"""Package only build products and explicit source paths, never runtime profiles."""
from pathlib import Path
import tarfile
import zipfile

root = Path(__file__).resolve().parent.parent
for required in ("bin/utgard.exe", "bin/sing-box/sing-box.exe", "bin/licenses/core-dependencies/modules.json"):
    if not (root / required).is_file():
        raise SystemExit("Run build.sh first: missing " + required)
source = root / "build/utgard-awg2-source.tar.gz"
with tarfile.open(source, "w:gz") as archive:
    for name in ("src", "vendor", "res", "licenses", "scripts", "tests", "image",
                 "README.md", "LICENSE", "build.sh", "build-core.sh", "Dockerfile.build", ".gitattributes"):
        archive.add(root / name, arcname="utgard/" + name)
    archive.add(root / "sing-box/config.default.json", arcname="utgard/sing-box/config.default.json")
bundle = root / "build/utgard-awg2-windows-amd64.zip"
with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as archive:
    # Explicit allowlist: a developer may have private profiles/logs in bin/.
    for name in ("utgard.exe", "sing-box/sing-box.exe", "sing-box/LICENSE"):
        archive.write(root / "bin" / name, name)
    for file in sorted((root / "bin/licenses").rglob("*")):
        if file.is_file():
            archive.write(file, file.relative_to(root / "bin"))
    archive.write(root / "README.md", "README.md")
    archive.write(source, source.name)
print(bundle)
