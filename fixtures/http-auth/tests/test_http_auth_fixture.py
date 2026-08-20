from __future__ import annotations

import base64
import contextlib
import http.client
import io
import json
import ssl
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path
from typing import Dict, Mapping, Optional, Tuple
from urllib.parse import urljoin, urlsplit


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIXTURE_DIRECTORY))

from fixture_server import (  # noqa: E402
    CREDENTIALS,
    DigestNonceStore,
    FixtureCluster,
    FixtureHTTPServer,
    FixtureState,
    LOOPBACK_HOST,
    SanitizedEventLog,
    build_digest_authorization,
    parse_authenticate_challenge,
)
from manage import main as manage_main  # noqa: E402


LOCAL_FIXTURE_TLS: Optional[ssl.SSLContext] = None


def basic_header(key: str) -> str:
    credential = CREDENTIALS[key]
    token = base64.b64encode(
        (credential["username"] + ":" + credential["password"]).encode("utf-8")
    ).decode("ascii")
    return "Basic " + token


def request(
    url: str, headers: Optional[Mapping[str, str]] = None
) -> Tuple[int, Dict[str, str], Mapping[str, object]]:
    parsed = urlsplit(url)
    target = parsed.path or "/"
    if parsed.query:
        target += "?" + parsed.query
    if parsed.scheme == "https":
        if LOCAL_FIXTURE_TLS is None:
            raise RuntimeError("fixture TLS trust is not initialized")
        connection: http.client.HTTPConnection = http.client.HTTPSConnection(
            parsed.hostname,
            parsed.port,
            context=LOCAL_FIXTURE_TLS,
            timeout=5,
        )
    else:
        connection = http.client.HTTPConnection(
            parsed.hostname,
            parsed.port,
            timeout=5,
        )
    try:
        connection.request("GET", target, headers=dict(headers or {}))
        response = connection.getresponse()
        raw_body = response.read()
        response_headers = {key.lower(): value for key, value in response.getheaders()}
        body = json.loads(raw_body.decode("utf-8")) if raw_body.startswith(b"{") else {}
        return response.status, response_headers, body
    finally:
        connection.close()


def origin(url: str) -> Tuple[str, str, int]:
    parsed = urlsplit(url)
    default_port = 443 if parsed.scheme == "https" else 80
    return parsed.scheme, parsed.hostname or "", parsed.port or default_port


def safely_follow(
    source_url: str,
    location: str,
    source_headers: Mapping[str, str],
) -> Tuple[int, Dict[str, str], Mapping[str, object]]:
    destination = urljoin(source_url, location)
    forwarded = dict(source_headers)
    if origin(source_url) != origin(destination):
        forwarded.pop("Authorization", None)
        forwarded.pop("authorization", None)
    return request(destination, forwarded)


class HTTPAuthFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        global LOCAL_FIXTURE_TLS
        cls.log_output = io.StringIO()
        cls.cluster = FixtureCluster(log_stream=cls.log_output).start()
        LOCAL_FIXTURE_TLS = ssl.create_default_context(
            ssl.Purpose.SERVER_AUTH,
            cafile=str(cls.cluster.certificate),
        )

    @classmethod
    def tearDownClass(cls) -> None:
        global LOCAL_FIXTURE_TLS
        cls.cluster.stop()
        LOCAL_FIXTURE_TLS = None

    def test_basic_challenges_distinguish_realms_and_ports(self) -> None:
        alpha_status, alpha_headers, _ = request(
            self.cluster.primary_https_url + "/basic/alpha/resource"
        )
        beta_status, beta_headers, _ = request(
            self.cluster.primary_https_url + "/basic/beta/resource"
        )
        secondary_status, secondary_headers, _ = request(
            self.cluster.secondary_https_url + "/basic/alpha/resource"
        )

        self.assertEqual(401, alpha_status)
        self.assertEqual(401, beta_status)
        self.assertEqual(401, secondary_status)
        self.assertIn('realm="Ahoi Basic Alpha"', alpha_headers["www-authenticate"])
        self.assertIn('realm="Ahoi Basic Beta"', beta_headers["www-authenticate"])
        self.assertIn(
            'realm="Ahoi Basic Secondary Port"',
            secondary_headers["www-authenticate"],
        )
        self.assertNotEqual(
            urlsplit(self.cluster.primary_https_url).port,
            urlsplit(self.cluster.secondary_https_url).port,
        )

    def test_basic_success_and_failure(self) -> None:
        success, _, body = request(
            self.cluster.primary_https_url + "/basic/alpha/resource",
            {"Authorization": basic_header("basic-alpha")},
        )
        wrong, wrong_headers, _ = request(
            self.cluster.primary_https_url + "/basic/alpha/resource",
            {"Authorization": basic_header("basic-beta")},
        )
        self.assertEqual(200, success)
        self.assertTrue(body["authenticated"])
        self.assertEqual("basic-alpha", body["protection_space"])
        self.assertEqual(401, wrong)
        self.assertTrue(wrong_headers["www-authenticate"].startswith("Basic "))

    def test_digest_sha256_success_failure_and_replay_rejection(self) -> None:
        target = "/digest/alpha/resource"
        status, headers, _ = request(self.cluster.primary_https_url + target)
        self.assertEqual(401, status)
        challenge_header = headers["www-authenticate"]
        self.assertIn("algorithm=SHA-256", challenge_header)
        self.assertIn('qop="auth"', challenge_header)
        challenge = parse_authenticate_challenge(challenge_header)
        credentials = CREDENTIALS["digest-alpha"]
        authorization = build_digest_authorization(
            challenge=challenge,
            method="GET",
            request_target=target,
            username=credentials["username"],
            password=credentials["password"],
        )

        success, _, body = request(
            self.cluster.primary_https_url + target,
            {"Authorization": authorization},
        )
        replay, _, _ = request(
            self.cluster.primary_https_url + target,
            {"Authorization": authorization},
        )
        invalid = authorization.replace('response="', 'response="00', 1)
        failure, _, _ = request(
            self.cluster.primary_https_url + target,
            {"Authorization": invalid},
        )
        self.assertEqual(200, success)
        self.assertEqual("digest-alpha", body["protection_space"])
        self.assertEqual(401, replay)
        self.assertEqual(401, failure)

    def test_same_origin_redirect_can_reuse_auth(self) -> None:
        source = self.cluster.primary_https_url + "/basic/alpha/redirect-same"
        auth_headers = {"Authorization": basic_header("basic-alpha")}
        status, headers, _ = request(source, auth_headers)
        self.assertEqual(302, status)
        followed, _, body = safely_follow(source, headers["location"], auth_headers)
        self.assertEqual(200, followed)
        self.assertTrue(body["authenticated"])

    def test_cross_origin_redirect_does_not_leak_authorization(self) -> None:
        source = self.cluster.primary_https_url + "/basic/alpha/redirect-cross"
        auth_headers = {"Authorization": basic_header("basic-alpha")}
        status, headers, _ = request(source, auth_headers)
        self.assertEqual(302, status)
        self.assertNotEqual(origin(source), origin(headers["location"]))

        followed, _, body = safely_follow(source, headers["location"], auth_headers)
        self.assertEqual(200, followed)
        self.assertFalse(body["authorization_present"])
        self.assertFalse(self.cluster.cross_observations[-1])

    def test_plain_http_case_is_explicitly_marked_for_browser_warning(self) -> None:
        status, headers, body = request(
            self.cluster.plain_http_url + "/basic/plaintext/resource"
        )
        self.assertEqual(401, status)
        self.assertEqual("true", headers["x-ahoi-fixture-insecure-transport"])
        self.assertEqual(
            "warn-before-save-or-automatic-login",
            headers["x-ahoi-fixture-expected-policy"],
        )
        self.assertEqual("http", body["transport"])

    def test_logs_redact_authorization_and_query_strings(self) -> None:
        token = basic_header("basic-alpha")
        password = CREDENTIALS["basic-alpha"]["password"]
        query_secret = "query-value-must-not-be-logged"
        request(
            self.cluster.primary_https_url
            + "/basic/alpha/resource?token="
            + query_secret,
            {"Authorization": token},
        )
        log = self.log_output.getvalue()
        self.assertIn('"authorization":"[REDACTED]"', log)
        self.assertNotIn(token, log)
        self.assertNotIn(password, log)
        self.assertNotIn(query_secret, log)

    def test_certificate_is_temporary_and_not_a_repository_asset(self) -> None:
        certificate = self.cluster.certificate
        private_key = self.cluster.private_key
        self.assertTrue(certificate.is_file())
        self.assertTrue(private_key.is_file())
        self.assertNotIn(str(FIXTURE_DIRECTORY), str(certificate))


class FixtureLifecycleCLITests(unittest.TestCase):
    def _fixture_state(self) -> FixtureState:
        return FixtureState(
            "lifecycle-regression",
            (),
            SanitizedEventLog(),
            DigestNonceStore(),
        )

    def test_loopback_bind_does_not_depend_on_fqdn_resolution(self) -> None:
        with mock.patch(
            "socket.getfqdn",
            side_effect=AssertionError("loopback fixture attempted FQDN resolution"),
        ):
            server = FixtureHTTPServer((LOOPBACK_HOST, 0), self._fixture_state())
        try:
            self.assertEqual(LOOPBACK_HOST, server.server_name)
            self.assertEqual(server.server_address[1], server.server_port)
        finally:
            server.server_close()

    def test_non_loopback_bind_is_rejected_before_server_creation(self) -> None:
        with mock.patch.object(
            FixtureHTTPServer,
            "server_bind",
            side_effect=AssertionError("unsafe address reached socket bind"),
        ) as server_bind:
            with self.assertRaisesRegex(ValueError, "must bind to IPv4 loopback"):
                FixtureHTTPServer(("0.0.0.0", 0), self._fixture_state())
        server_bind.assert_not_called()

    def test_start_status_stop_are_idempotent(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ahoi-http-auth-cli-test-") as directory:
            state_directory = Path(directory)
            common = ["--state-dir", str(state_directory)]
            start = [
                "start",
                *common,
                "--primary-port",
                "0",
                "--secondary-port",
                "0",
                "--cross-port",
                "0",
                "--http-port",
                "0",
                "--startup-timeout",
                "30",
            ]
            output = io.StringIO()
            errors = io.StringIO()
            key_path: Optional[Path] = None
            try:
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    first = manage_main(start)
                    second = manage_main(start)
                    status = manage_main(["status", *common])
                self.assertEqual(0, first, errors.getvalue())
                self.assertEqual(0, second, errors.getvalue())
                self.assertEqual(0, status, errors.getvalue())
                self.assertIn("already running", output.getvalue())
                state = json.loads((state_directory / "state.json").read_text())
                self.assertNotIn("credentials", state)
                key_path = Path(str(state["certificate"])).with_name(
                    "localhost-key.pem"
                )
                self.assertTrue(key_path.exists())
            finally:
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    stopped = manage_main(["stop", *common])
                    stopped_again = manage_main(["stop", *common])
            self.assertEqual(0, stopped, errors.getvalue())
            self.assertEqual(0, stopped_again, errors.getvalue())
            self.assertIn("already stopped", output.getvalue())
            if key_path is not None:
                self.assertFalse(key_path.exists())


if __name__ == "__main__":
    unittest.main()
