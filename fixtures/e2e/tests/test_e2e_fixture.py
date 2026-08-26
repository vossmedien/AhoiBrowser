#!/usr/bin/env python3
"""Self-tests for the local HTTPS E2E fixture; never modify system trust."""

from __future__ import annotations

import contextlib
import hashlib
import http.client
import io
import json
import ssl
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Mapping, Optional, Sequence, Tuple
from urllib.parse import urlsplit


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

import certificates  # noqa: E402
import manage  # noqa: E402
import receipts  # noqa: E402
import server  # noqa: E402


class CertificateLifecycleTests(unittest.TestCase):
    def test_ca_signs_leaf_with_all_loopback_names_and_private_permissions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-certs-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            self.assertEqual(64, len(str(manifest["caSha256"])))
            self.assertEqual(64, len(str(manifest["leafSha256"])))
            self.assertNotEqual(manifest["caSha256"], manifest["leafSha256"])
            self.assertEqual(list(certificates.HOST_NAMES), manifest["hostNames"])
            self.assertEqual(0o600, Path(str(manifest["caPrivateKey"])).stat().st_mode & 0o777)
            self.assertEqual(0o600, Path(str(manifest["leafPrivateKey"])).stat().st_mode & 0o777)
            verification = subprocess.run(
                [
                    "openssl",
                    "verify",
                    "-CAfile",
                    str(manifest["caCertificate"]),
                    str(manifest["leafCertificate"]),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(0, verification.returncode, verification.stderr)
            leaf_text = subprocess.run(
                ["openssl", "x509", "-in", str(manifest["leafCertificate"]), "-noout", "-text"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            for hostname in certificates.HOST_NAMES:
                self.assertIn(hostname, leaf_text)
            self.assertIsNone(certificates.read_trust_receipt(directory))

    def test_trust_requires_exact_consent_and_records_exact_removal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-trust-") as temporary:
            directory = Path(temporary)
            manifest = certificates.generate(directory)
            keychain = directory / "isolated-test.keychain-db"
            commands = []

            def runner(command, **_kwargs):
                commands.append(list(command))
                if command[0] == "openssl" and "-fingerprint" in command:
                    algorithm = command[-1]
                    value = manifest["caSha256"] if algorithm == "-sha256" else manifest["caSha1"]
                    return subprocess.CompletedProcess(command, 0, "SHA=%s\n" % value, "")
                if command[:2] == ["security", "find-certificate"]:
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        "SHA-256 hash: %s\n" % manifest["caSha256"].upper(),
                        "",
                    )
                return subprocess.CompletedProcess(command, 0, "", "")

            with self.assertRaisesRegex(certificates.CertificateError, "exact confirmation"):
                certificates.install_trust(
                    directory,
                    confirmation="yes",
                    keychain=keychain,
                    runner=runner,
                )
            self.assertEqual([], commands)
            receipt = certificates.install_trust(
                directory,
                confirmation=certificates.TRUST_CONFIRMATION,
                keychain=keychain,
                runner=runner,
            )
            self.assertTrue(receipt["explicitConsent"])
            add = [command for command in commands if command[:2] == ["security", "add-trusted-cert"]]
            self.assertEqual(1, len(add))
            self.assertNotIn(str(manifest["caPrivateKey"]), add[0])
            self.assertTrue(certificates.trust_installation_is_valid(directory, runner=runner))
            verify = [command for command in commands if command[:2] == ["security", "verify-cert"]]
            self.assertEqual(1, len(verify))
            self.assertIn("first-party.localhost", verify[0])
            with self.assertRaisesRegex(certificates.CertificateError, "exact confirmation"):
                certificates.remove_trust(directory, confirmation="yes", runner=runner)
            self.assertIsNotNone(certificates.read_trust_receipt(directory))
            self.assertTrue(
                certificates.remove_trust(
                    directory,
                    confirmation=certificates.REMOVE_CONFIRMATION,
                    runner=runner,
                )
            )
            delete = [command for command in commands if command[:2] == ["security", "delete-certificate"]]
            self.assertEqual(1, len(delete))
            self.assertIn("-t", delete[0])
            self.assertIn(str(manifest["caSha256"]), delete[0])
            self.assertIsNone(certificates.read_trust_receipt(directory))

    def test_certificate_cleanup_refuses_while_trust_receipt_exists(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-cleanup-") as temporary:
            directory = Path(temporary)
            certificates.generate(directory)
            (directory / certificates.TRUST_RECEIPT).write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(certificates.CertificateError, "trusted CA"):
                certificates.remove_certificate_material(directory)

    def test_cli_start_refuses_without_explicit_trust_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-cli-") as temporary:
            directory = Path(temporary)
            certificates.generate(directory)
            args = manage.build_parser().parse_args(["start", "--state-dir", str(directory)])
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                self.assertEqual(1, args.function(args))
            self.assertIn("explicit trust-install receipt", stderr.getvalue())
            self.assertIsNone(manage._read_state(directory))


class HTTPSClusterTests(unittest.TestCase):
    temporary: tempfile.TemporaryDirectory[str]
    directory: Path
    manifest: Mapping[str, object]
    cluster: server.FixtureCluster
    tls: ssl.SSLContext

    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory(prefix="ahoi-e2e-server-")
        cls.directory = Path(cls.temporary.name)
        cls.manifest = certificates.generate(cls.directory)
        cls.cluster = server.FixtureCluster(
            runtime_directory=cls.directory,
            leaf_certificate=Path(str(cls.manifest["leafCertificate"])),
            leaf_private_key=Path(str(cls.manifest["leafPrivateKey"])),
        ).start()
        cls.tls = ssl.create_default_context(cafile=str(cls.manifest["caCertificate"]))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.cluster.stop()
        cls.temporary.cleanup()

    def setUp(self) -> None:
        self.cluster.context.receipts.reset()

    def request(
        self,
        url: str,
        *,
        method: str = "GET",
        body: Optional[bytes] = None,
        headers: Optional[Mapping[str, str]] = None,
    ) -> Tuple[int, Sequence[Tuple[str, str]], bytes]:
        split = urlsplit(url)
        connection = http.client.HTTPSConnection(
            split.hostname,
            split.port,
            timeout=5,
            context=self.tls,
        )
        target = split.path or "/"
        if split.query:
            target += "?" + split.query
        try:
            connection.request(method, target, body=body, headers=dict(headers or {}))
            response = connection.getresponse()
            payload = response.read()
            return response.status, response.getheaders(), payload
        finally:
            connection.close()

    def json_request(self, url: str, **kwargs) -> Tuple[int, Sequence[Tuple[str, str]], object]:
        status, headers, body = self.request(url, **kwargs)
        return status, headers, json.loads(body)

    def test_three_hostnames_validate_against_generated_ca_without_tls_bypass(self) -> None:
        for key in ("firstPartyHttpsUrl", "thirdPartyHttpsUrl", "mediaHttpsUrl"):
            with self.subTest(origin=key):
                status, _headers, body = self.request(
                    self.cluster.context.urls[key] + "/__fixture/health"
                )
                self.assertEqual(200, status)
                self.assertTrue(json.loads(body)["ready"])
        rejecting = ssl.create_default_context()
        split = urlsplit(self.cluster.context.urls["firstPartyHttpsUrl"])
        connection = http.client.HTTPSConnection(split.hostname, split.port, timeout=5, context=rejecting)
        with self.assertRaises(ssl.SSLError):
            connection.request("GET", "/__fixture/health")
        connection.close()

    def test_manifest_covers_all_required_journeys_and_reproducible_payloads(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        status, _headers, manifest = self.json_request(first + "/__fixture/manifest")
        self.assertEqual(200, status)
        self.assertTrue(manifest["loopbackOnly"])
        self.assertTrue(manifest["tlsValidationMustRemainEnabled"])
        self.assertEqual(server.DOWNLOAD_SHA256, manifest["payloads"]["rangeDownload"]["sha256"])
        self.assertEqual(server.MEDIA_SHA256, manifest["payloads"]["h264AacMp4"]["sha256"])
        expected = {
            "downloadUploadDnD",
            "threePaneSplitDnD",
            "redirectPopup",
            "syntheticOAuth",
            "simulatedPasskey",
            "mediaMsePip",
            "webrtcAndCapture",
            "permissions",
            "cookiesChipsPrivacy",
            "storageCacheServiceWorker",
            "headersCspCors",
            "developerInjection",
            "syntheticLogin",
        }
        self.assertEqual(expected, set(manifest["journeys"]))
        self.assertIn("ASSISTED", manifest["boundaries"]["passkey"])

    def test_range_pause_resume_reassembles_exact_published_hash(self) -> None:
        url = self.cluster.context.urls["firstPartyHttpsUrl"] + "/download/deterministic.bin"
        first_status, first_headers, first = self.request(url, headers={"Range": "bytes=0-65535"})
        second_status, second_headers, second = self.request(url, headers={"Range": "bytes=65536-"})
        self.assertEqual((206, 206), (first_status, second_status))
        self.assertIn(("Content-Range", "bytes 0-65535/%d" % len(server.DOWNLOAD_BYTES)), first_headers)
        self.assertIn(("Accept-Ranges", "bytes"), second_headers)
        self.assertEqual(server.DOWNLOAD_SHA256, hashlib.sha256(first + second).hexdigest())
        invalid, invalid_headers, _body = self.request(url, headers={"Range": "bytes=999999999-"})
        self.assertEqual(416, invalid)
        self.assertIn(("Content-Range", "bytes */%d" % len(server.DOWNLOAD_BYTES)), invalid_headers)

    def test_upload_hashes_raw_bytes_without_storing_content(self) -> None:
        body = b"synthetic upload\x00with bytes"
        status, _headers, result = self.json_request(
            self.cluster.context.urls["firstPartyHttpsUrl"] + "/upload",
            method="POST",
            body=body,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Ahoi-Filename": "../../fixture?.bin",
            },
        )
        self.assertEqual(200, status)
        self.assertEqual(hashlib.sha256(body).hexdigest(), result["sha256"])
        self.assertEqual("fixture_.bin", result["filename"])
        self.assertFalse(result["stored"])
        receipt_bytes = self.cluster.context.receipts.path.read_bytes()
        self.assertNotIn(body, receipt_bytes)
        self.assertNotIn(b"fixture_.bin", receipt_bytes)

    def test_pages_expose_split_media_capture_permissions_storage_and_injection_controls(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        media = self.cluster.context.urls["mediaHttpsUrl"]
        expectations = {
            first + "/download-upload": (b"upload-drop", b"warning-download"),
            first + "/split": (b"three-pane", b"text/uri-list"),
            first + "/navigation": (b"open-popup", b"cross-redirect"),
            first + "/webrtc": (b"getUserMedia", b"getDisplayMedia", b"RTCPeerConnection"),
            first + "/permissions": (b"geolocation", b"Notification", b"clipboard"),
            first + "/privacy": (b"third-party-cookie", b"CHIPS"),
            first + "/storage": (b"localStorage", b"sessionStorage", b"indexedDB", b"caches", b"serviceWorker"),
            first + "/injection": (b"LESS", b"SASS", b"apply-js"),
            first + "/login": (b"fixture-user", b"current-password"),
            media + "/media": (b"requestPictureInPicture", b"MediaSource", b"avc1.42c00c", b"mp4a.40.2"),
        }
        for url, markers in expectations.items():
            with self.subTest(url=url):
                status, _headers, body = self.request(url)
                self.assertEqual(200, status)
                for marker in markers:
                    self.assertIn(marker, body)

    def test_media_is_valid_deterministic_mp4_with_range_support(self) -> None:
        url = self.cluster.context.urls["mediaHttpsUrl"] + "/media/sample.mp4"
        status, headers, body = self.request(url)
        self.assertEqual(200, status)
        self.assertIn(b"ftypiso", body[:64])
        self.assertIn(b"moof", body)
        self.assertIn(b"avc1", body)
        self.assertIn(b"mp4a", body)
        self.assertEqual(server.MEDIA_SHA256, hashlib.sha256(body).hexdigest())
        self.assertIn(("Accept-Ranges", "bytes"), headers)
        range_status, range_headers, part = self.request(url, headers={"Range": "bytes=0-31"})
        self.assertEqual(206, range_status)
        self.assertEqual(body[:32], part)
        self.assertIn(("Content-Range", "bytes 0-31/%d" % len(body)), range_headers)

    def test_synthetic_oauth_and_passkey_never_claim_real_platform_ceremonies(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        status, _headers, oauth = self.request(first + "/oauth/authorize?client_id=ahoi&state=visible-test")
        self.assertEqual(200, status)
        self.assertIn(b"Local OAuth simulation only", oauth)
        status, _headers, challenge = self.json_request(first + "/passkey/challenge?kind=register")
        self.assertEqual(200, status)
        self.assertTrue(challenge["simulated"])
        self.assertFalse(challenge["platformWebAuthnPerformed"])
        payload = json.dumps(
            {
                "kind": "register",
                "challengeId": challenge["challengeId"],
                "credentialId": "synthetic-local-credential",
            }
        ).encode()
        status, _headers, verified = self.json_request(
            first + "/passkey/verify",
            method="POST",
            body=payload,
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(200, status)
        self.assertTrue(verified["accepted"])
        self.assertFalse(verified["platformWebAuthnPerformed"])

    def test_cookie_controls_publish_first_party_and_partitioned_attributes(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        third = self.cluster.context.urls["thirdPartyHttpsUrl"]
        status, headers, _body = self.request(first + "/cookies/set")
        self.assertEqual(200, status)
        cookies = [value for name, value in headers if name.lower() == "set-cookie"]
        self.assertEqual(3, len(cookies))
        self.assertTrue(any("HttpOnly" in value for value in cookies))
        self.assertTrue(all("Secure" in value for value in cookies))
        status, headers, _body = self.request(third + "/cookies/third-party")
        self.assertEqual(200, status)
        partitioned = [value for name, value in headers if name.lower() == "set-cookie"]
        self.assertEqual(1, len(partitioned))
        self.assertIn("SameSite=None; Partitioned", partitioned[0])

    def test_receipts_are_machine_readable_and_do_not_retain_secret_values(self) -> None:
        secret = "must-not-be-persisted-8472"
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        status, _headers, echo = self.json_request(
            first + "/privacy/echo?utm_source=" + secret + "&ordinary=" + secret,
            headers={
                "Authorization": "Bearer " + secret,
                "Cookie": "session=" + secret,
                "Sec-GPC": "1",
                "Referer": first + "/source?token=" + secret,
            },
        )
        self.assertEqual(200, status)
        self.assertTrue(echo["gpc"])
        self.assertEqual(["utm_source"], echo["trackingParameterKeys"])
        serialized = self.cluster.context.receipts.path.read_text(encoding="utf-8")
        self.assertNotIn(secret, serialized)
        entry = json.loads(serialized.splitlines()[-1])
        self.assertEqual(self.cluster.context.instance_id, entry["fixtureRunId"])
        self.assertEqual(["ordinary", "utm_source"], entry["queryKeys"])
        self.assertTrue(entry["requestSignals"]["authorizationPresent"])
        self.assertTrue(entry["requestSignals"]["cookiePresent"])
        self.assertTrue(entry["requestSignals"]["gpc"])
        status, _headers, readback = self.json_request(first + "/__fixture/receipts")
        self.assertEqual(200, status)
        self.assertGreaterEqual(len(readback["receipts"]), 1)

    def test_storage_assets_counters_service_worker_and_reset(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        first_status, first_headers, first_asset = self.json_request(first + "/assets/v1/data.json")
        second_status, _headers, second_asset = self.json_request(first + "/assets/v1/data.json")
        self.assertEqual((200, 200), (first_status, second_status))
        self.assertEqual(1, first_asset["accessCount"])
        self.assertEqual(2, second_asset["accessCount"])
        self.assertIn(("Cache-Control", "public, max-age=31536000, immutable"), first_headers)
        status, headers, worker = self.request(first + "/service-worker.js")
        self.assertEqual(200, status)
        self.assertIn(b"ahoi-e2e-v1", worker)
        self.assertIn(("Service-Worker-Allowed", "/"), headers)
        status, _headers, reset = self.json_request(first + "/__fixture/reset", method="POST", body=b"")
        self.assertEqual(200, status)
        self.assertTrue(reset["reset"])

    def test_header_echo_csp_and_cors_controls(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        third = self.cluster.context.urls["thirdPartyHttpsUrl"]
        status, headers, echoed = self.json_request(
            first + "/headers/echo",
            headers={"X-Ahoi-Test": "public", "Authorization": "secret", "Cookie": "secret=yes"},
        )
        self.assertEqual(200, status)
        self.assertEqual("public", echoed["allowedValues"]["x-ahoi-test"])
        self.assertNotIn("secret", json.dumps(echoed))
        status, csp_headers, _body = self.request(first + "/csp/strict")
        self.assertEqual(200, status)
        policy = dict(csp_headers)["Content-Security-Policy"]
        self.assertIn("object-src 'none'", policy)
        status, allowed_headers, _body = self.request(
            third + "/cors/allow", headers={"Origin": first}
        )
        self.assertEqual(200, status)
        self.assertEqual(first, dict(allowed_headers)["Access-Control-Allow-Origin"])
        status, denied_headers, _body = self.request(third + "/cors/deny", headers={"Origin": first})
        self.assertEqual(200, status)
        self.assertNotIn("Access-Control-Allow-Origin", dict(denied_headers))
        status, preflight_headers, _body = self.request(
            third + "/cors/allow",
            method="OPTIONS",
            headers={"Origin": first, "Access-Control-Request-Headers": "X-Ahoi-Test"},
        )
        self.assertEqual(204, status)
        self.assertIn("X-Ahoi-Test", dict(preflight_headers)["Access-Control-Allow-Headers"])


class PrivacyUnitTests(unittest.TestCase):
    def test_query_summary_retains_only_keys(self) -> None:
        value = receipts.query_key_summary(
            "/path?utm_campaign=super-secret&normal=also-secret&fbclid=hidden"
        )
        self.assertEqual(["fbclid", "normal", "utm_campaign"], value["queryKeys"])
        self.assertEqual(["fbclid", "utm_campaign"], value["trackingParameterKeys"])
        self.assertNotIn("secret", json.dumps(value))

    def test_unknown_and_dynamic_paths_do_not_persist_user_values(self) -> None:
        self.assertEqual("/pane/:id", receipts.sanitized_path("/pane/personal-name"))
        self.assertEqual("/__unmatched__", receipts.sanitized_path("/private/token-8472"))


if __name__ == "__main__":
    unittest.main()
