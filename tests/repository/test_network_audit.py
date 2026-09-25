import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/network_audit"))

import fresh_profile_audit as audit  # noqa: E402

NETLOG = json.dumps({"events": [
    {"type": 2, "params": {"url": "https://update.googleapis.com/service/update2/json?x=1"}},
    {"type": 3, "params": {"host": "android.clients.google.com:443"}},
    {"type": 4, "params": {"host": "https://safebrowsing.googleapis.com:443"}},
    {"type": 5, "params": {"host_port": "mtalk.google.com:5228"}},
    {"type": 6, "params": {"group_name": "ssl/clients2.google.com:443"}},
    {"type": 7, "params": {"url": "http://127.0.0.1:8123/page/0"}},
    {"type": 8, "params": {"hostname": "tracker.example."}},
]})


class NetworkAuditTest(unittest.TestCase):
    def test_hosts_from_every_event_kind(self):
        hosts = audit.netlog_hosts(NETLOG)
        for host in ("update.googleapis.com", "android.clients.google.com",
                     "safebrowsing.googleapis.com", "mtalk.google.com",
                     "clients2.google.com", "127.0.0.1", "tracker.example"):
            self.assertIn(host, hosts)

    def test_classification_against_the_real_allowlist(self):
        classified = audit.classify(audit.netlog_hosts(NETLOG), audit.load_allowlist())
        names = {group: {i["host"] for i in items} for group, items in classified.items()}
        self.assertIn("update.googleapis.com", names["allowed"])
        self.assertIn("safebrowsing.googleapis.com", names["allowed"])
        self.assertEqual(names["denied"], {"android.clients.google.com", "mtalk.google.com"})
        self.assertEqual(names["conditional"], {"clients2.google.com"})
        self.assertEqual(names["unknown"], {"tracker.example"})
        self.assertNotIn("127.0.0.1", set().union(*names.values()))

    def test_verdicts(self):
        clean = {"allowed": [], "conditional": [], "denied": [], "unknown": []}
        self.assertEqual(audit.verdict(clean, False),
                         {"NET-GCM-01": "PASS", "fresh-profile-silence": "PASS"})
        self.assertEqual(audit.verdict(clean, True)["NET-GCM-01"], "FAIL")
        gcm = dict(clean, denied=[{"host": "mtalk.google.com", "reason": "N1 GCM MCS"}])
        self.assertEqual(audit.verdict(gcm, None),
                         {"NET-GCM-01": "FAIL", "fresh-profile-silence": "FAIL"})
        other = dict(clean, unknown=[{"host": "x.example"}])
        self.assertEqual(audit.verdict(other, False),
                         {"NET-GCM-01": "PASS", "fresh-profile-silence": "FAIL"})

    def test_gcm_store_marker(self):
        with tempfile.TemporaryDirectory() as tmp:
            profile = pathlib.Path(tmp)
            self.assertIsNone(audit.gcm_checkin_present(profile))
            store = profile / "Default" / "GCM Store"
            store.mkdir(parents=True)
            (store / "000003.log").write_bytes(b"\x00abc")
            self.assertFalse(audit.gcm_checkin_present(profile))
            (store / "000004.log").write_bytes(b"xx" + audit.GCM_CHECKIN_MARKER + b"yy")
            self.assertTrue(audit.gcm_checkin_present(profile))

    def test_installed_app_needs_lease(self):
        self.assertEqual(audit.main(["--app", "/Applications/AhoiBrowser.app",
                                     "--output", "/tmp/unused"]), 7)


if __name__ == "__main__":
    unittest.main()
