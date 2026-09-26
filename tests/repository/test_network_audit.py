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

    def test_symlinked_bundle_launches_the_real_bundle(self):
        # The Mac sandbox crashes a symlinked bundle's first child launch, so
        # the identity (and the launched executable) is the resolved bundle.
        import plistlib
        with tempfile.TemporaryDirectory() as tmp:
            real = pathlib.Path(tmp).resolve() / "Real.app"
            (real / "Contents/MacOS").mkdir(parents=True)
            (real / "Contents/MacOS/Real").write_bytes(b"binary")
            with (real / "Contents/Info.plist").open("wb") as handle:
                plistlib.dump({"CFBundleExecutable": "Real"}, handle)
            link = pathlib.Path(tmp) / "link.app"
            link.symlink_to(real)
            identity = audit.perf.app_identity(link)
            self.assertEqual(identity["path"], str(real))
            self.assertEqual(identity["executable"],
                             str(real / "Contents/MacOS/Real"))

    def test_navigated_hosts_are_foreground_not_unknown(self):
        classified = audit.classify({"example.com": 3, "tracker.example": 1}, {},
                                    frozenset({"example.com"}))
        self.assertEqual([d["host"] for d in classified["navigated"]], ["example.com"])
        self.assertEqual([d["host"] for d in classified["unknown"]], ["tracker.example"])

    def test_crashpad_settings_uploads_flag(self):
        header = b"sdPC" + (1).to_bytes(4, "little")
        self.assertIs(audit.crashpad_uploads_enabled(header + (0).to_bytes(4, "little")),
                      False)
        self.assertIs(audit.crashpad_uploads_enabled(header + (1).to_bytes(4, "little")),
                      True)
        self.assertIsNone(audit.crashpad_uploads_enabled(b"nope"))

    def test_crash_verdict(self):
        before = {"new": [], "pending": [], "completed": ["a.dmp"], "uploads_enabled": False}
        after = {"new": [], "pending": [], "completed": ["a.dmp", "b.dmp"],
                 "uploads_enabled": False}
        ok = audit.crash_verdict(before, after, [], set(), crashed=True)
        self.assertEqual(ok["PRIV-16"], "PASS")
        self.assertEqual(ok["newReports"]["completed"], ["b.dmp"])
        # Uploads disabled: Chrome runs no upload thread, reports stay pending.
        pending = dict(after, pending=["c.dmp"])
        self.assertEqual(audit.crash_verdict(before, pending, [], set(), True)["PRIV-16"],
                         "PASS")
        self.assertEqual(audit.crash_verdict(before, after, ["/cr/report"], set(),
                                             True)["PRIV-16"], "FAIL")
        self.assertEqual(audit.crash_verdict(before, after, [], {"1.2.3.4:443"},
                                             True)["PRIV-16"], "FAIL")
        enabled = dict(after, uploads_enabled=True)
        self.assertEqual(audit.crash_verdict(before, enabled, [], set(), True)["PRIV-16"],
                         "FAIL")
        self.assertEqual(audit.crash_verdict(before, after, [], set(), False)["PRIV-16"],
                         "FAIL")

    def test_crash_upload_marker_in_netlog(self):
        self.assertEqual(audit.crash_upload_requested(
            '{"url":"https://clients2.google.com/cr/report"}')[:1], ["/cr/report"])
        self.assertEqual(audit.crash_upload_requested('{"url":"https://example.com/"}'), [])

    def test_unknown_phase_is_rejected(self):
        with self.assertRaises(SystemExit):
            audit.main(["--app", "/nonexistent.app", "--output", "/tmp/unused",
                        "--phases", "idle,bogus"])


if __name__ == "__main__":
    unittest.main()
