"""Verify and stage pinned sources without retaining files from earlier builds."""
import shutil

def stage_source(src, target, build):
    # Remove stale files too: merging trees could compile unverified *.go files
    # left by another build. Restrict deletion to our two build directories.
    if (target.resolve().parent != build.resolve() or target.is_symlink() or
            target.name not in {"sing-box-source", "amneziawg-source"}):
        raise SystemExit("Unsafe dependency staging path")
    if target.exists():
        target.chmod(0o755)
        for item in target.rglob("*"):
            if not item.is_symlink(): item.chmod(0o755 if item.is_dir() else 0o644)
        shutil.rmtree(target)
    shutil.copytree(src, target, copy_function=shutil.copyfile)

def verify_awg(awg, lock):
    if (awg.get("Error") or awg.get("Path") != lock["module"] or awg.get("Version") != lock["version"] or
            awg.get("Sum") != lock["sum"] or awg.get("GoModSum") != lock["go_mod_sum"] or
            awg.get("Origin", {}).get("Hash") != lock["commit"]):
        raise SystemExit("AmneziaWG version, commit or checksum mismatch")
