import base64
import pathlib
import re
import shutil
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from release import common, sparkle, update_testbed  # noqa: E402


SIGNATURE = base64.b64encode(b"s" * 64).decode("ascii")
BASE = "https://updates.example.invalid/nightly/"


def appcast(items: str) -> bytes:
    prefix = f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="{sparkle.SPARKLE_NAMESPACE}">
  <channel>{items}</channel>
</rss>
""".encode()
    trailer = (
        f"<!-- sparkle-signatures:\nedSignature: {SIGNATURE}\n"
        f"length: {len(prefix)}\n-->\n"
    ).encode()
    return prefix + trailer


def item(build: int, *, channel: str = "nightly", url: str | None = None,
         delta_url: str | None = None, delta_from: int | None = None,
         minimum: int | None = None) -> str:
    url = url or f"{BASE}AhoiBrowser-{build}.zip"
    deltas = ""
    if delta_url is not None:
        deltas = (
            f'<sparkle:deltas><enclosure url="{delta_url}" '
            f'sparkle:deltaFrom="{delta_from}" length="10" '
            f'sparkle:edSignature="{SIGNATURE}"/></sparkle:deltas>'
        )
    minimum_element = (
        f"<sparkle:minimumUpdateVersion>{minimum}</sparkle:minimumUpdateVersion>"
        if minimum is not None else ""
    )
    return (
        f"<item><sparkle:version>{build}</sparkle:version>"
        f"<sparkle:channel>{channel}</sparkle:channel>{minimum_element}"
        f'<enclosure url="{url}" length="10" sparkle:edSignature="{SIGNATURE}"/>'
        f"{deltas}</item>"
    )


class AppcastVersionPolicyTests(unittest.TestCase):
    def validate(self, content: bytes, **overrides):
        arguments = {
            "expected_channel": "nightly",
            "expected_build": 13,
            "expected_artifact_base_url": BASE,
            "published_build_floor": 13,
        }
        arguments.update(overrides)
        with tempfile.TemporaryDirectory(prefix="ahoi-appcast-policy-") as directory:
            path = pathlib.Path(directory) / "appcast.xml"
            path.write_bytes(content)
            return sparkle.validate_appcast_contract(path, **arguments)

    def test_newest_release_with_https_delta_is_accepted(self):
        result = self.validate(appcast(
            item(13, minimum=11, delta_url=f"{BASE}AhoiBrowser13-12.delta",
                 delta_from=12) + item(12, minimum=11)))
        self.assertEqual([13, 12], result["builds"])
        self.assertEqual(1, result["deltaCount"])

    def test_version_regressions_are_rejected(self):
        with self.assertRaisesRegex(common.ReleaseError, "duplicate build"):
            self.validate(appcast(item(13) + item(13, url=f"{BASE}other.zip")))
        with self.assertRaisesRegex(common.ReleaseError, "not the newest"):
            self.validate(appcast(item(13) + item(12)), expected_build=12,
                          published_build_floor=None)
        with self.assertRaisesRegex(common.ReleaseError, "regresses"):
            self.validate(appcast(item(12)), expected_build=None)
        with self.assertRaisesRegex(common.ReleaseError, "minimum update"):
            self.validate(appcast(item(13, minimum=13)))
        with self.assertRaisesRegex(common.ReleaseError, "delta source"):
            self.validate(appcast(item(
                13, delta_url=f"{BASE}AhoiBrowser13-14.delta", delta_from=14)))

    def test_delta_enclosures_follow_the_full_enclosure_rules(self):
        with self.assertRaisesRegex(common.ReleaseError, "HTTPS"):
            self.validate(appcast(item(
                13, delta_url="http://updates.example.invalid/nightly/d.delta",
                delta_from=12)))
        with self.assertRaisesRegex(common.ReleaseError, "artifact base"):
            self.validate(appcast(item(
                13, delta_url="https://elsewhere.example.invalid/d.delta",
                delta_from=12)))

    def test_dot_segments_cannot_escape_the_artifact_base(self):
        for url in (f"{BASE}../evil/Ahoi.zip", f"{BASE}%2e%2e/evil/Ahoi.zip"):
            with self.assertRaisesRegex(common.ReleaseError, "dot path"):
                self.validate(appcast(item(13, url=url)))


class UpdateTestbedTests(unittest.TestCase):
    def test_channel_mirror_matches_the_native_updater(self):
        source = (
            ROOT / "overlay/chromium/src/ahoi/browser/updater/update_channel.cc"
        ).read_text()
        body = source[source.index("AllowedSparkleChannels"):]
        native = {
            name: frozenset(re.findall(r'"(\w+)"', returned))
            for name, returned in re.findall(
                r"case UpdateChannel::k(\w+):\s*return \{([^}]*)\};", body)
        }
        self.assertEqual(
            update_testbed.ALLOWED_SPARKLE_CHANNELS,
            {name.lower(): value for name, value in native.items()},
        )

    def test_selection_never_offers_older_or_foreign_channel_items(self):
        feed = appcast(item(13) + item(12, channel="beta") + item(11, channel=""))
        self.assertEqual(13, update_testbed.select_update(feed, 12, "nightly"))
        self.assertIsNone(update_testbed.select_update(feed, 13, "nightly"))
        self.assertIsNone(update_testbed.select_update(feed, 12, "beta"))
        self.assertEqual(12, update_testbed.select_update(feed, 10, "beta"))
        self.assertEqual(11, update_testbed.select_update(feed, 10, "stable"))

    def test_tools_are_never_run_with_a_keychain_account(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-testbed-guard-") as directory:
            tools = update_testbed.SparkleTools(
                pathlib.Path(directory), pathlib.Path(directory) / "home")
            with self.assertRaisesRegex(common.ReleaseError, "Keychain"):
                tools._run("sign_update", "--account", "ed25519", "feed.xml")
            with self.assertRaisesRegex(common.ReleaseError, "Keychain"):
                tools._run("sign_update", "--verify", "feed.xml")
            self.assertEqual([], tools.commands)

    def test_feed_signature_requires_length_bound_final_trailer(self):
        feed = appcast(item(13))
        prefix, signature = update_testbed.feed_signature(feed)
        self.assertEqual(SIGNATURE, signature)
        self.assertTrue(feed.startswith(prefix))
        self.assertIsNone(update_testbed.feed_signature(feed + b"<item/>"))
        self.assertIsNone(update_testbed.feed_signature(b"x" + feed))

    @unittest.skipUnless(
        shutil.which("openssl") and shutil.which("ditto")
        and all(
            (update_testbed.default_sparkle_tools() / name).is_file()
            for name in sparkle.OFFICIAL_TOOL_NAMES
        ),
        "fetched official Sparkle 2.9.6 tools are not available",
    )
    def test_official_sparkle_tools_accept_genuine_and_reject_tampering(self):
        tools = update_testbed.default_sparkle_tools()
        with tempfile.TemporaryDirectory(prefix="ahoi-update-testbed-") as directory:
            report = update_testbed.run_testbed(tools, pathlib.Path(directory))
        failed = [check for check in report["checks"] if check["result"] != "PASS"]
        self.assertEqual([], failed)
        groups = {check["group"] for check in report["checks"]}
        self.assertEqual(
            {"a-genuine", "b-tampered-archive", "c-wrong-signature",
             "d-downgrade", "e-http-enclosure", "f-feed-weakening"},
            groups,
        )
        self.assertEqual([13, 12], report["appcast"]["builds"])
        self.assertEqual(1, report["appcast"]["deltaCount"])
        for command in report["commands"]:
            self.assertIn("--ed-key-file", command)
            self.assertNotIn("--account", command)
            self.assertTrue(
                all("://" not in part or ".invalid/" in part for part in command))


if __name__ == "__main__":
    unittest.main()
