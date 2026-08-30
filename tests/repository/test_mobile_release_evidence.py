import copy
import json
import pathlib
import plistlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import verify_mobile_release_evidence as mobile_evidence  # noqa: E402


COMMIT = "a" * 40
BUILD_ID = "1234567890"


class MobileReleaseEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ahoi-mobile-evidence-")
        self.root = pathlib.Path(self.temporary.name)
        self.manifest_path = self.root / "manifest.json"
        self.files = {}
        for name, content in {
            "build.log": b"BUILD SUCCEEDED\n",
            "archive-inspection.json": b'{"valid":true}\n',
            "upload.json": b'{"uploaded":true}\n',
            "processing.json": b'{"state":"VALID"}\n',
            "public-link.json": b'{"enabled":true}\n',
            "iphone-install.json": b'{"installed":true}\n',
            "ipad-install.json": b'{"installed":true}\n',
            "iphone-journey.json": b'{"passed":true}\n',
            "ipad-journey.json": b'{"passed":true}\n',
            "visible.png": b"PNG fixture",
            "default-request.json": b'{"requested":true}\n',
            "default-grant.json": b'{"granted":true}\n',
        }.items():
            path = self.root / name
            path.write_bytes(content)
            self.files[name] = path
        for name in ("unit.xcresult", "integration.xcresult", "ui.xcresult"):
            path = self.root / name
            path.mkdir()
            (path / "marker").write_text(name, encoding="utf-8")
            self.files[name] = path
        archive = self.root / "AhoiMobile.xcarchive"
        archive.mkdir()
        (archive / "marker").write_text("archive", encoding="utf-8")
        self.files[archive.name] = archive
        self.write_receipts()
        self.manifest = self.full_manifest()
        self.write_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    def reference(self, name):
        return {
            "path": name,
            "sha256": mobile_evidence.sha256_path(self.files[name]),
        }

    def write_receipt(self, name, payload):
        self.files[name].write_text(
            json.dumps(
                {
                    "schemaVersion": 1,
                    "capturedAt": "2026-08-30T12:01:00+00:00",
                    **payload,
                }
            )
            + "\n",
            encoding="utf-8",
        )

    def write_receipts(self):
        identity = self.identity()
        self.write_receipt(
            "archive-inspection.json",
            {
                "kind": "archive-inspection",
                **identity,
                "configuration": "ReleasePostGrant",
                "status": "PASS",
            },
        )
        self.write_receipt(
            "upload.json",
            {
                "kind": "app-store-connect-upload",
                **identity,
                "buildId": BUILD_ID,
                "status": "UPLOADED",
            },
        )
        self.write_receipt(
            "processing.json",
            {
                "kind": "app-store-connect-processing",
                **identity,
                "buildId": BUILD_ID,
                "processingState": "VALID",
            },
        )
        self.write_receipt(
            "public-link.json",
            {
                "kind": "public-testflight-link",
                "buildId": BUILD_ID,
                "enabled": True,
                "betaReviewState": "APPROVED",
                "url": "https://testflight.apple.com/join/Ahoi1234",
            },
        )
        for kind, prefix in (("iPhone", "iphone"), ("iPad", "ipad")):
            device = self.device(kind)
            self.write_receipt(
                f"{prefix}-install.json",
                {
                    "kind": "testflight-installation",
                    **identity,
                    "buildId": BUILD_ID,
                    "device": device,
                    "status": "INSTALLED",
                },
            )
            self.write_receipt(
                f"{prefix}-journey.json",
                {
                    "kind": "testflight-device-journey",
                    **identity,
                    "buildId": BUILD_ID,
                    "device": device,
                    "status": "PASS",
                },
            )
        for state, name in (
            ("REQUESTED", "default-request.json"),
            ("GRANTED", "default-grant.json"),
        ):
            self.write_receipt(
                name,
                {
                    "kind": f"default-browser-{state.lower()}",
                    **identity,
                    "buildId": BUILD_ID,
                    "state": state,
                },
            )

    @staticmethod
    def device(kind, physical=True):
        if kind == "iPad":
            return {
                "identifier": "ipad-device-id",
                "model": "iPad Pro 13-inch",
                "platform": "iPadOS",
                "osVersion": "26.6",
                "osBuild": "23F81",
                "physical": physical,
            }
        if kind == "Simulator":
            return {
                "identifier": "simulator-id",
                "model": "iPhone 17 Pro",
                "platform": "iOS Simulator",
                "osVersion": "26.6",
                "osBuild": "23F81",
                "physical": False,
            }
        return {
            "identifier": "iphone-device-id",
            "model": "iPhone 16 Pro Max",
            "platform": "iOS",
            "osVersion": "26.6",
            "osBuild": "23F81",
            "physical": physical,
        }

    @staticmethod
    def identity():
        return {
            "sourceCommit": COMMIT,
            "bundleId": "app.ahoibrowser.AhoiBrowser",
            "teamId": "248AJ5BN47",
            "marketingVersion": "0.1",
            "buildNumber": "42",
        }

    @staticmethod
    def result_context(expected="The candidate operation succeeds"):
        return {
            "startedAt": "2026-08-30T12:00:00+00:00",
            "completedAt": "2026-08-30T12:01:00+00:00",
            "expectedResult": expected,
            "actualResult": "PASS: independently retained evidence matches",
        }

    def execution(self, kind, device, test_ids, source="TestFlight"):
        result = {
            "kind": kind,
            "status": "PASS",
            **self.identity(),
            **self.result_context(),
            "configuration": "ReleasePostGrant",
            "device": device,
            "installationSource": source,
            "testIds": test_ids,
            "evidence": [self.reference("visible.png")],
        }
        if source == "TestFlight":
            result["appStoreConnectBuildId"] = BUILD_ID
        return result

    def xcresult(self, kind, name):
        return {
            "kind": kind,
            "resultBundle": self.reference(name),
            **self.identity(),
            **self.result_context("The named test suite passes"),
            "configuration": "ReleasePostGrant",
            "device": self.device("iPhone"),
        }

    def installation(self, kind):
        prefix = "ipad" if kind == "iPad" else "iphone"
        return {
            "source": "TestFlight",
            "appStoreConnectBuildId": BUILD_ID,
            **self.identity(),
            **self.result_context("The processed TestFlight build is installed"),
            "device": self.device(kind),
            "installationReceipt": self.reference(f"{prefix}-install.json"),
            "journeyReceipt": self.reference(f"{prefix}-journey.json"),
        }

    def full_manifest(self):
        mobile_ids = [f"MOB-USER-{number:02d}" for number in range(1, 16)]
        ios_ids = [f"IOS-{number:02d}" for number in range(1, 16)]
        return {
            "schemaVersion": 1,
            "createdAt": "2026-08-30T12:00:00+00:00",
            "candidate": {
                "id": "0.1-42-aaaaaaaa",
                **self.identity(),
                "sourceDirty": False,
                "configuration": "ReleasePostGrant",
            },
            "claimedStages": sorted(mobile_evidence.ALLOWED_STAGES),
            "builds": [
                {
                    "kind": "device",
                    "status": "PASS",
                    **self.identity(),
                    **self.result_context("The app builds for the named destination"),
                    "configuration": "ReleasePostGrant",
                    "destination": "generic/platform=iOS",
                    "receipt": self.reference("build.log"),
                }
            ],
            "xcresults": [
                self.xcresult("unit", "unit.xcresult"),
                self.xcresult("integration", "integration.xcresult"),
                self.xcresult("ui", "ui.xcresult"),
            ],
            "executions": [
                self.execution(
                    "simulator-visible",
                    self.device("Simulator"),
                    ["MOB-USER-15"],
                    source="local-simulator",
                ),
                self.execution("device-visible", self.device("iPhone"), ["MOB-USER-01"]),
                self.execution("assisted-e2e", self.device("iPad"), mobile_ids + ios_ids),
                self.execution(
                    "default-browser-e2e", self.device("iPhone"), ["MOB-USER-06"]
                ),
                self.execution(
                    "default-browser-e2e", self.device("iPad"), ["MOB-USER-06"]
                ),
            ],
            "archive": {
                "bundle": self.reference("AhoiMobile.xcarchive"),
                "inspectionReceipt": self.reference("archive-inspection.json"),
                **self.identity(),
                **self.result_context("The archive matches the exact candidate"),
                "configuration": "ReleasePostGrant",
            },
            "appStoreConnect": {
                "buildId": BUILD_ID,
                **self.identity(),
                **self.result_context("App Store Connect processes the uploaded build"),
                "processingState": "VALID",
                "uploadReceipt": self.reference("upload.json"),
                "processingReceipt": self.reference("processing.json"),
                "publicTestFlight": {
                    "enabled": True,
                    "betaReviewState": "APPROVED",
                    "url": "https://testflight.apple.com/join/Ahoi1234",
                    "receipt": self.reference("public-link.json"),
                },
            },
            "testFlightInstallations": [
                self.installation("iPhone"),
                self.installation("iPad"),
            ],
            "defaultBrowser": {
                **self.result_context("Apple grants and the app proves default-browser routing"),
                "requestState": "REQUESTED",
                "requestReceipt": self.reference("default-request.json"),
                "grantState": "GRANTED",
                "grantReceipt": self.reference("default-grant.json"),
            },
        }

    def write_manifest(self):
        self.manifest_path.write_text(
            json.dumps(self.manifest, indent=2) + "\n", encoding="utf-8"
        )

    def archive_inspector(self, _path):
        return {**self.identity(), "configuration": "ReleasePostGrant"}

    def xcresult_inspector(self, _path):
        device = self.device("iPhone")
        return {
            "result": "Passed",
            "passedTests": 8,
            "failedTests": 0,
            "devicesAndConfigurations": [{
                "device": {
                    "deviceId": device["identifier"],
                    "modelName": device["model"],
                    "platform": device["platform"],
                    "osVersion": device["osVersion"],
                    "osBuildNumber": device["osBuild"],
                }
            }],
        }

    def validate(self):
        self.write_manifest()
        return mobile_evidence.validate_manifest(
            self.manifest_path,
            repository_root=self.root,
            state_reader=lambda _root: (COMMIT, False),
            xcresult_inspector=self.xcresult_inspector,
            archive_inspector=self.archive_inspector,
        )

    def test_complete_candidate_chain_passes(self):
        self.assertEqual([], self.validate())

    def test_source_complete_is_progressive_and_needs_no_false_release_receipt(self):
        self.manifest["claimedStages"] = ["SOURCE_COMPLETE"]
        self.manifest["builds"] = []
        self.manifest["xcresults"] = []
        self.manifest["executions"] = []
        self.manifest["archive"] = None
        self.manifest["appStoreConnect"] = None
        self.manifest["testFlightInstallations"] = []
        self.manifest["defaultBrowser"] = None
        self.assertEqual([], self.validate())

    def test_artifact_hash_and_path_traversal_are_rejected(self):
        self.manifest["builds"][0]["receipt"]["sha256"] = "0" * 64
        errors = self.validate()
        self.assertIn("builds[0].receipt.sha256 does not match the local artifact", errors)

        self.manifest = self.full_manifest()
        self.manifest["builds"][0]["receipt"]["path"] = "../build.log"
        errors = self.validate()
        self.assertIn(
            "builds[0].receipt.path must stay inside the candidate directory", errors
        )

    def test_xcresult_device_and_archive_identity_are_cross_checked(self):
        def wrong_device(_path):
            summary = self.xcresult_inspector(_path)
            summary["devicesAndConfigurations"][0]["device"]["osVersion"] = "25.0"
            return summary

        def wrong_archive(_path):
            return {**self.archive_inspector(_path), "sourceCommit": "b" * 40}

        self.write_manifest()
        errors = mobile_evidence.validate_manifest(
            self.manifest_path,
            repository_root=self.root,
            state_reader=lambda _root: (COMMIT, False),
            xcresult_inspector=wrong_device,
            archive_inspector=wrong_archive,
        )
        self.assertIn("xcresults[0].device does not match an xcresult destination", errors)
        self.assertIn("archive binary sourceCommit does not match candidate", errors)

    def test_each_result_requires_timestamp_and_expected_actual_outcome(self):
        del self.manifest["builds"][0]["expectedResult"]
        self.manifest["builds"][0]["completedAt"] = "2026-08-30T11:00:00+00:00"
        errors = self.validate()
        self.assertIn("builds[0].expectedResult is required", errors)
        self.assertIn("builds[0].completedAt must not precede startedAt", errors)

    def test_processing_build_and_public_link_cannot_be_mixed_between_candidates(self):
        self.manifest["appStoreConnect"]["processingState"] = "PROCESSING"
        self.manifest["testFlightInstallations"][0]["appStoreConnectBuildId"] = "foreign"
        self.manifest["appStoreConnect"]["publicTestFlight"]["url"] = (
            "https://example.com/not-testflight"
        )
        errors = self.validate()
        self.assertIn("appStoreConnect.processingState must be VALID", errors)
        self.assertIn(
            "testFlightInstallations[0].appStoreConnectBuildId does not match processed build",
            errors,
        )
        self.assertIn("public TestFlight URL must be an Apple join URL", errors)

    def test_post_grant_claims_require_post_grant_mode_and_apple_receipts(self):
        self.manifest["candidate"]["configuration"] = "TestFlightBootstrap"
        self.manifest["archive"]["configuration"] = "TestFlightBootstrap"
        self.manifest["defaultBrowser"]["grantReceipt"] = None
        errors = self.validate()
        self.assertIn("post-grant stages require ReleasePostGrant", errors)
        self.assertIn("post-grant stage requires Apple's hashed grant receipt", errors)

    def test_release_pass_requires_both_device_families_and_all_journeys(self):
        self.manifest["testFlightInstallations"] = [self.installation("iPhone")]
        self.manifest["executions"] = [
            self.execution(
                "default-browser-e2e", self.device("iPhone"), ["MOB-USER-06"]
            )
        ]
        errors = self.validate()
        self.assertIn("RELEASE_PASS requires TestFlight installs on iPhone and iPad", errors)
        self.assertTrue(any(error.startswith("RELEASE_PASS is missing journeys:") for error in errors))

    def test_archive_inspector_reads_candidate_binding_from_bundle(self):
        archive = self.root / "Real.xcarchive"
        app = archive / "Products/Applications/AhoiMobile.app"
        app.mkdir(parents=True)
        with (archive / "Info.plist").open("wb") as handle:
            plistlib.dump({"ApplicationProperties": {"Team": "248AJ5BN47"}}, handle)
        with (app / "Info.plist").open("wb") as handle:
            plistlib.dump(
                {
                    "CFBundleIdentifier": "app.ahoibrowser.AhoiBrowser",
                    "CFBundleShortVersionString": "0.1",
                    "CFBundleVersion": "42",
                    "AhoiSourceCommit": COMMIT,
                    "AhoiBuildMode": "ReleasePostGrant",
                },
                handle,
            )
        self.assertEqual(
            {**self.identity(), "configuration": "ReleasePostGrant"},
            mobile_evidence.inspect_archive(archive),
        )


if __name__ == "__main__":
    unittest.main()
