#!/usr/bin/env python3
"""Self-tests for the local HTTPS E2E fixture; never modify system trust."""

from __future__ import annotations

import contextlib
import hashlib
import http.client
import io
import json
import plistlib
import ssl
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path
from typing import Mapping, Optional, Sequence, Tuple
from urllib.parse import urlsplit


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

import certificates  # noqa: E402
import custom_protocol  # noqa: E402
import manage  # noqa: E402
import pages  # noqa: E402
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
            trusted = False

            def runner(command, **_kwargs):
                nonlocal trusted
                commands.append(list(command))
                if command[0] == "openssl" and "-fingerprint" in command:
                    algorithm = command[-1]
                    value = manifest["caSha256"] if algorithm == "-sha256" else manifest["caSha1"]
                    return subprocess.CompletedProcess(command, 0, "SHA=%s\n" % value, "")
                if command[:2] == ["security", "find-certificate"]:
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        (
                            "SHA-256 hash: %s\n" % manifest["caSha256"].upper()
                            if trusted
                            else ""
                        ),
                        "",
                    )
                if command[:2] == ["security", "add-trusted-cert"]:
                    trusted = True
                if command[:2] == ["security", "delete-certificate"]:
                    trusted = False
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
        self.cluster.context.reset()

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
                health = json.loads(body)
                self.assertTrue(health["ready"])
                self.assertEqual(
                    server.MOBILE_REAL_E2E_CONTRACT_VERSION,
                    health["mobileRealE2EContractVersion"],
                )
                self.assertEqual(self.cluster.context.instance_id, health["fixtureRunId"])
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
        self.assertEqual(
            server.MOBILE_REAL_E2E_CONTRACT_VERSION,
            manifest["mobileRealE2EContractVersion"],
        )
        self.assertEqual(self.cluster.context.instance_id, manifest["fixtureRunId"])
        self.assertEqual(server.DOWNLOAD_SHA256, manifest["payloads"]["rangeDownload"]["sha256"])
        self.assertEqual(server.PDF_SHA256, manifest["payloads"]["syntheticPdf"]["sha256"])
        self.assertEqual(server.LARGE_ZIP_SHA256, manifest["payloads"]["largeRangeZip"]["sha256"])
        self.assertEqual("no-store", manifest["payloads"]["largeRangeZip"]["cacheControl"])
        self.assertEqual(
            server.LARGE_ZIP_THROTTLE_SECONDS,
            manifest["payloads"]["largeRangeZip"]["throttleSeconds"],
        )
        self.assertEqual(
            server.LARGE_ZIP_SHA256,
            manifest["payloads"]["disconnectResumeZip"]["sha256"],
        )
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
            "safeCustomProtocol",
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

    def test_large_throttled_download_cannot_be_satisfied_from_cache(self) -> None:
        url = self.cluster.context.urls["firstPartyHttpsUrl"] + "/download/large-range.zip"
        status, headers, _body = self.request(url, method="HEAD")
        self.assertEqual(200, status)
        self.assertIn(("Cache-Control", "no-store"), headers)

    def _assert_http_recovery_sequence(self, requested_status: int, token: str) -> None:
        url = (
            self.cluster.context.urls["firstPartyHttpsUrl"]
            + "/failure/recover-once?status=%d&token=%s" % (requested_status, token)
        )
        first_status, first_headers, first_body = self.request(url)
        self.assertEqual(requested_status, first_status)
        self.assertIn(("Cache-Control", "no-store"), first_headers)
        self.assertNotIn(b"http-recovery-ready", first_body)
        self.assertNotIn(b"HTTP recovery complete", first_body)

        second_status, second_headers, second_body = self.request(url)
        self.assertEqual(200, second_status)
        self.assertIn(("Cache-Control", "no-store"), second_headers)
        self.assertIn(b"id='http-recovery-ready'", second_body)
        self.assertIn(b"HTTP recovery complete", second_body)

        third_status, third_headers, third_body = self.request(url)
        self.assertEqual(200, third_status)
        self.assertIn(("Cache-Control", "no-store"), third_headers)
        self.assertIn(b"id='http-recovery-ready'", third_body)
        self.assertIn(b"HTTP recovery complete", third_body)

    def test_http_404_recovers_after_the_first_get(self) -> None:
        self._assert_http_recovery_sequence(404, "not-found-case")

    def test_http_500_recovers_after_the_first_get(self) -> None:
        self._assert_http_recovery_sequence(500, "server-error-case")

    def test_subframe_failures_and_dns_redirect_are_deterministic(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        for status in (404, 500):
            with self.subTest(status=status):
                response_status, headers, body = self.request(
                    first + "/failure/subframe-%d" % status
                )
                self.assertEqual(status, response_status)
                self.assertIn(("Cache-Control", "no-store"), headers)
                self.assertIn(
                    ("id='subframe-http-%d-loaded'" % status).encode("ascii"),
                    body,
                )

        status, headers, body = self.request(first + "/redirect/dns-failure")
        self.assertEqual(302, status)
        self.assertEqual(b"", body)
        self.assertIn(("Cache-Control", "no-store"), headers)
        self.assertIn(("Location", pages.DNS_REDIRECT_FAILURE_URL), headers)

    def test_http_recovery_rejects_unsafe_or_ambiguous_queries_without_incrementing(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        invalid_queries = (
            "status=418&token=validation-case",
            "status=404&token=../validation-case",
            "status=404&token=validation-case&token=other",
            "status=404&token=validation-case&extra=value",
        )
        for query in invalid_queries:
            with self.subTest(query=query):
                status, headers, _body = self.request(
                    first + "/failure/recover-once?" + query
                )
                self.assertEqual(400, status)
                self.assertIn(("Cache-Control", "no-store"), headers)

        status, _headers, _body = self.request(
            first + "/failure/recover-once?status=404&token=validation-case"
        )
        self.assertEqual(404, status)

    def test_pdf_large_zip_and_disconnect_resume_are_deterministic(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        status, pdf_headers, pdf = self.request(first + "/document/synthetic.pdf")
        self.assertEqual(200, status)
        self.assertTrue(pdf.startswith(b"%PDF-1.4"))
        self.assertEqual(server.PDF_SHA256, hashlib.sha256(pdf).hexdigest())
        self.assertEqual("application/pdf", dict(pdf_headers)["Content-Type"])

        status, zip_headers, first_chunk = self.request(
            first + "/download/large-range.zip", headers={"Range": "bytes=0-65535"}
        )
        self.assertEqual(206, status)
        self.assertEqual(server.LARGE_ZIP_BYTES[:65536], first_chunk)
        self.assertIn(("Accept-Ranges", "bytes"), zip_headers)

        split = urlsplit(first + "/download/disconnect-once.zip")
        connection = http.client.HTTPSConnection(
            split.hostname, split.port, timeout=5, context=self.tls
        )
        try:
            connection.request("GET", split.path)
            response = connection.getresponse()
            with self.assertRaises(http.client.IncompleteRead) as interrupted:
                response.read()
            partial = interrupted.exception.partial
        finally:
            connection.close()
        self.assertEqual(server.DISCONNECT_AFTER_BYTES, len(partial))
        status, range_headers, remainder = self.request(
            first + "/download/disconnect-once.zip",
            headers={"Range": "bytes=%d-" % len(partial)},
        )
        self.assertEqual(206, status)
        self.assertIn(("Accept-Ranges", "bytes"), range_headers)
        self.assertEqual(
            server.LARGE_ZIP_SHA256, hashlib.sha256(partial + remainder).hexdigest()
        )

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
            first + "/download-upload": (
                b"upload-drop",
                b"warning-download",
                b"large-range-download",
                b"disconnect-resume-download",
                b"pdf-document",
            ),
            first + "/split": (b"three-pane", b"text/uri-list"),
            first + "/navigation": (
                b"open-popup",
                b"cross-redirect",
                b"ahoi-e2e-safe://open/fixture",
                b"dns-link-failure",
                b"dns-redirect-failure",
                b"subframe-404",
                b"subframe-500",
            ),
            first + "/slow-document": (b"slow-resource.svg", b"Slow document body ready"),
            first + "/webrtc": (b"getUserMedia", b"getDisplayMedia", b"RTCPeerConnection"),
            first + "/permissions": (b"geolocation", b"Notification", b"clipboard"),
            first + "/privacy": (
                b"third-party-cookie",
                b"CHIPS",
                b"set-normal-marker",
                b"set-private-marker",
                b"inspect-private-data",
                b"clear-private-data",
            ),
            first + "/storage": (b"localStorage", b"sessionStorage", b"indexedDB", b"caches", b"serviceWorker"),
            first + "/injection": (
                b"LESS",
                b"SASS",
                b"apply-js",
                b"Run confirm control",
                b"Open blocked Ahoi scheme",
            ),
            first + "/login": (b"fixture-user", b"current-password"),
            media + "/media": (b"requestPictureInPicture", b"MediaSource", b"avc1.42c00c", b"mp4a.40.2"),
        }
        for url, markers in expectations.items():
            with self.subTest(url=url):
                status, _headers, body = self.request(url)
                self.assertEqual(200, status)
                for marker in markers:
                    self.assertIn(marker, body)

        status, headers, body = self.request(first + "/slow-resource.svg", method="HEAD")
        self.assertEqual(200, status)
        self.assertEqual(b"", body)
        header_map = dict(headers)
        self.assertEqual("image/svg+xml", header_map["Content-Type"])
        self.assertEqual(str(len(server.SLOW_RESOURCE_BYTES)), header_map["Content-Length"])
        self.assertEqual("no-store", header_map["Cache-Control"])
        self.assertGreater(server.SLOW_RESOURCE_THROTTLE_SECONDS, 0)

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

    def test_private_data_marker_controls_expose_presence_without_values(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        cookie_pairs = []
        for marker in ("normal", "private"):
            status, headers, result = self.json_request(
                first + "/privacy/marker/set",
                method="POST",
                body=("marker=" + marker).encode("ascii"),
                headers={"Content-Type": "application/x-www-form-urlencoded"},
            )
            self.assertEqual(200, status)
            self.assertEqual(marker, result["marker"])
            self.assertFalse(result["valueExposed"])
            set_cookies = [
                value for name, value in headers if name.lower() == "set-cookie"
            ]
            self.assertEqual(1, len(set_cookies))
            self.assertIn("Secure; HttpOnly; SameSite=Strict", set_cookies[0])
            cookie_pairs.append(set_cookies[0].split(";", 1)[0])

        status, headers, result = self.json_request(
            first + "/privacy/marker/inspect",
            headers={"Cookie": "; ".join(cookie_pairs)},
        )
        self.assertEqual(200, status)
        self.assertIn(("Cache-Control", "no-store"), headers)
        self.assertEqual({"normal": True, "private": True}, result["markers"])
        self.assertFalse(result["valuesExposed"])

        status, headers, result = self.json_request(
            first + "/privacy/marker/clear",
            method="POST",
            body=b"",
        )
        self.assertEqual(200, status)
        self.assertTrue(result["cleared"])
        expired = [value for name, value in headers if name.lower() == "set-cookie"]
        self.assertEqual(2, len(expired))
        self.assertTrue(all("Max-Age=0" in value for value in expired))

        serialized = self.cluster.context.receipts.path.read_text(encoding="utf-8")
        self.assertNotIn(server.PRIVATE_DATA_MARKER_COOKIE_VALUE, serialized)
        self.assertNotIn("ahoi_e2e_normal_marker=", serialized)
        self.assertNotIn("ahoi_e2e_private_marker=", serialized)

    def test_private_data_marker_controls_reject_ambiguous_input(self) -> None:
        first = self.cluster.context.urls["firstPartyHttpsUrl"]
        for body in (
            b"marker=unknown",
            b"marker=normal&marker=private",
            b"marker=normal&extra=value",
            b"marker=",
            b"\xff",
        ):
            with self.subTest(body=body):
                status, headers, result = self.json_request(
                    first + "/privacy/marker/set",
                    method="POST",
                    body=body,
                    headers={"Content-Type": "application/x-www-form-urlencoded"},
                )
                self.assertEqual(400, status)
                self.assertIn(("Cache-Control", "no-store"), headers)
                self.assertEqual("invalid private-data marker", result["error"])

        status, headers, result = self.json_request(
            first + "/privacy/marker/clear",
            method="POST",
            body=b"unexpected",
        )
        self.assertEqual(400, status)
        self.assertIn(("Cache-Control", "no-store"), headers)
        self.assertEqual("private-data clear body must be empty", result["error"])

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
        status, _headers, reset_asset = self.json_request(first + "/assets/v1/data.json")
        self.assertEqual(200, status)
        self.assertEqual(1, reset_asset["accessCount"])

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


class CustomProtocolUnitTests(unittest.TestCase):
    def test_handler_source_accepts_one_url_without_forwarding_it(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            app_path = directory / custom_protocol.APP_NAME
            helper_path = custom_protocol._record_helper_path(app_path)
            helper_sha256 = "a" * 64
            source = custom_protocol._source(
                helper_path, helper_sha256, app_path
            )
        self.assertIn(custom_protocol.ACCEPTED_URL, source)
        self.assertIn("considering case", source)
        self.assertIn("/usr/bin/codesign --verify --deep --strict", source)
        self.assertIn("/usr/bin/grep -Fx", source)
        self.assertIn("Identifier=" + custom_protocol.BUNDLE_ID, source)
        self.assertIn("/usr/bin/shasum -a 256", source)
        self.assertIn(helper_sha256, source)
        self.assertIn(str(helper_path), source)
        self.assertNotIn(str(Path(custom_protocol.__file__).resolve()), source)
        self.assertNotIn("_record --state-dir", source)
        self.assertNotIn("quoted form of incomingUrl", source)

    def test_bundle_record_helper_is_self_contained_and_retains_no_url(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            payload = custom_protocol._record_helper_source(Path(temporary))
        self.assertIn(b"exact-custom-protocol-open", payload)
        self.assertIn(b"O_NOFOLLOW", payload)
        self.assertNotIn(custom_protocol.ACCEPTED_URL.encode("utf-8"), payload)
        self.assertNotIn(str(Path(custom_protocol.__file__).resolve()).encode(), payload)

    def test_existing_handler_requires_exact_config_hashes_and_signature(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            app_path = directory / custom_protocol.APP_NAME
            resources = app_path / "Contents" / "Resources"
            executable_directory = app_path / "Contents" / "MacOS"
            resources.mkdir(parents=True)
            executable_directory.mkdir(parents=True)
            info = {
                "CFBundleDisplayName": "AhoiBrowser E2E Protocol Handler",
                "CFBundleExecutable": "handler",
                "CFBundleIdentifier": custom_protocol.BUNDLE_ID,
                "CFBundleName": "AhoiBrowser E2E Protocol Handler",
                "CFBundleURLTypes": [
                    {
                        "CFBundleURLName": custom_protocol.BUNDLE_ID,
                        "CFBundleURLSchemes": [custom_protocol.SCHEME],
                    }
                ],
                "LSUIElement": True,
            }
            with (app_path / "Contents" / "Info.plist").open("wb") as stream:
                plistlib.dump(info, stream, sort_keys=True)
            (executable_directory / "handler").write_bytes(b"compiled-handler")
            installation_id = "1" * 32
            helper_payload = custom_protocol._record_helper_source(directory)
            helper_path = resources / custom_protocol.RECORD_HELPER_NAME
            helper_path.write_bytes(helper_payload)
            helper_path.chmod(0o400)
            helper_sha256 = hashlib.sha256(helper_payload).hexdigest()
            marker_path = resources / custom_protocol.MARKER_NAME
            marker_path.write_text(
                json.dumps(
                    custom_protocol._expected_marker(
                        app_path, installation_id, helper_sha256
                    ),
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            marker_path.chmod(0o600)
            hashes = custom_protocol._artifact_hashes(app_path, info)
            receipt = {
                "schemaVersion": custom_protocol.RECEIPT_SCHEMA_VERSION,
                "managedBy": custom_protocol.MANAGED_BY,
                "installationId": installation_id,
                "explicitConsent": True,
                "appPath": str(app_path),
                "bundleIdentifier": custom_protocol.BUNDLE_ID,
                "scheme": custom_protocol.SCHEME,
                "acceptedUrl": custom_protocol.ACCEPTED_URL,
                "artifactHashes": hashes,
            }

            def codesign_runner(command, **_kwargs):
                detail = (
                    "Identifier=%s\n" % custom_protocol.BUNDLE_ID
                    if "-d" in command
                    else ""
                )
                return subprocess.CompletedProcess(command, 0, "", detail)

            with mock.patch.object(custom_protocol.sys, "platform", "darwin"):
                self.assertTrue(
                    custom_protocol._valid_app(
                        app_path, receipt, runner=codesign_runner
                    )
                )
                (executable_directory / "handler").write_bytes(b"tampered-handler")
                self.assertFalse(
                    custom_protocol._valid_app(
                        app_path, receipt, runner=codesign_runner
                    )
                )

    def test_receipt_without_an_exact_matching_marker_cannot_own_mutation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            app_path = directory / custom_protocol.APP_NAME
            resources = app_path / "Contents" / "Resources"
            resources.mkdir(parents=True)
            (app_path / "Contents" / "MacOS").mkdir()
            installation_id = "2" * 32
            helper_sha256 = "3" * 64
            hashes = {key: "4" * 64 for key in custom_protocol.HASH_KEYS}
            receipt = {
                "schemaVersion": custom_protocol.RECEIPT_SCHEMA_VERSION,
                "managedBy": custom_protocol.MANAGED_BY,
                "installationId": installation_id,
                "explicitConsent": True,
                "appPath": str(app_path),
                "bundleIdentifier": custom_protocol.BUNDLE_ID,
                "scheme": custom_protocol.SCHEME,
                "acceptedUrl": custom_protocol.ACCEPTED_URL,
                "artifactHashes": hashes,
            }
            self.assertFalse(
                custom_protocol._receipt_marker_owns_app(receipt, app_path)
            )
            marker_path = resources / custom_protocol.MARKER_NAME
            marker_path.write_text(
                json.dumps(
                    custom_protocol._expected_marker(
                        app_path, installation_id, helper_sha256
                    ),
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            marker_path.chmod(0o600)
            hashes["recordHelperSha256"] = helper_sha256
            hashes["markerSha256"] = hashlib.sha256(marker_path.read_bytes()).hexdigest()
            self.assertTrue(
                custom_protocol._receipt_marker_owns_app(receipt, app_path)
            )
            marker_path.write_text("{}\n", encoding="utf-8")
            self.assertFalse(
                custom_protocol._receipt_marker_owns_app(receipt, app_path)
            )

    def test_launchservices_paths_are_exact_and_foreign_claims_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            expected = directory / custom_protocol.APP_NAME
            foreign = directory / "other-state" / custom_protocol.APP_NAME
            dump = (
                "------------------------------\n"
                "bundle id: %s\nbundle path: %s\nbindings: %s:\n"
                "------------------------------\n"
                "identifier: foreign.bundle\npath: %s\nschemes: %s\n"
                % (
                    custom_protocol.BUNDLE_ID,
                    expected,
                    custom_protocol.SCHEME,
                    foreign,
                    custom_protocol.SCHEME,
                )
            )
            paths = custom_protocol._parse_registered_handler_paths(dump)
            self.assertEqual({str(expected), str(foreign)}, set(paths))
            with self.assertRaisesRegex(
                custom_protocol.ProtocolHandlerError, "foreign"
            ):
                custom_protocol._assert_registration_scope(paths, expected)
            custom_protocol._assert_registration_scope(
                frozenset((str(expected),)), expected
            )

    def test_launchservices_claim_without_path_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            custom_protocol.ProtocolHandlerError, "without an exact path"
        ):
            custom_protocol._parse_registered_handler_paths(
                "bundle id: %s\nbindings: %s:\n"
                % (custom_protocol.BUNDLE_ID, custom_protocol.SCHEME)
            )

    def test_launchservices_noncanonical_path_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            custom_protocol.ProtocolHandlerError, "non-canonical"
        ):
            custom_protocol._parse_registered_handler_paths(
                "bundle id: %s\npath: /private/tmp/state/../other/%s\n"
                "bindings: %s:\n"
                % (
                    custom_protocol.BUNDLE_ID,
                    custom_protocol.APP_NAME,
                    custom_protocol.SCHEME,
                )
            )

    def test_status_reports_foreign_registration_without_disclosing_its_path(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            foreign = directory / "private-other-state" / custom_protocol.APP_NAME
            with mock.patch.object(
                custom_protocol.sys, "platform", "darwin"
            ), mock.patch.object(
                custom_protocol,
                "_registered_handler_paths",
                return_value=frozenset((str(foreign),)),
            ):
                result = custom_protocol.status(directory)
            self.assertFalse(result["installed"])
            self.assertTrue(result["foreignRegistrationPresent"])
            self.assertEqual(1, result["registeredPathCount"])
            self.assertNotIn(str(foreign), json.dumps(result, sort_keys=True))

    def test_removal_rechecks_ownership_and_restores_registration(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            app_path = directory / custom_protocol.APP_NAME
            app_path.mkdir()
            receipt = {"installationId": "5" * 32}
            expected_paths = frozenset((str(app_path),))
            with mock.patch.object(
                custom_protocol.sys, "platform", "darwin"
            ), mock.patch.object(
                custom_protocol,
                "_registered_handler_paths",
                return_value=expected_paths,
            ), mock.patch.object(
                custom_protocol,
                "_read_receipt",
                return_value=receipt,
            ), mock.patch.object(
                custom_protocol,
                "_receipt_marker_owns_app",
                side_effect=(True, False),
            ), mock.patch.object(
                custom_protocol, "_unregister_exact"
            ) as unregister, mock.patch.object(
                custom_protocol, "_restore_registration"
            ) as restore, mock.patch.object(
                custom_protocol, "_remove_app_tree"
            ) as remove_tree:
                with self.assertRaisesRegex(
                    custom_protocol.ProtocolHandlerError, "ownership changed"
                ):
                    custom_protocol.remove(
                        directory,
                        confirmation=custom_protocol.REMOVE_CONFIRMATION,
                    )
            unregister.assert_called_once_with(app_path, runner=subprocess.run)
            restore.assert_called_once_with(app_path, True, runner=subprocess.run)
            remove_tree.assert_not_called()

    def test_fixed_protocol_event_retains_no_incoming_value(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-e2e-protocol-") as temporary:
            directory = Path(temporary)
            custom_protocol.record_invocation(directory)
            events = (directory / custom_protocol.EVENTS_NAME).read_text(
                encoding="utf-8"
            )
            self.assertIn("exact-custom-protocol-open", events)
            self.assertIn('"incomingValueRetained": false', events)
            self.assertEqual(
                0o600,
                (directory / custom_protocol.EVENTS_NAME).stat().st_mode & 0o777,
            )


if __name__ == "__main__":
    unittest.main()
