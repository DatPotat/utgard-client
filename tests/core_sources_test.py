import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from core_sources import stage_source, verify_awg


class SourcesTest(unittest.TestCase):
    def test_lock_rejects_changed_identity_or_checksums(self):
        lock = json.loads((Path(__file__).resolve().parents[1] / "core/amneziawg.lock.json").read_text())
        metadata = dict(Path=lock["module"], Version=lock["version"], Sum=lock["sum"],
                        GoModSum=lock["go_mod_sum"], Origin={"Hash": lock["commit"]})
        verify_awg(metadata, lock)
        for field in ("Path", "Version", "Sum", "GoModSum", "Origin"):
            with self.subTest(field=field), self.assertRaises(SystemExit):
                verify_awg(dict(metadata, **{field: {} if field == "Origin" else "changed"}), lock)

    def test_staging_removes_stale_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            src, build = root / "module", root / "build"
            src.mkdir()
            build.mkdir()
            (src / "valid.go").write_text("verified")
            target = build / "amneziawg-source"
            stage_source(src, target, build)
            (target / "stale.go").write_text("must never compile")
            (target / "valid.go").write_text("modified")
            stage_source(src, target, build)
            self.assertEqual((target / "valid.go").read_text(), "verified")
            self.assertFalse((target / "stale.go").exists())
            with self.assertRaises(SystemExit):
                stage_source(src, root / "amneziawg-source", build)
            with self.assertRaises(SystemExit):
                stage_source(src, build / "unrelated", build)


if __name__ == "__main__":
    unittest.main()
