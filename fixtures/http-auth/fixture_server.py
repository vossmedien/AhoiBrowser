#!/usr/bin/env python3
"""Local-only HTTP authentication fixtures for AhoiBrowser.

The module intentionally uses only the Python standard library.  It provides
four loopback servers: two HTTPS origins, a separate HTTPS redirect target,
and one explicitly insecure HTTP origin.  It is test infrastructure, not a
password manager implementation.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import ssl
import subprocess
import tempfile
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, TextIO, Tuple
from urllib.parse import urlsplit


LOOPBACK_HOST = "127.0.0.1"

# Deliberately synthetic, repository-public credentials.  They must never be
# replaced with personal, staging, or production credentials.
CREDENTIALS: Mapping[str, Mapping[str, str]] = {
    "basic-alpha": {
        "username": "fixture-basic-alpha",
        "password": "alpha-password-for-local-tests",
    },
    "basic-beta": {
        "username": "fixture-basic-beta",
        "password": "beta-password-for-local-tests",
    },
    "basic-secondary": {
        "username": "fixture-basic-secondary",
        "password": "secondary-port-password",
    },
    "digest-alpha": {
        "username": "fixture-digest-alpha",
        "password": "digest-alpha-local-password",
    },
    "digest-beta": {
        "username": "fixture-digest-beta",
        "password": "digest-beta-local-password",
    },
    "http-warning": {
        "username": "fixture-http-warning",
        "password": "plaintext-transport-test-only",
    },
}


@dataclass(frozen=True)
class ProtectionSpace:
    key: str
    scheme: str
    realm: str
    path_prefix: str


PRIMARY_SPACES: Tuple[ProtectionSpace, ...] = (
    ProtectionSpace("basic-alpha", "basic", "Ahoi Basic Alpha", "/basic/alpha/"),
    ProtectionSpace("basic-beta", "basic", "Ahoi Basic Beta", "/basic/beta/"),
    ProtectionSpace("digest-alpha", "digest", "Ahoi Digest Alpha", "/digest/alpha/"),
    ProtectionSpace("digest-beta", "digest", "Ahoi Digest Beta", "/digest/beta/"),
)

SECONDARY_SPACES: Tuple[ProtectionSpace, ...] = (
    ProtectionSpace(
        "basic-secondary",
        "basic",
        "Ahoi Basic Secondary Port",
        "/basic/alpha/",
    ),
)

PLAIN_HTTP_SPACES: Tuple[ProtectionSpace, ...] = (
    ProtectionSpace(
        "http-warning",
        "basic",
        "Ahoi Insecure HTTP Warning",
        "/basic/plaintext/",
    ),
)


class SanitizedEventLog:
    """Thread-safe JSONL logger which never serializes credential material."""

    def __init__(self, stream: Optional[TextIO] = None) -> None:
        self._stream = stream
        self._lock = threading.Lock()
        self.events: List[Mapping[str, object]] = []

    def request(
        self,
        *,
        server_role: str,
        method: str,
        path: str,
        status: int,
        authorization_present: bool,
    ) -> None:
        event: Mapping[str, object] = {
            "event": "request",
            "time_unix_ms": int(time.time() * 1000),
            "server_role": server_role,
            "method": method,
            # Never persist query strings: they may themselves contain secrets.
            "path": urlsplit(path).path,
            "status": status,
            "authorization_present": authorization_present,
            "authorization": "[REDACTED]" if authorization_present else None,
        }
        encoded = json.dumps(event, sort_keys=True, separators=(",", ":"))
        with self._lock:
            self.events.append(event)
            if self._stream is not None:
                self._stream.write(encoded + "\n")
                self._stream.flush()


class DigestNonceStore:
    """Issues per-run nonces and rejects replayed nonce-count values."""

    def __init__(self) -> None:
        self._secret = secrets.token_bytes(32)
        self._lock = threading.Lock()
        self._issued: MutableMapping[str, Tuple[str, str]] = {}
        self._highest_nc: MutableMapping[Tuple[str, str, str], int] = {}
        self._counter = 0

    def issue(self, realm: str) -> Tuple[str, str]:
        with self._lock:
            self._counter += 1
            seed = "%s:%d:%d" % (realm, self._counter, time.monotonic_ns())
            nonce = hmac.new(self._secret, seed.encode("utf-8"), hashlib.sha256).hexdigest()
            opaque = hmac.new(
                self._secret,
                ("opaque:" + realm).encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            self._issued[nonce] = (realm, opaque)
            return nonce, opaque

    def validate_and_consume(
        self,
        *,
        nonce: str,
        opaque: str,
        realm: str,
        username: str,
        cnonce: str,
        nc_hex: str,
    ) -> bool:
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", nc_hex):
            return False
        nc = int(nc_hex, 16)
        if nc <= 0:
            return False
        with self._lock:
            issued = self._issued.get(nonce)
            if issued is None:
                return False
            issued_realm, issued_opaque = issued
            if not hmac.compare_digest(issued_realm, realm):
                return False
            if not hmac.compare_digest(issued_opaque, opaque):
                return False
            replay_key = (nonce, username, cnonce)
            previous = self._highest_nc.get(replay_key, 0)
            if nc <= previous:
                return False
            self._highest_nc[replay_key] = nc
            return True


def _split_digest_fields(value: str) -> Iterable[str]:
    start = 0
    quoted = False
    escaped = False
    for index, character in enumerate(value):
        if escaped:
            escaped = False
        elif character == "\\" and quoted:
            escaped = True
        elif character == '"':
            quoted = not quoted
        elif character == "," and not quoted:
            yield value[start:index].strip()
            start = index + 1
    yield value[start:].strip()


def parse_digest_authorization(header: str) -> Optional[Dict[str, str]]:
    if not header.lower().startswith("digest "):
        return None
    parsed: Dict[str, str] = {}
    for field in _split_digest_fields(header[7:].strip()):
        if "=" not in field:
            return None
        raw_key, raw_value = field.split("=", 1)
        key = raw_key.strip().lower()
        value = raw_value.strip()
        if not key or key in parsed:
            return None
        if value.startswith('"'):
            if len(value) < 2 or not value.endswith('"'):
                return None
            value = re.sub(r"\\(.)", r"\1", value[1:-1])
        parsed[key] = value
    return parsed


def _sha256_hex(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def build_digest_authorization(
    *,
    challenge: Mapping[str, str],
    method: str,
    request_target: str,
    username: str,
    password: str,
    nc: int = 1,
    cnonce: str = "fixture-client-cnonce",
) -> str:
    """Build a SHA-256/qop=auth header for fixture clients and tests."""

    realm = challenge["realm"]
    nonce = challenge["nonce"]
    opaque = challenge["opaque"]
    nc_hex = "%08x" % nc
    ha1 = _sha256_hex("%s:%s:%s" % (username, realm, password))
    ha2 = _sha256_hex("%s:%s" % (method, request_target))
    response = _sha256_hex(
        "%s:%s:%s:%s:auth:%s" % (ha1, nonce, nc_hex, cnonce, ha2)
    )
    return (
        'Digest username="%s", realm="%s", nonce="%s", uri="%s", '
        'algorithm=SHA-256, response="%s", opaque="%s", qop=auth, '
        'nc=%s, cnonce="%s"'
        % (
            username,
            realm,
            nonce,
            request_target,
            response,
            opaque,
            nc_hex,
            cnonce,
        )
    )


def parse_authenticate_challenge(header: str) -> Dict[str, str]:
    """Parse a single fixture challenge (used by tests; not a general parser)."""

    if header.lower().startswith("digest "):
        result = parse_digest_authorization(header)
        if result is None:
            raise ValueError("invalid Digest challenge")
        return result
    raise ValueError("only Digest challenges have parameter maps")


@dataclass
class FixtureState:
    role: str
    protection_spaces: Tuple[ProtectionSpace, ...]
    event_log: SanitizedEventLog
    nonce_store: DigestNonceStore
    cross_https_url: str = ""
    cross_observations: Optional[List[bool]] = None


class FixtureHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: Tuple[str, int], state: FixtureState) -> None:
        self.fixture_state = state
        super().__init__(address, FixtureRequestHandler)


class FixtureRequestHandler(BaseHTTPRequestHandler):
    server_version = "AhoiHttpAuthFixture/1"
    sys_version = ""
    protocol_version = "HTTP/1.1"

    @property
    def state(self) -> FixtureState:
        return self.server.fixture_state  # type: ignore[attr-defined,no-any-return]

    def log_message(self, _format: str, *args: object) -> None:
        # BaseHTTPRequestHandler includes the raw request line.  Centralized
        # structured logging below is intentionally stricter and redacted.
        return

    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        authorization = self.headers.get("Authorization")
        status = self._route(authorization)
        self.state.event_log.request(
            server_role=self.state.role,
            method="GET",
            path=self.path,
            status=status,
            authorization_present=authorization is not None,
        )

    def _route(self, authorization: Optional[str]) -> int:
        path = urlsplit(self.path).path
        if path == "/__fixture/health":
            return self._json(200, {"ok": True, "role": self.state.role})
        if path == "/__fixture/observer" and self.state.role == "cross":
            present = authorization is not None
            if self.state.cross_observations is not None:
                self.state.cross_observations.append(present)
            return self._json(200, {"authorization_present": present})

        if self.state.role == "primary" and path == "/redirect/cross-origin":
            return self._redirect(self.state.cross_https_url + "/__fixture/observer")
        if self.state.role == "primary" and path == "/redirect/same-origin":
            return self._redirect("/basic/alpha/resource")

        space = next(
            (
                candidate
                for candidate in self.state.protection_spaces
                if path.startswith(candidate.path_prefix)
            ),
            None,
        )
        if space is None:
            return self._json(
                404,
                {"error": "unknown fixture route", "role": self.state.role},
            )

        if space.scheme == "basic":
            authorized = self._check_basic(space, authorization)
        else:
            authorized = self._check_digest(space, authorization)
        if not authorized:
            return self._challenge(space)

        if path.endswith("/redirect-cross") and self.state.role == "primary":
            return self._redirect(self.state.cross_https_url + "/__fixture/observer")
        if path.endswith("/redirect-same") and self.state.role == "primary":
            return self._redirect(space.path_prefix + "resource")
        return self._json(
            200,
            {
                "authenticated": True,
                "protection_space": space.key,
                "realm": space.realm,
                "server_role": self.state.role,
            },
        )

    def _check_basic(
        self, space: ProtectionSpace, authorization: Optional[str]
    ) -> bool:
        if authorization is None or not authorization.lower().startswith("basic "):
            return False
        try:
            decoded = base64.b64decode(
                authorization.split(None, 1)[1], validate=True
            ).decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            return False
        if ":" not in decoded:
            return False
        username, password = decoded.split(":", 1)
        expected = CREDENTIALS[space.key]
        return hmac.compare_digest(username, expected["username"]) and hmac.compare_digest(
            password, expected["password"]
        )

    def _check_digest(
        self, space: ProtectionSpace, authorization: Optional[str]
    ) -> bool:
        if authorization is None:
            return False
        fields = parse_digest_authorization(authorization)
        if fields is None:
            return False
        required = {
            "username",
            "realm",
            "nonce",
            "uri",
            "response",
            "opaque",
            "qop",
            "nc",
            "cnonce",
            "algorithm",
        }
        if not required.issubset(fields):
            return False
        expected_credentials = CREDENTIALS[space.key]
        expected_pairs = (
            (fields["username"], expected_credentials["username"]),
            (fields["realm"], space.realm),
            (fields["uri"], self.path),
            (fields["qop"].lower(), "auth"),
            (fields["algorithm"].upper(), "SHA-256"),
        )
        if not all(hmac.compare_digest(actual, expected) for actual, expected in expected_pairs):
            return False

        ha1 = _sha256_hex(
            "%s:%s:%s"
            % (
                expected_credentials["username"],
                space.realm,
                expected_credentials["password"],
            )
        )
        ha2 = _sha256_hex("%s:%s" % (self.command, fields["uri"]))
        expected_response = _sha256_hex(
            "%s:%s:%s:%s:%s:%s"
            % (
                ha1,
                fields["nonce"],
                fields["nc"],
                fields["cnonce"],
                fields["qop"],
                ha2,
            )
        )
        if not hmac.compare_digest(fields["response"], expected_response):
            return False
        return self.state.nonce_store.validate_and_consume(
            nonce=fields["nonce"],
            opaque=fields["opaque"],
            realm=space.realm,
            username=fields["username"],
            cnonce=fields["cnonce"],
            nc_hex=fields["nc"],
        )

    def _challenge(self, space: ProtectionSpace) -> int:
        if space.scheme == "basic":
            challenge = 'Basic realm="%s", charset="UTF-8"' % space.realm
        else:
            nonce, opaque = self.state.nonce_store.issue(space.realm)
            challenge = (
                'Digest realm="%s", qop="auth", algorithm=SHA-256, '
                'nonce="%s", opaque="%s", charset=UTF-8'
                % (space.realm, nonce, opaque)
            )
        headers = {"WWW-Authenticate": challenge}
        if self.state.role == "plain-http":
            headers.update(
                {
                    "X-Ahoi-Fixture-Insecure-Transport": "true",
                    "X-Ahoi-Fixture-Expected-Policy": (
                        "warn-before-save-or-automatic-login"
                    ),
                }
            )
        return self._json(
            401,
            {
                "authenticated": False,
                "protection_space": space.key,
                "realm": space.realm,
                "transport": "http" if self.state.role == "plain-http" else "https",
            },
            headers,
        )

    def _redirect(self, location: str) -> int:
        body = b"redirecting\n"
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return 302

    def _json(
        self,
        status: int,
        value: Mapping[str, object],
        headers: Optional[Mapping[str, str]] = None,
    ) -> int:
        body = (json.dumps(value, sort_keys=True) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        if headers:
            for name, header_value in headers.items():
                self.send_header(name, header_value)
        self.end_headers()
        self.wfile.write(body)
        return status


def generate_temporary_certificate(directory: Path) -> Tuple[Path, Path]:
    """Generate a short-lived localhost certificate using the system OpenSSL."""

    directory.mkdir(parents=True, exist_ok=True)
    certificate = directory / "localhost-cert.pem"
    private_key = directory / "localhost-key.pem"
    config = directory / "openssl-localhost.cnf"
    config.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "prompt = no\n"
        "x509_extensions = v3_req\n"
        "[dn]\n"
        "CN = localhost\n"
        "[v3_req]\n"
        "basicConstraints = critical,CA:FALSE\n"
        "keyUsage = critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage = serverAuth\n"
        "subjectAltName = @alt_names\n"
        "[alt_names]\n"
        "DNS.1 = localhost\n"
        "IP.1 = 127.0.0.1\n",
        encoding="utf-8",
    )
    command = [
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "ec",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-sha256",
        "-nodes",
        "-days",
        "2",
        "-keyout",
        str(private_key),
        "-out",
        str(certificate),
        "-config",
        str(config),
        "-extensions",
        "v3_req",
    ]
    try:
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError("openssl is required to create the local TLS fixture") from error
    except subprocess.CalledProcessError as error:
        raise RuntimeError("openssl certificate generation failed: %s" % error.stderr) from error
    os.chmod(private_key, 0o600)
    return certificate, private_key


class FixtureCluster:
    """Lifecycle wrapper around all HTTP-auth fixture origins."""

    def __init__(
        self,
        *,
        runtime_directory: Optional[Path] = None,
        primary_port: int = 0,
        secondary_port: int = 0,
        cross_port: int = 0,
        http_port: int = 0,
        log_stream: Optional[TextIO] = None,
    ) -> None:
        self._owned_tempdir: Optional[tempfile.TemporaryDirectory[str]] = None
        if runtime_directory is None:
            self._owned_tempdir = tempfile.TemporaryDirectory(
                prefix="ahoibrowser-http-auth-"
            )
            runtime_directory = Path(self._owned_tempdir.name)
        self.runtime_directory = runtime_directory
        self.runtime_directory.mkdir(parents=True, exist_ok=True)
        self.certificate, self.private_key = generate_temporary_certificate(
            self.runtime_directory
        )
        self.event_log = SanitizedEventLog(log_stream)
        self.nonce_store = DigestNonceStore()
        self.cross_observations: List[bool] = []

        self._cross = FixtureHTTPServer(
            (LOOPBACK_HOST, cross_port),
            FixtureState(
                "cross",
                (),
                self.event_log,
                self.nonce_store,
                cross_observations=self.cross_observations,
            ),
        )
        self.cross_https_url = "https://%s:%d" % (
            LOOPBACK_HOST,
            self._cross.server_address[1],
        )
        self._primary = FixtureHTTPServer(
            (LOOPBACK_HOST, primary_port),
            FixtureState(
                "primary",
                PRIMARY_SPACES,
                self.event_log,
                self.nonce_store,
                cross_https_url=self.cross_https_url,
            ),
        )
        self._secondary = FixtureHTTPServer(
            (LOOPBACK_HOST, secondary_port),
            FixtureState(
                "secondary",
                SECONDARY_SPACES,
                self.event_log,
                self.nonce_store,
            ),
        )
        self._plain = FixtureHTTPServer(
            (LOOPBACK_HOST, http_port),
            FixtureState(
                "plain-http",
                PLAIN_HTTP_SPACES,
                self.event_log,
                self.nonce_store,
            ),
        )
        self._servers = (self._primary, self._secondary, self._cross, self._plain)
        self._threads: List[threading.Thread] = []

        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(str(self.certificate), str(self.private_key))
        for server in (self._primary, self._secondary, self._cross):
            server.socket = tls_context.wrap_socket(server.socket, server_side=True)

    @property
    def primary_https_url(self) -> str:
        return "https://%s:%d" % (LOOPBACK_HOST, self._primary.server_address[1])

    @property
    def secondary_https_url(self) -> str:
        return "https://%s:%d" % (LOOPBACK_HOST, self._secondary.server_address[1])

    @property
    def plain_http_url(self) -> str:
        return "http://%s:%d" % (LOOPBACK_HOST, self._plain.server_address[1])

    def start(self) -> "FixtureCluster":
        if self._threads:
            return self
        for server in self._servers:
            thread = threading.Thread(
                target=server.serve_forever,
                name="http-auth-fixture-%s" % server.fixture_state.role,
                daemon=True,
            )
            thread.start()
            self._threads.append(thread)
        return self

    def stop(self) -> None:
        shutdown_threads = [
            threading.Thread(target=server.shutdown, daemon=True)
            for server in self._servers
        ]
        for thread in shutdown_threads:
            thread.start()
        for thread in shutdown_threads:
            thread.join(timeout=5)
        for server in self._servers:
            server.server_close()
        for thread in self._threads:
            thread.join(timeout=5)
        self._threads.clear()
        for path in (
            self.private_key,
            self.certificate,
            self.runtime_directory / "openssl-localhost.cnf",
        ):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        if self._owned_tempdir is not None:
            self._owned_tempdir.cleanup()
            self._owned_tempdir = None

    def describe(self) -> Mapping[str, object]:
        return {
            "primary_https_url": self.primary_https_url,
            "secondary_https_url": self.secondary_https_url,
            "cross_https_url": self.cross_https_url,
            "plain_http_url": self.plain_http_url,
            "certificate": str(self.certificate),
            "credentials": CREDENTIALS,
            "note": "All credentials are synthetic and local-test-only.",
        }

    def __enter__(self) -> "FixtureCluster":
        return self.start()

    def __exit__(self, *_args: object) -> None:
        self.stop()
