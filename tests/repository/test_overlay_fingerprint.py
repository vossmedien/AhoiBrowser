import pathlib
import tempfile
import unittest


from tools.overlay_fingerprint import fingerprint


class OverlayFingerprintTests(unittest.TestCase):
    def fixture(self, root: pathlib.Path):
        patches = root / "patches/chromium"
        overlay = root / "overlay/chromium/src"
        patches.mkdir(parents=True)
        overlay.mkdir(parents=True)
        (patches / "series").write_text("# empty\n", encoding="utf-8")
        source = overlay / "tool"
        source.write_text("same\n", encoding="utf-8")
        return overlay, source

    def test_executable_mode_is_a_semantic_input(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            _, source = self.fixture(root)
            regular = fingerprint(root)
            source.chmod(0o755)
            executable = fingerprint(root)
            self.assertNotEqual(regular, executable)

    def test_symlink_target_is_a_semantic_input(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            overlay, _ = self.fixture(root)
            (overlay / "target-a").write_text("same\n", encoding="utf-8")
            (overlay / "target-b").write_text("same\n", encoding="utf-8")
            link = overlay / "link"
            link.symlink_to("target-a")
            first = fingerprint(root)
            link.unlink()
            link.symlink_to("target-b")
            second = fingerprint(root)
            self.assertNotEqual(first, second)

    def test_patch_symlinks_fail_closed(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            self.fixture(root)
            patch_root = root / "patches/chromium"
            (patch_root / "real.patch").write_text("patch\n", encoding="utf-8")
            (patch_root / "linked.patch").symlink_to("real.patch")
            with self.assertRaisesRegex(SystemExit, "must not be symlinks"):
                fingerprint(root)


if __name__ == "__main__":
    unittest.main()
