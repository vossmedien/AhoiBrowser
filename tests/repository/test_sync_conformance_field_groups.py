import json
import pathlib
import shutil
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))

import field_groups  # noqa: E402

SOURCES = ["config/sync-format.json",
           f"{field_groups.SYNC}/sync_model.h",
           f"{field_groups.SYNC}/sync_field_merge.cc"] + sorted({
               path for declarations in field_groups.SWIFT_DECLARATIONS.values()
               for path, _ in declarations})


class FieldGroupDriftTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        for relative in SOURCES:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)

    def tearDown(self):
        self.tmp.cleanup()

    def edit(self, relative, old, new):
        path = self.root / relative
        text = path.read_text()
        self.assertEqual(text.count(old), 1, old)
        path.write_text(text.replace(old, new))

    def test_repository_has_no_drift(self):
        findings, checked = field_groups.check(ROOT)
        self.assertEqual([f.render() for f in findings], [])
        self.assertGreaterEqual(checked, 37)

    def test_every_contract_entity_is_mapped_in_swift(self):
        ids = {record["id"] for record in json.loads(
            (ROOT / "config/sync-format.json").read_text())["records"]}
        self.assertEqual(ids, set(field_groups.SWIFT_DECLARATIONS))

    def test_contract_change_without_code_change_is_drift(self):
        path = self.root / "config/sync-format.json"
        contract = json.loads(path.read_text())
        tree = next(r for r in contract["records"] if r["dataClass"] == "treeNode")
        tree["fieldGroups"].insert(-1, "pinned")
        path.write_text(json.dumps(contract))
        sources = {f.source for f in field_groups.check(self.root)[0]}
        self.assertIn("C++ FieldNames", sources)
        self.assertTrue(any("CompanionFieldMerge.swift" in s for s in sources))

    def test_cpp_only_change_is_drift(self):
        self.edit(f"{field_groups.SYNC}/sync_field_merge.cc",
                  '"setting_id", "value_json", "tombstone"',
                  '"setting_id", "value_json", "scope", "tombstone"')
        findings = field_groups.check(self.root)[0]
        self.assertEqual([(f.entity, f.source) for f in findings],
                         [("8 permittedSetting", "C++ FieldNames")])

    def test_swift_only_change_is_drift(self):
        self.edit(f"{field_groups.SPIKE}/WorkspaceStructureRecords.swift",
                  '["snapshot", "state", "tombstone"]', '["snapshot", "tombstone"]')
        findings = field_groups.check(self.root)[0]
        self.assertEqual(len(findings), 1)
        self.assertIn("missing ['state']", findings[0].detail)


if __name__ == "__main__":
    unittest.main()
