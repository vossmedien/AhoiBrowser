import copy
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import engine_input_key as eik  # noqa: E402

PATCH = b"patch-bytes\n"
TARGET = "t" * 64


def write(path: pathlib.Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, (dict, list)):
        data = json.dumps(data)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())


def make_repository(root: pathlib.Path, compatible: bool = True) -> None:
    write(root / "config/chromium.json", {"commit": "c" * 40})
    write(root / "config/depot-tools.json", {"commit": "d" * 40})
    write(root / "config/build/ahoi-dev.gn", "is_debug = false\n")
    write(root / "config/build/ahoi-release.gn", "is_official_build = true\n")
    xcode = {"requiredBuild": "17F113"}
    macos = {"chromiumOfficialBuild": "25F70"}
    if compatible:
        xcode["compatibleDevelopment"] = {"build": "27A266a"}
        macos["compatibleDevelopmentBuild"] = "26A425"
    write(root / "config/toolchain.json", {"xcode": xcode, "sdks": {"macOS": macos}})
    write(root / "patches/dependencies/x/0001.patch", PATCH)
    write(root / "config/dependency-build-workarounds.json", {
        "schemaVersion": 2,
        "x": {"id": "x-fix", "patchPath": "patches/dependencies/x/0001.patch",
              "patchSha256": hashlib.sha256(PATCH).hexdigest(), "targetSha256": TARGET},
    })
    write(root / "patches/chromium/series", "0001-a.patch\n")
    write(root / "patches/chromium/0001-a.patch", "diff\n")
    write(root / "overlay/chromium/src/ahoi/a.cc", "int a;\n")


def receipt_for(root: pathlib.Path, **overrides) -> dict:
    components = eik.repository_components(root, "dev")
    receipt = {
        "kind": "ahoi-dev",
        "builtAt": "2026-09-25T00:00:00+00:00",
        "app": {"buildProfile": "dev", "binarySha256": "b" * 64},
        "build": {"gnArgsSha256": components["gnArgsSha256"]},
        "source": {
            "chromiumCommit": components["chromiumCommit"],
            "overlayFingerprint": components["overlayFingerprint"],
            "depotToolsCommit": components["depotToolsCommit"],
            "repositoryCommit": "e" * 40,
            "overlayApplied": True,
            "repositoryDirty": False,
            "dependencyBuildWorkarounds": [{
                "id": "x-fix", "patchSha256": hashlib.sha256(PATCH).hexdigest(),
                "targetSha256": TARGET, "restoredByteForByte": True}],
        },
        "toolchain": {"mode": "compatible-development", "xcodeBuild": "27A266a",
                      "macOSSDKBuild": "26A425"},
    }
    for dotted, value in overrides.items():
        node = receipt
        *parents, leaf = dotted.split(".")
        for name in parents:
            node = node[name]
        node[leaf] = value
    return receipt


class EngineInputKeyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        make_repository(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def key(self):
        return eik.key_of(eik.repository_components(self.root, "dev"))

    def test_receipt_and_repository_agree(self):
        self.assertEqual(eik.key_of(eik.receipt_components(receipt_for(self.root))),
                         self.key())

    def test_every_engine_input_changes_the_key(self):
        before = self.key()
        for path, data in (
                ("overlay/chromium/src/ahoi/a.cc", "int b;\n"),
                ("patches/chromium/0001-a.patch", "diff2\n"),
                ("config/build/ahoi-dev.gn", "is_debug = true\n"),
                ("config/chromium.json", {"commit": "f" * 40})):
            original = (self.root / path).read_bytes()
            write(self.root / path, data)
            self.assertNotEqual(self.key(), before, path)
            write(self.root / path, original)
        self.assertEqual(self.key(), before)

    def test_unrelated_repository_files_do_not_change_the_key(self):
        before = self.key()
        write(self.root / "docs/NOTES.md", "text\n")
        write(self.root / "apps/AhoiMobile/X.swift", "let x = 1\n")
        self.assertEqual(self.key(), before)

    def test_profiles_have_distinct_toolchains(self):
        dev = eik.repository_components(self.root, "dev")["toolchain"]
        release = eik.repository_components(self.root, "release")["toolchain"]
        self.assertEqual(dev["xcodeBuild"], "27A266a")
        self.assertEqual(release["xcodeBuild"], "17F113")

    def test_old_pins_fall_back_to_reference_toolchain(self):
        make_repository(self.root, compatible=False)
        toolchain = eik.repository_components(self.root, "dev")["toolchain"]
        self.assertEqual((toolchain["xcodeBuild"], toolchain["macOSSDKBuild"]),
                         ("17F113", "25F70"))

    def test_tampered_workaround_patch_is_rejected(self):
        write(self.root / "patches/dependencies/x/0001.patch", b"other\n")
        with self.assertRaises(SystemExit):
            eik.repository_components(self.root, "dev")

    def test_non_receipts_are_ignored(self):
        self.assertIsNone(eik.receipt_components({"kind": "ahoi-dev"}))

    def test_lookup_reports_reusable_dirty_and_nearest(self):
        receipts = self.root / "artifacts/build"
        installs = self.root / "artifacts/install"
        write(receipts / "good.json", receipt_for(self.root))
        write(receipts / "dirty.json", receipt_for(self.root, **{"source.repositoryDirty": True}))
        write(receipts / "notes.json", {"kind": "other"})
        write(installs / "i.json", {"bundle": {"executableSha256": "b" * 64}})
        result = eik.lookup(self.root, "dev", receipts, installs)
        self.assertEqual([r["receipt"] for r in result["reusable"]],
                         ["artifacts/build/good.json"])
        self.assertEqual(result["reusable"][0]["installReceipts"], ["artifacts/install/i.json"])
        self.assertEqual(len(result["unusableMatches"]), 1)
        self.assertIsNone(result["nearest"])

        write(self.root / "overlay/chromium/src/ahoi/a.cc", "int changed;\n")
        result = eik.lookup(self.root, "dev", receipts, installs)
        self.assertEqual(result["reusable"], [])
        self.assertEqual(result["nearest"]["differs"], ["overlayFingerprint"])

    def test_workaround_order_does_not_matter(self):
        receipt = receipt_for(self.root)
        swapped = copy.deepcopy(receipt)
        extra = dict(receipt["source"]["dependencyBuildWorkarounds"][0], id="a-first")
        receipt["source"]["dependencyBuildWorkarounds"].append(extra)
        swapped["source"]["dependencyBuildWorkarounds"].insert(0, extra)
        self.assertEqual(eik.key_of(eik.receipt_components(receipt)),
                         eik.key_of(eik.receipt_components(swapped)))


if __name__ == "__main__":
    unittest.main()
