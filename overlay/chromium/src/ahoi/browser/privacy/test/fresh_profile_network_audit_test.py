# Copyright 2026 The AhoiBrowser Authors
# SPDX-License-Identifier: GPL-3.0-or-later

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from ahoi.browser.privacy.test import fresh_profile_network_audit as audit


class FreshProfileNetworkAuditTest(unittest.TestCase):
    def setUp(self):
        self.policy = audit.load_policy(audit.DEFAULT_POLICY, {})

    def test_policy_is_versioned_default_deny_without_release_endpoint(self):
        self.assertEqual("ahoi-fresh-profile-network-v1", self.policy["policy_id"])
        self.assertEqual(64, len(self.policy["sha256"]))
        self.assertNotIn(
            "sparkle-appcast", {item["id"] for item in self.policy["endpoints"]}
        )
        for endpoint in self.policy["endpoints"]:
            self.assertTrue(audit.REVIEW_FIELDS.issubset(endpoint))
            for field in audit.REVIEW_FIELDS:
                self.assertTrue(endpoint[field])

    def test_plaintext_component_download_is_not_allowlisted(self):
        result = audit.audit_urls(
            ["http://edgedl.me.gvt1.com/edgedl/component.crx"], self.policy
        )
        self.assertFalse(result["passed"])
        self.assertEqual("http://edgedl.me.gvt1.com:80", result["unknown"][0]["origin"])

        secure = audit.audit_urls(
            ["https://edgedl.me.gvt1.com/edgedl/component.crx"], self.policy
        )
        self.assertTrue(secure["passed"])
        self.assertEqual(
            "chromium-component-download-edge",
            secure["allowed"][0]["endpoint_id"],
        )

    def test_known_endpoint_passes_and_unknown_endpoint_fails_redacted(self):
        result = audit.audit_urls(
            [
                "https://update.googleapis.com/service/update2/json?secret=value",
                "https://tracker.invalid/collect?token=do-not-print",
            ],
            self.policy,
        )
        self.assertFalse(result["passed"])
        self.assertEqual("chromium-component-update-check", result["allowed"][0]["endpoint_id"])
        serialized = json.dumps(result)
        self.assertNotIn("secret", serialized)
        self.assertNotIn("token", serialized)
        self.assertEqual("https://tracker.invalid:443", result["unknown"][0]["origin"])

    def test_prohibited_endpoint_is_reported_even_if_future_allowlist_expands(self):
        result = audit.audit_urls(
            ["https://clientservices.googleapis.com/chrome-variations/seed"],
            self.policy,
        )
        self.assertFalse(result["passed"])
        self.assertEqual("variations-seed", result["prohibited"][0]["endpoint_id"])

    def test_release_feed_requires_clean_https_url(self):
        with self.assertRaises(audit.PolicyError):
            audit.load_policy(
                audit.DEFAULT_POLICY,
                {"AHOI_SPARKLE_FEED_URL": "http://updates.example.test/appcast.xml"},
            )
        policy = audit.load_policy(
            audit.DEFAULT_POLICY,
            {"AHOI_SPARKLE_FEED_URL": "https://updates.example.test/appcast.xml"},
        )
        self.assertIn(
            "sparkle-appcast", {item["id"] for item in policy["endpoints"]}
        )

    def test_netlog_extraction_ignores_non_url_values(self):
        netlog = {
            "events": [
                {"params": {"url": "https://safebrowsing.googleapis.com/v5/hashListUpdates:batchGet"}},
                {"params": {"headers": ["Cookie: secret"]}},
                {"params": "not-a-dictionary"},
            ]
        }
        result = audit.audit_urls(audit.extract_urls(netlog), self.policy)
        self.assertTrue(result["passed"])
        self.assertEqual("safe-browsing-v5", result["allowed"][0]["endpoint_id"])

    def test_capture_succeeds_only_after_full_window_and_stops_browser(self):
        process = mock.Mock()
        process.wait.side_effect = [
            subprocess.TimeoutExpired("AhoiBrowser", 5.0),
            0,
        ]
        process.poll.return_value = None
        with mock.patch.object(audit.subprocess, "Popen", return_value=process):
            return_code = audit.capture_fresh_profile(
                Path("/fake/AhoiBrowser"), Path("/fake/netlog.json"), 5.0, None
            )

        self.assertEqual(0, return_code)
        process.terminate.assert_called_once_with()
        process.kill.assert_not_called()

    def test_capture_reports_early_browser_exit_including_clean_exit(self):
        for browser_return_code, expected in ((23, 23), (0, 1)):
            with self.subTest(browser_return_code=browser_return_code):
                process = mock.Mock()
                process.wait.return_value = browser_return_code
                process.poll.return_value = browser_return_code
                with mock.patch.object(
                    audit.subprocess, "Popen", return_value=process
                ):
                    return_code = audit.capture_fresh_profile(
                        Path("/fake/AhoiBrowser"),
                        Path("/fake/netlog.json"),
                        5.0,
                        None,
                    )

                self.assertEqual(expected, return_code)
                process.terminate.assert_not_called()

    def test_main_rejects_capture_return_code_with_redacted_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            browser = root / "AhoiBrowser"
            browser.touch()
            output = root / "result.json"
            arguments = [
                "fresh_profile_network_audit.py",
                "--browser",
                str(browser),
                "--output",
                str(output),
            ]
            with mock.patch.object(sys, "argv", arguments), mock.patch.object(
                audit, "capture_fresh_profile", return_value=23
            ):
                return_code = audit.main()

            self.assertEqual(2, return_code)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertFalse(result["passed"])
            self.assertEqual("browser_exited_early", result["failure"]["code"])
            self.assertEqual(23, result["failure"]["capture_return_code"])

    def test_missing_empty_and_invalid_netlogs_fail_with_safe_codes(self):
        cases = (
            ("missing.json", None, "net_log_missing"),
            ("empty.json", b" \n\t", "net_log_empty"),
            (
                "invalid.json",
                b'{"events": [{"secret": "do-not-print"}]} trailing-secret',
                "net_log_invalid_json",
            ),
            (
                "wrong-shape.json",
                b'{"events": "do-not-print"}',
                "net_log_invalid_structure",
            ),
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for filename, contents, expected_code in cases:
                with self.subTest(filename=filename):
                    net_log = root / filename
                    if contents is not None:
                        net_log.write_bytes(contents)
                    output = root / f"{filename}.result.json"
                    arguments = [
                        "fresh_profile_network_audit.py",
                        "--net-log",
                        str(net_log),
                        "--output",
                        str(output),
                    ]
                    stderr = io.StringIO()
                    with mock.patch.object(
                        sys, "argv", arguments
                    ), contextlib.redirect_stderr(stderr):
                        return_code = audit.main()

                    self.assertEqual(2, return_code)
                    serialized = output.read_text(encoding="utf-8")
                    result = json.loads(serialized)
                    self.assertFalse(result["passed"])
                    self.assertEqual(expected_code, result["failure"]["code"])
                    self.assertNotIn("do-not-print", serialized)
                    self.assertNotIn("trailing-secret", serialized)
                    self.assertNotIn("do-not-print", stderr.getvalue())
                    self.assertNotIn("trailing-secret", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
