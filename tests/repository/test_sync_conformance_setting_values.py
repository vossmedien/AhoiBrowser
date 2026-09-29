import json
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import generate_setting_value_vectors as gen  # noqa: E402

CPP = ROOT / "overlay/chromium/src/ahoi/browser/sync/browser_setting_catalog.cc"
CPP_HEADER = ROOT / "overlay/chromium/src/ahoi/browser/sync/browser_setting_catalog.h"
SWIFT = ROOT / "apps/AhoiMobile/Sources/AhoiMobileCore/CompanionBrowserSettingCatalog.swift"
HANDOFF = ROOT / "handoffs/crest-hardening/144-setting-value-conformance"


def cpp_ids():
    source = CPP.read_text()
    ids = set(re.findall(r'\{"([^"]+)",\s*"[a-z]+",\s*Type::', source))
    if "{kBrowserSearchEngineSettingId," in source:
        ids.add(re.search(r'kBrowserSearchEngineSettingId\[\] =\s*"([^"]+)"',
                          CPP_HEADER.read_text()).group(1))
    return ids


def swift_ids():
    source = SWIFT.read_text()
    ids = set(re.findall(r'^\s*"([^"]+)": \.[a-zA-Z]+,', source, re.M))
    if re.search(r"^\s*searchEngineSettingID: \.searchEngine,", source, re.M):
        ids.add(re.search(r'searchEngineSettingID = "([^"]+)"', source).group(1))
    return ids


class SettingValueVectorsTest(unittest.TestCase):
    def document(self):
        return json.loads(gen.OUTPUT.read_text())

    def test_generated_file_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)

    def test_catalogue_matches_both_platform_sources(self):
        catalogue = set(self.document()["catalogueIds"])
        self.assertEqual(len(catalogue), 24)
        self.assertEqual(cpp_ids(), catalogue)
        self.assertEqual(swift_ids(), catalogue)

    def test_every_catalogue_id_has_reset_and_a_rejection_or_rule(self):
        cases = self.document()["cases"]
        for setting in self.document()["catalogueIds"]:
            with self.subTest(setting=setting):
                own = [c for c in cases if c["settingId"] == setting]
                self.assertIn(("null", True), {(c["valueJson"], c["valid"]) for c in own})
                self.assertTrue(any(not c["valid"] for c in own) or
                                setting == "ahoi.appearance.glass_enabled")

    def test_value_json_is_rfc_json_except_named_parse_case(self):
        for case in self.document()["cases"]:
            if case["reason"] == "not JSON":
                with self.assertRaises(json.JSONDecodeError):
                    json.loads(case["valueJson"])
                self.assertFalse(case["valid"])
            else:
                json.loads(case["valueJson"])

    def test_owner_testdata_mirror_and_patch(self):
        mirror = HANDOFF / "files/overlay/chromium/src/ahoi/browser/sync/testdata/setting_values_v3.json"
        self.assertEqual(gen.OUTPUT.read_bytes(), mirror.read_bytes())
        patch = (HANDOFF / "144-setting-value-conformance.patch").read_text()
        self.assertIn("ValidateBrowserSettingValue", patch)
        self.assertIn("CompanionBrowserSettingCatalog.validatesValue", patch)
        self.assertIn('"testdata/setting_values_v3.json"', patch)


if __name__ == "__main__":
    unittest.main()
