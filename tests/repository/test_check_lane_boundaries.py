import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_lane_boundaries as lanes  # noqa: E402

CONFIG = json.loads((ROOT / "config/agent-lanes.json").read_text())


def commit(lane, *paths, deletions=None):
    return lanes.Commit("0123456789abcdef", lane, list(paths), deletions or {})


def errors(*commits):
    return [f for f in lanes.check_commits(CONFIG, list(commits)) if f.level == "ERROR"]


class LaneBoundaryTest(unittest.TestCase):
    def test_glob_semantics(self):
        self.assertTrue(lanes.matches("tools/perf/a/b.py", ["tools/perf/**"]))
        self.assertFalse(lanes.matches("tools/perfx.py", ["tools/perf/**"]))
        self.assertTrue(lanes.matches("tests/repository/test_perf_x.py",
                                      ["tests/repository/test_perf_*.py"]))
        self.assertFalse(lanes.matches("a/b/c.md", ["a/*.md"]))

    def test_crest_lane_inside_allowlist_passes(self):
        self.assertEqual(errors(commit("crest-hardening", "tools/perf/run.py",
                                       "handoffs/crest-hardening/001-x/patch.diff")), [])

    def test_crest_lane_cannot_touch_overlay(self):
        found = errors(commit("crest-hardening",
                              "overlay/chromium/src/ahoi/browser/sync/new_file.cc"))
        self.assertEqual(len(found), 1)
        self.assertIn("forbidden", found[0].reason)

    def test_crest_lane_outside_allowlist_fails(self):
        self.assertEqual(len(errors(commit("crest-hardening", "docs/SYNC.md"))), 1)

    def test_pointer_insertion_is_note_but_deletion_is_error(self):
        path = "outputs/AhoiBrowser-Master-Zielprompt.md"
        self.assertEqual(errors(commit("crest-hardening", path, deletions={path: 0})), [])
        self.assertEqual(len(errors(commit("crest-hardening", path, deletions={path: 2}))), 1)

    def test_other_lanes_cannot_touch_exclusive_paths(self):
        self.assertEqual(len(errors(commit("desktop", "tools/engine_input_key.py"))), 1)
        self.assertEqual(len(errors(commit(None, "config/agent-lanes.json"))), 1)

    def test_owner_may_update_handoff_status(self):
        self.assertEqual(errors(commit("desktop",
                                       "handoffs/crest-hardening/001-x/HANDOFF.md")), [])

    def test_unrelated_commits_pass(self):
        self.assertEqual(errors(commit("mobile", "apps/AhoiMobile/Sources/X.swift"),
                                commit(None, "overlay/chromium/src/ahoi/browser/x.cc")), [])


if __name__ == "__main__":
    unittest.main()
