import base64
import json
import os
import pathlib
import plistlib
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests/repository"))

from release import (  # noqa: E402
    chain,
    cli,
    common,
    crypto,
    materials,
    packaging,
    signing,
    sparkle,
)
from release_test_support import (  # noqa: E402
    TemporaryEd25519Key,
    create_release_assets_fixture,
    write_json,
)


class ReleasePrimitiveTests(unittest.TestCase):
    def test_production_key_trust_and_release_pass_ship_closed(self):
        policy = cli._policy()
        gate = json.loads((ROOT / "config/release-evidence.json").read_text())
        self.assertFalse(gate["releasePassEnabled"])
        self.assertEqual([], policy["manifestSigning"]["trustedKeyIds"])
        for channel in ("nightly", "beta", "stable"):
            self.assertEqual("", policy["updates"]["channels"][channel]["feedUrl"])
            self.assertEqual(
                "", policy["updates"]["channels"][channel]["artifactBaseUrl"]
            )
            self.assertEqual(
                "", policy["updates"]["channels"][channel]["publicEdKey"]
            )

    def test_tree_hash_binds_mode_symlink_target_and_bytes(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-tree-test-") as directory:
            root = pathlib.Path(directory) / "AhoiBrowser.app"
            root.mkdir()
            executable = root / "binary"
            executable.write_bytes(b"one")
            executable.chmod(0o755)
            os.symlink("binary", root / "current")
            original = common.tree_sha256(root)
            executable.chmod(0o644)
            self.assertNotEqual(original, common.tree_sha256(root))
            executable.chmod(0o755)
            executable.write_bytes(b"two")
            self.assertNotEqual(original, common.tree_sha256(root))

    def test_ed25519_signature_rejects_payload_tampering(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-signature-test-") as directory:
            root = pathlib.Path(directory)
            keys = TemporaryEd25519Key(root)
            payload = common.canonical_json({"channel": "nightly", "build": 2})
            signature = crypto.signature_object(payload, keys.private, keys.public)
            crypto.verify_signature_object(
                payload, signature, keys.public, {keys.key_id}
            )
            with self.assertRaises(common.ReleaseError):
                crypto.verify_signature_object(
                    payload + b" ", signature, keys.public, {keys.key_id}
                )

    def test_nested_signing_plan_is_leaf_to_root(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-sign-plan-") as directory:
            app = pathlib.Path(directory) / "AhoiBrowser.app"
            helper = app / "Contents/Frameworks/Ahoi.framework/Helpers/Worker.app"
            executable = helper / "Contents/MacOS/Worker"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"Mach-O fixture")
            framework_binary = app / "Contents/Frameworks/Ahoi.framework/Ahoi"
            framework_binary.write_bytes(b"Mach-O fixture")
            plan = signing.signing_plan(app, [executable, framework_binary])
            self.assertEqual(app, plan[-1])
            self.assertLess(plan.index(executable), plan.index(helper))
            self.assertLess(
                plan.index(helper),
                plan.index(app / "Contents/Frameworks/Ahoi.framework"),
            )

    def test_sparkle_nested_code_has_minimum_entitlement_roles(self):
        policy = json.loads((ROOT / "config/macos-entitlements.json").read_text())
        paths = {
            "Contents/Frameworks/Sparkle.framework/Versions/B/Sparkle":
                "sparkle-framework",
            "Contents/Frameworks/Sparkle.framework/Versions/B/Autoupdate":
                "sparkle-autoupdate",
            "Contents/Frameworks/Sparkle.framework/Versions/B/Updater.app/Contents/"
            "MacOS/Updater": "sparkle-updater-app",
            "Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/"
            "Downloader.xpc/Contents/MacOS/Downloader": "sparkle-downloader-xpc",
            "Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/"
            "Installer.xpc/Contents/MacOS/Installer": "sparkle-installer-xpc",
        }
        for path, expected_role in paths.items():
            matches = [
                rule
                for rule in policy["rules"]
                if re.fullmatch(rule["pathPattern"], path)
            ]
            self.assertEqual([expected_role], [rule["id"] for rule in matches])
            self.assertEqual({}, matches[0]["entitlements"])

    def test_signing_refuses_candidate_not_bound_to_build_provenance(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-sign-binding-") as directory:
            root = pathlib.Path(directory)
            app = root / "AhoiBrowser.app"
            executable = app / "Contents/MacOS/AhoiBrowser"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"unsigned fixture")
            plist = {
                "CFBundleName": "AhoiBrowser",
                "CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser",
                "CFBundleExecutable": "AhoiBrowser",
                "CFBundleShortVersionString": "1.2.3",
                "CFBundleVersion": "12",
                "AhoiProductVersion": "1.2.3",
                "AhoiUpdateChannel": "nightly",
                "AhoiSourceCommit": "1" * 40,
                "AhoiChromiumVersion": "151.0.7922.170",
                "AhoiChromiumCommit": "2" * 40,
                "AhoiGNArgsSHA256": "3" * 64,
                "AhoiBuildProfile": "release",
            }
            (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
            provenance = root / "build.json"
            write_json(
                provenance,
                {
                    "schemaVersion": 2,
                    "kind": "ahoi-release",
                    "app": {
                        "bundleSha256": "f" * 64,
                        "binarySha256": common.sha256_file(executable),
                    },
                },
            )
            with self.assertRaisesRegex(common.ReleaseError, "bundle hash"):
                signing.sign_app(
                    app,
                    identity="Developer ID Application: Fixture (ABCDEFGHIJ)",
                    team_id="ABCDEFGHIJ",
                    policy_path=ROOT / "config/macos-entitlements.json",
                    build_provenance_path=provenance,
                    output=root / "signing.json",
                )

    def test_dmg_packager_creates_a_valid_local_image(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-dmg-test-") as directory:
            root = pathlib.Path(directory)
            app = root / "AhoiBrowser.app"
            app.mkdir()
            (app / "fixture").write_text("AhoiBrowser")
            dmg = root / "AhoiBrowser.dmg"
            packaging.create_dmg(app, dmg, "AhoiBrowser Test")
            self.assertGreater(dmg.stat().st_size, 0)
            subprocess.run(
                ["hdiutil", "imageinfo", str(dmg)],
                check=True,
                capture_output=True,
            )


class ReleaseMaterialsTests(unittest.TestCase):
    def test_sbom_and_license_receipt_are_deterministic_and_complete(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-materials-test-") as directory:
            root = pathlib.Path(directory)
            (root / "licenses").mkdir()
            (root / "licenses/Ahoi.txt").write_text("GPL-3.0-or-later\n")
            inventory = root / "release-components.json"
            write_json(
                inventory,
                {
                    "schemaVersion": 1,
                    "components": [
                        {
                            "name": "AhoiBrowser",
                            "version": "1.2.3",
                            "supplier": "Organization: AhoiBrowser",
                            "downloadLocation": "https://example.invalid/source",
                            "licenseConcluded": "GPL-3.0-or-later",
                            "licenseFiles": ["licenses/Ahoi.txt"],
                        }
                    ],
                },
            )
            source_offer = root / "SOURCE-OFFER.txt"
            notices = root / "THIRD-PARTY-NOTICES.txt"
            source_offer.write_text("source offer\n")
            notices.write_text("notices\n")
            first = root / "sbom-first.json"
            second = root / "sbom-second.json"
            first_licenses = root / "licenses-first.zip"
            second_licenses = root / "licenses-second.zip"
            receipt = root / "materials.json"
            arguments = dict(
                source_root=root,
                source_offer=source_offer,
                notices=notices,
                receipt_output=receipt,
                document_namespace="https://releases.example.invalid/1.2.3/sbom",
                created_at="2026-08-25T00:00:00Z",
            )
            materials.create_materials(
                inventory,
                sbom_output=first,
                license_archive_output=first_licenses,
                **arguments,
            )
            materials.create_materials(
                inventory,
                sbom_output=second,
                license_archive_output=second_licenses,
                **arguments,
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first_licenses.read_bytes(), second_licenses.read_bytes())
            self.assertEqual("SPDX-2.3", json.loads(first.read_text())["spdxVersion"])

    def test_unresolved_bundled_license_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-materials-fail-") as directory:
            root = pathlib.Path(directory)
            license_file = root / "LICENSE"
            license_file.write_text("unknown\n")
            inventory = root / "inventory.json"
            write_json(
                inventory,
                {
                    "schemaVersion": 1,
                    "components": [
                        {
                            "name": "Unknown",
                            "version": "1",
                            "supplier": "Person: Fixture",
                            "downloadLocation": "https://example.invalid",
                            "licenseConcluded": "NOASSERTION",
                            "licenseFiles": ["LICENSE"],
                        }
                    ],
                },
            )
            with self.assertRaisesRegex(common.ReleaseError, "unresolved license"):
                materials.create_materials(
                    inventory,
                    source_root=root,
                    source_offer=license_file,
                    notices=license_file,
                    sbom_output=root / "sbom.json",
                    license_archive_output=root / "licenses.zip",
                    receipt_output=root / "receipt.json",
                    document_namespace="https://example.invalid/sbom",
                    created_at="2026-08-25T00:00:00Z",
                )


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


class ReleaseChainTests(unittest.TestCase):
    def make_chain(self, root: pathlib.Path):
        keys = TemporaryEd25519Key(root)
        product = json.loads((ROOT / "config/product.json").read_text())
        version = json.loads((ROOT / "config/version.json").read_text())
        chromium = json.loads((ROOT / "config/chromium.json").read_text())
        toolchain = json.loads((ROOT / "config/toolchain.json").read_text())
        gn_args_sha256 = common.sha256_file(ROOT / "config/build/ahoi-release.gn")
        unsigned_hash = "1" * 64
        signed_tree = "2" * 64
        stapled_tree = "3" * 64
        build = {
            "schemaVersion": 2,
            "kind": "ahoi-release",
            "app": {
                "bundleName": product["name"],
                "bundleIdentifier": product["bundleId"],
                "marketingVersion": version["marketingVersion"],
                "buildNumber": version["buildNumber"],
                "productVersion": version["displayVersion"],
                "channel": version["channel"],
                "sourceCommit": "4" * 40,
                "chromiumVersion": chromium["version"],
                "chromiumCommit": chromium["commit"],
                "gnArgsSha256": gn_args_sha256,
                "buildProfile": "release",
                "bundleSha256": unsigned_hash,
                "binarySha256": "6" * 64,
            },
            "source": {
                "repositoryDirty": False,
                "repositoryCommit": "4" * 40,
                "chromiumCommit": chromium["commit"],
            },
            "build": {
                "gnArgsSha256": gn_args_sha256,
                "generatedGnArgsSha256": gn_args_sha256,
                "gn": {
                    "version": toolchain["buildTools"]["gnVersionOutput"],
                    "binarySha256": toolchain["buildTools"]["gnBinarySha256"],
                },
                "ninja": {
                    "version": toolchain["buildTools"]["ninjaVersionOutput"],
                    "binarySha256": toolchain["buildTools"]["ninjaBinarySha256"],
                },
                "siso": {
                    "enabled": False,
                    "configuredRevision": toolchain["buildTools"]["sisoRevision"],
                },
                "clang": {
                    "version": toolchain["buildTools"]["clangVersionLine"],
                    "package": toolchain["buildTools"]["clangPackage"],
                    "archiveSha256": toolchain["buildTools"][
                        "clangArchiveSha256"
                    ],
                    "binarySha256": toolchain["buildTools"]["clangBinarySha256"],
                },
                "lld": {
                    "version": toolchain["buildTools"]["lldVersionLine"],
                    "binarySha256": toolchain["buildTools"]["lldBinarySha256"],
                    "driver": "ld64.lld -> lld",
                },
            },
            "toolchain": {"mode": "pinned-reference", "pins": toolchain},
        }
        paths = {name: root / name for name in (
            "build.json", "signing.json", "notary.json", "package.json",
            "installed.json", "materials.json", "assets.json",
        )}
        write_json(paths["build.json"], build)
        identity_metadata = {
            "name": product["name"],
            "identifier": product["bundleId"],
            "marketingVersion": version["marketingVersion"],
            "buildNumber": version["buildNumber"],
            "productVersion": version["displayVersion"],
            "channel": version["channel"],
            "sourceCommit": "4" * 40,
            "chromiumVersion": chromium["version"],
            "chromiumCommit": chromium["commit"],
            "gnArgsSha256": gn_args_sha256,
            "buildProfile": "release",
            "executable": "AhoiBrowser",
        }
        unsigned_identity = {
            **identity_metadata,
            "executableSha256": "6" * 64,
            "bundleTreeSha256": "0" * 64,
            "buildProvenanceBundleSha256": unsigned_hash,
        }
        signed_identity = {
            **identity_metadata,
            "executableSha256": "7" * 64,
            "bundleTreeSha256": signed_tree,
        }
        notarized_identity = {
            **signed_identity,
            "bundleTreeSha256": stapled_tree,
        }
        signing_receipt = {
            "schemaVersion": 1,
            "kind": "signed-package-provenance",
            "buildProvenance": {
                "file": "build.json",
                "sha256": common.sha256_file(paths["build.json"]),
            },
            "unsignedBundle": unsigned_identity,
            "signedBundle": signed_identity,
            "signing": {
                "teamIdentifier": "ABCDEFGHIJ",
                "authority": "Developer ID Application: Fixture (ABCDEFGHIJ)",
                "hardenedRuntime": True,
                "trustedTimestamp": True,
                "order": [{"path": ".", "role": "browser-app"}],
            },
            "verification": {
                "deepStrict": True,
                "nestedCode": [{"path": ".", "role": "browser-app"}],
            },
        }
        write_json(paths["signing.json"], signing_receipt)
        submissions = []
        for label in ("app", "dmg"):
            subject = root / f"notary-{label}-subject"
            log = root / f"notary-{label}-response.json"
            subject.write_bytes(label.encode("ascii"))
            write_json(log, {"id": f"fixture-{label}", "status": "Accepted"})
            submissions.append(
                {
                    "subject": subject.name,
                    "subjectSha256": common.sha256_file(subject),
                    "submissionId": f"fixture-{label}",
                    "status": "Accepted",
                    "log": log.name,
                    "logSha256": common.sha256_file(log),
                }
            )
        notary_receipt = {
            "schemaVersion": 1,
            "kind": "notarization-receipt",
            "bundle": {
                "preStapleTreeSha256": signed_tree,
                "postStapleTreeSha256": stapled_tree,
                "identity": notarized_identity,
                "staplerValidated": True,
                "gatekeeperAccepted": True,
            },
            "submissions": submissions,
            "dmg": {
                "preStapleSha256": submissions[1]["subjectSha256"],
                "postStapleSha256": "8" * 64,
                "staplerValidated": True,
            },
        }
        zip_path = root / "AhoiBrowser.zip"
        dmg_path = root / "AhoiBrowser.dmg"
        zip_path.write_bytes(b"zip fixture")
        dmg_path.write_bytes(b"dmg fixture")
        artifacts = {
            "zip": {
                "file": zip_path.name,
                "sha256": common.sha256_file(zip_path),
                "size": zip_path.stat().st_size,
            },
            "dmg": {
                "file": dmg_path.name,
                "sha256": common.sha256_file(dmg_path),
                "size": dmg_path.stat().st_size,
            },
        }
        notary_receipt["dmg"]["postStapleSha256"] = artifacts["dmg"]["sha256"]
        write_json(paths["notary.json"], notary_receipt)
        write_json(
            paths["package.json"],
            {
                "schemaVersion": 1,
                "kind": "package-provenance",
                "stapledBundleTreeSha256": stapled_tree,
                "artifacts": artifacts,
            },
        )
        write_json(
            paths["installed.json"],
            {
                "schemaVersion": 1,
                "kind": "installed-bundle-binding",
                "installPath": "/Applications/AhoiBrowser.app",
                "bundleTreeSha256": stapled_tree,
                "bundle": notarized_identity,
                "signingReceipt": {
                    "file": "signing.json",
                    "sha256": common.sha256_file(paths["signing.json"]),
                },
                "notarizationReceipt": {
                    "file": "notary.json",
                    "sha256": common.sha256_file(paths["notary.json"]),
                },
                "verification": {
                    "identity": True,
                    "nestedCode": True,
                    "hardenedRuntime": True,
                    "gatekeeper": True,
                    "stapling": True,
                },
            },
        )
        sbom = root / "sbom.json"
        notices = root / "THIRD-PARTY-NOTICES.txt"
        source = root / "SOURCE-OFFER.txt"
        inventory = root / "release-components.json"
        license_archive = root / "LICENSES.zip"
        sbom.write_text("{}\n")
        notices.write_text("notices\n")
        source.write_text("source\n")
        write_json(inventory, {"schemaVersion": 1, "components": ["fixture"]})
        license_bytes = b"fixture license\n"
        with zipfile.ZipFile(license_archive, "w") as archive:
            archive.writestr("licenses/fixture.txt", license_bytes)
        write_json(
            paths["materials.json"],
            {
                "schemaVersion": 1,
                "kind": "release-materials",
                "componentInventory": {
                    "file": inventory.name,
                    "sha256": common.sha256_file(inventory),
                },
                "sbom": {"file": sbom.name, "sha256": common.sha256_file(sbom)},
                "licenseArchive": {
                    "file": license_archive.name,
                    "sha256": common.sha256_file(license_archive),
                },
                "thirdPartyNotices": {
                    "file": notices.name,
                    "sha256": common.sha256_file(notices),
                },
                "correspondingSourceOffer": {
                    "file": source.name,
                    "sha256": common.sha256_file(source),
                },
                "licenses": [
                    {
                        "name": "Fixture",
                        "files": [
                            {
                                "file": "licenses/fixture.txt",
                                "sha256": common.sha256_bytes(license_bytes),
                            }
                        ],
                    }
                ],
            },
        )
        create_release_assets_fixture(root, paths)
        output = root / "release-manifest.json"
        manifest = chain.assemble_manifest(
            root=root,
            build_provenance_path=paths["build.json"],
            signing_receipt_path=paths["signing.json"],
            notarization_receipt_path=paths["notary.json"],
            package_receipt_path=paths["package.json"],
            installed_receipt_path=paths["installed.json"],
            materials_receipt_path=paths["materials.json"],
            release_assets_receipt_path=paths["assets.json"],
            product=product,
            version=version,
            chromium=chromium,
            toolchain=toolchain,
            release_args_sha256=gn_args_sha256,
            private_key=keys.private,
            public_key=keys.public,
            trusted_key_ids={keys.key_id},
            output=output,
        )
        return (
            manifest,
            output,
            keys,
            product,
            version,
            chromium,
            toolchain,
            gn_args_sha256,
            zip_path,
        )

    def test_manifest_is_reproducible_signed_and_fully_bound(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-chain-test-") as directory:
            root = pathlib.Path(directory)
            values = self.make_chain(root)
            manifest, output, keys, product, version, chromium, toolchain, gn_hash, _ = values
            self.assertIn("releaseAssetsReceipt", manifest["evidence"])
            first = output.read_bytes()
            chain.assemble_manifest(
                root=root,
                build_provenance_path=root / "build.json",
                signing_receipt_path=root / "signing.json",
                notarization_receipt_path=root / "notary.json",
                package_receipt_path=root / "package.json",
                installed_receipt_path=root / "installed.json",
                materials_receipt_path=root / "materials.json",
                release_assets_receipt_path=root / "assets.json",
                product=product,
                version=version,
                chromium=chromium,
                toolchain=toolchain,
                release_args_sha256=gn_hash,
                private_key=keys.private,
                public_key=keys.public,
                trusted_key_ids={keys.key_id},
                output=output,
            )
            self.assertEqual(first, output.read_bytes())
            chain.validate_manifest(
                output,
                public_key=keys.public,
                trusted_key_ids={keys.key_id},
                product=product,
                version=version,
                chromium=chromium,
                toolchain=toolchain,
                release_args_sha256=gn_hash,
            )
            unsigned = dict(manifest)
            signature = unsigned.pop("signature")
            crypto.verify_signature_object(
                common.canonical_json(unsigned), signature, keys.public, {keys.key_id}
            )

    def test_artifact_tampering_breaks_independent_chain_validation(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-chain-tamper-") as directory:
            root = pathlib.Path(directory)
            values = self.make_chain(root)
            _, output, keys, product, version, chromium, toolchain, gn_hash, zip_path = values
            zip_path.write_bytes(b"tampered")
            with self.assertRaisesRegex(common.ReleaseError, "zip SHA-256 mismatch"):
                chain.validate_manifest(
                    output,
                    public_key=keys.public,
                    trusted_key_ids={keys.key_id},
                    product=product,
                    version=version,
                    chromium=chromium,
                    toolchain=toolchain,
                    release_args_sha256=gn_hash,
                )


if __name__ == "__main__":
    unittest.main()
