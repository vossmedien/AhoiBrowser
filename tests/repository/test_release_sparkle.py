import base64
import json
import pathlib
import plistlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests/repository"))

from release import common, materials, sparkle  # noqa: E402
from release_test_support import write_json  # noqa: E402


class SparkleUpdateSecurityTests(unittest.TestCase):
    @staticmethod
    def appcast(channel="nightly", url="https://updates.example.invalid/Ahoi.zip"):
        signature = base64.b64encode(b"s" * 64).decode("ascii")
        channel_element = (
            f"<sparkle:channel>{channel}</sparkle:channel>" if channel else ""
        )
        prefix = f'''<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel><item><title>AhoiBrowser</title><sparkle:version>12</sparkle:version>
    {channel_element}
    <enclosure url="{url}" length="1234" type="application/octet-stream"
      sparkle:edSignature="{signature}" />
  </item></channel>
</rss>
'''.encode()
        trailer = f'''<!-- sparkle-signatures:
edSignature: {signature}
length: {len(prefix)}
-->
'''.encode()
        return prefix + trailer

    def test_reviewed_sparkle_pin_includes_security_fix_and_artifact_hash(self):
        pin = sparkle.validate_pin(ROOT / "config/third-party-pins.json")
        self.assertEqual("2.9.6", pin["version"])
        self.assertEqual(sparkle.PINNED_COMMIT, pin["commit"])
        self.assertEqual(sparkle.PINNED_ARCHIVE_SHA256, pin["archive"]["sha256"])
        self.assertEqual(sparkle.PINNED_ARCHIVE_SIZE, pin["archive"]["size"])
        self.assertEqual(sparkle.PINNED_LICENSE_SHA256, pin["licenseSha256"])
        self.assertTrue((ROOT / pin["licenseFile"]).is_file())

    def test_vulnerable_or_unreviewed_sparkle_pin_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-sparkle-pin-") as directory:
            path = pathlib.Path(directory) / "pin.json"
            pin = json.loads((ROOT / "config/third-party-pins.json").read_text())
            pin["dependencies"]["sparkle"]["version"] = "2.9.5"
            write_json(path, pin)
            with self.assertRaisesRegex(common.ReleaseError, "security baseline"):
                sparkle.validate_pin(path)

    def test_fetched_material_receipt_detects_framework_or_tool_tampering(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-sparkle-material-") as directory:
            root = pathlib.Path(directory)
            framework = root / "Sparkle.framework"
            (framework / "Resources").mkdir(parents=True)
            (framework / "Resources/Info.plist").write_bytes(
                plistlib.dumps({"CFBundleShortVersionString": "2.9.6"})
            )
            tools = root / "bin"
            tools.mkdir()
            for name in sparkle.OFFICIAL_TOOL_NAMES:
                (tools / name).write_bytes(f"official-{name}".encode())
            license_path = root / "LICENSE"
            license_path.write_text("MIT fixture")
            receipt = root / "receipt.json"
            sparkle.create_material_receipt(
                ROOT / "config/third-party-pins.json",
                framework,
                tools,
                license_path,
                receipt,
            )
            sparkle.validate_material_receipt(
                ROOT / "config/third-party-pins.json",
                framework,
                tools,
                license_path,
                receipt,
            )
            (tools / "sign_update").write_bytes(b"tampered")
            with self.assertRaisesRegex(common.ReleaseError, "differs"):
                sparkle.validate_material_receipt(
                    ROOT / "config/third-party-pins.json",
                    framework,
                    tools,
                    license_path,
                    receipt,
                )

    def test_materials_receipt_requires_exact_sparkle_sbom_component(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-sparkle-sbom-") as directory:
            root = pathlib.Path(directory)
            inventory = root / "release-components.json"
            write_json(
                inventory,
                {
                    "schemaVersion": 1,
                    "components": [
                        {
                            "name": "Sparkle",
                            "version": "2.9.6",
                            "supplier": "Organization: Sparkle Project",
                            "downloadLocation": sparkle.PINNED_RELEASE_URL,
                            "licenseConcluded": "MIT",
                            "licenseFiles": [
                                "overlay/chromium/src/third_party/sparkle/LICENSE"
                            ],
                        }
                    ],
                },
            )
            source_offer = root / "SOURCE-OFFER.txt"
            notices = root / "THIRD-PARTY-NOTICES.txt"
            source_offer.write_text("source offer")
            notices.write_text("Sparkle notices")
            receipt = root / "materials.json"
            materials.create_materials(
                inventory,
                source_root=ROOT,
                source_offer=source_offer,
                notices=notices,
                sbom_output=root / "sbom.json",
                license_archive_output=root / "licenses.zip",
                receipt_output=receipt,
                document_namespace="https://example.invalid/ahoi/sbom",
                created_at="2026-08-25T00:00:00Z",
            )
            sparkle.validate_sparkle_materials_receipt(
                receipt, ROOT / "config/third-party-pins.json"
            )
            write_json(inventory, {"schemaVersion": 1, "components": []})
            with self.assertRaisesRegex(common.ReleaseError, "hash"):
                sparkle.validate_sparkle_materials_receipt(
                    receipt, ROOT / "config/third-party-pins.json"
                )

    def test_signed_https_appcast_contract_accepts_expected_channel(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-appcast-test-") as directory:
            path = pathlib.Path(directory) / "appcast.xml"
            path.write_bytes(
                self.appcast(
                    url="https://updates.example.invalid/releases/Ahoi.zip"
                )
            )
            result = sparkle.validate_appcast_contract(
                path,
                expected_channel="nightly",
                expected_build=12,
                expected_artifact_base_url=(
                    "https://updates.example.invalid/releases/"
                ),
            )
            self.assertEqual([12], result["builds"])

    def test_appcast_rejects_transport_channel_and_feed_signature_weakening(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-appcast-fail-") as directory:
            path = pathlib.Path(directory) / "appcast.xml"
            path.write_bytes(
                self.appcast(url="http://updates.example.invalid/Ahoi.zip")
            )
            with self.assertRaisesRegex(common.ReleaseError, "HTTPS"):
                sparkle.validate_appcast_contract(path, expected_channel="nightly")

            path.write_bytes(self.appcast(channel="future"))
            with self.assertRaisesRegex(common.ReleaseError, "foreign channel"):
                sparkle.validate_appcast_contract(path, expected_channel="nightly")

            path.write_bytes(self.appcast().split(b"<!-- sparkle-signatures:")[0])
            with self.assertRaisesRegex(common.ReleaseError, "signed-feed"):
                sparkle.validate_appcast_contract(path, expected_channel="nightly")

            path.write_bytes(self.appcast() + b"<unsigned-item />")
            with self.assertRaisesRegex(common.ReleaseError, "final content"):
                sparkle.validate_appcast_contract(path, expected_channel="nightly")


if __name__ == "__main__":
    unittest.main()
