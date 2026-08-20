import copy
import json
import pathlib
import plistlib
import unittest


from tools.verify_macos_entitlements import load_policy, parse_entitlements, verify


ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY_PATH = ROOT / "config/macos-entitlements.json"


class MacOSEntitlementPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = load_policy(POLICY_PATH)

    def test_policy_is_bound_to_current_chromium_commit(self):
        pin = json.loads((ROOT / "config/chromium.json").read_text())
        self.assertEqual(pin["commit"], self.policy["chromiumCommit"])

    def test_browser_requires_exact_upstream_entitlements(self):
        expected = self.policy["rules"][0]["entitlements"]
        role = verify(self.policy, "Contents/MacOS/AhoiBrowser", expected)
        self.assertEqual("browser-app", role)

        missing = copy.deepcopy(expected)
        missing.pop("com.apple.security.device.camera")
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/AhoiBrowser", missing)

    def test_jit_is_allowed_only_for_renderer_and_gpu(self):
        renderer = (
            "Contents/Frameworks/AhoiBrowser Framework.framework/Versions/151.0.0/"
            "Helpers/AhoiBrowser Helper (Renderer).app/Contents/MacOS/"
            "AhoiBrowser Helper (Renderer)"
        )
        jit = {"com.apple.security.cs.allow-jit": True}
        self.assertEqual("renderer-helper", verify(self.policy, renderer, jit))

        generic = (
            "Contents/Frameworks/AhoiBrowser Framework.framework/Versions/151.0.0/"
            "Helpers/AhoiBrowser Helper.app/Contents/MacOS/AhoiBrowser Helper"
        )
        with self.assertRaises(SystemExit):
            verify(self.policy, generic, jit)
        self.assertEqual("generic-helper", verify(self.policy, generic, {}))

    def test_risky_or_unknown_entitlements_fail_closed(self):
        actual = {"com.apple.security.cs.disable-library-validation": True}
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/AhoiBrowser", actual)
        with self.assertRaises(SystemExit):
            verify(self.policy, "Contents/MacOS/UnknownHelper", {})

    def test_plist_parser_accepts_empty_and_binary_payloads(self):
        self.assertEqual({}, parse_entitlements(b"\n"))
        payload = {"com.apple.security.cs.allow-jit": True}
        self.assertEqual(
            payload,
            parse_entitlements(plistlib.dumps(payload, fmt=plistlib.FMT_BINARY)),
        )


if __name__ == "__main__":
    unittest.main()
