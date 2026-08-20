import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH_PATH = ROOT / "patches/chromium/0002-macos-26-posix-spawn-chdir.patch"
SERIES_PATH = ROOT / "patches/chromium/series"


class MacOS26CompatibilityPatchContractTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH_PATH.read_text(encoding="utf-8")

    def test_patch_is_second_and_documented(self):
        entries = [
            line.strip()
            for line in SERIES_PATH.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertGreaterEqual(len(entries), 2)
        self.assertEqual(PATCH_PATH.name, entries[1])

        ledger = (ROOT / "patches/chromium/README.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(f"## `{PATCH_PATH.name}`", ledger)
        self.assertIn("fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae", ledger)

    def test_patch_only_changes_launch_mac_chdir_selection(self):
        touched_paths = re.findall(
            r"^diff --git a/(\S+) b/(\S+)$", self.patch, re.MULTILINE
        )
        self.assertEqual(
            [("base/process/launch_mac.cc", "base/process/launch_mac.cc")],
            touched_paths,
        )
        self.assertIn(
            "#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000", self.patch
        )
        self.assertEqual(
            2,
            self.patch.count("posix_spawn_file_actions_addchdir(&file_actions_"),
        )
        self.assertEqual(
            1,
            self.patch.count(
                "posix_spawn_file_actions_addchdir_np(&file_actions_"
            ),
        )
        self.assertIn("__builtin_available(macOS 26, *)", self.patch)

    def test_patch_does_not_weaken_compiler_or_process_security(self):
        forbidden = (
            "-Wno-deprecated-declarations",
            "-Wno-error",
            "--no-sandbox",
            "disable-site-isolation",
            "DCHECK_IS_ON",
        )
        for marker in forbidden:
            self.assertNotIn(marker, self.patch)


if __name__ == "__main__":
    unittest.main()
