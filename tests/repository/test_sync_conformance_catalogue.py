import contextlib
import copy
import io
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/sync_conformance"))
import generate_field_catalogue as generator


class GeneratedFieldCatalogueTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        (self.root / "config").mkdir()
        self.contract = {"records": [
            {"id": 1, "dataClass": "workspace", "wireModelVersion": 3,
             "fieldGroups": ["name", "tombstone"]},
            {"id": 2, "dataClass": "treeNode", "wireModelVersion": 3,
             "fieldGroups": ["location", "title", "tombstone"]},
        ]}
        self.save()

    def save(self):
        (self.root / "config/sync-format.json").write_text(json.dumps(self.contract))

    def test_repository_catalogue_is_current(self):
        self.assertEqual(generator.main(["--check"]), 0)

    def test_canonical_entity_order_and_each_declared_field_reach_both_languages(self):
        before = generator.render(self.root)
        self.contract["records"].reverse()
        self.save()
        self.assertEqual(generator.render(self.root), before)
        cpp = before[generator.DIRECTORY / "sync_field_catalogue.h"]
        swift = before[generator.DIRECTORY / "SyncFieldCatalogue.swift"]
        self.assertIn('{1, "workspace", 3, kEntity1Fields, 2}', cpp)
        self.assertIn('Entry(entityType: 2, dataClass: "treeNode"', swift)
        for key in ("location", "title", "name", "tombstone"):
            self.assertIn(json.dumps(key), cpp)
            self.assertIn(json.dumps(key), swift)

    def test_contract_addition_and_generated_file_edits_break_freshness(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(generator.main(["--repository", str(self.root)]), 0)
        self.assertEqual(generator.main(["--repository", str(self.root), "--check"]), 0)
        self.contract["records"][0]["fieldGroups"].append("icon")
        self.save()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(generator.main(["--repository", str(self.root), "--check"]), 1)
        with contextlib.redirect_stdout(io.StringIO()):
            generator.main(["--repository", str(self.root)])
        swift = self.root / generator.DIRECTORY / "SyncFieldCatalogue.swift"
        swift.write_text(swift.read_text().replace('"icon"', '"untracked"'))
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(generator.main(["--repository", str(self.root), "--check"]), 1)

    def test_manifest_tracks_contract_types_and_complete_field_lists(self):
        data = generator.render(self.root)[generator.DIRECTORY / "field_catalogue.json"]
        self.assertEqual(json.loads(data)["records"], generator.records(self.root))

    def test_invalid_ids_names_versions_and_duplicate_fields_are_rejected(self):
        original = copy.deepcopy(self.contract)
        changes = [("id", True), ("id", 2147483648), ("wireModelVersion", 0), ("dataClass", "unsafe-name"),
                   ("fieldGroups", ["name", "name"]), ("fieldGroups", [])]
        for key, value in changes:
            self.contract = copy.deepcopy(original)
            self.contract["records"][0][key] = value
            self.save()
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                generator.render(self.root)
        self.contract = copy.deepcopy(original)
        self.contract["records"].append(copy.deepcopy(self.contract["records"][0]))
        self.save()
        with self.assertRaises(ValueError):
            generator.render(self.root)


if __name__ == "__main__":
    unittest.main()
