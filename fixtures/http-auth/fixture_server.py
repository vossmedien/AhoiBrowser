#!/usr/bin/env python3
"""Protocol and request-handler support for local AhoiBrowser auth fixtures.

The module intentionally uses only the Python standard library.  It provides
five loopback servers: two HTTPS origins, a separate HTTPS redirect target,
one explicitly insecure HTTP origin, and a non-forwarding synthetic proxy.
It is test infrastructure, not a password manager implementation.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import re
import secrets
import socketserver
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import (
    Dict,
    Iterable,
    List,
    Mapping,
    MutableMapping,
    Optional,
    TextIO,
    Tuple,
    Type,
)
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
    "subresource": {
        "username": "fixture-subresource",
        "password": "subresource-password-for-local-tests",
    },
    "proxy": {
        "username": "fixture-proxy",
        "password": "proxy-password-for-local-tests",
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
    ProtectionSpace(
        "subresource",
        "basic",
        "Ahoi Subresource Image",
        "/subresource/protected",
    ),
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
        proxy_authorization_present: bool = False,
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
            "proxy_authorization_present": proxy_authorization_present,
            "proxy_authorization": (
                "[REDACTED]" if proxy_authorization_present else None
            ),
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

    def __init__(
        self,
        address: Tuple[str, int],
        state: FixtureState,
        handler_class: Optional[Type[BaseHTTPRequestHandler]] = None,
    ) -> None:
        if address[0] != LOOPBACK_HOST:
            raise ValueError("HTTP-auth fixtures must bind to IPv4 loopback")
        self.fixture_state = state
        super().__init__(address, handler_class or FixtureRequestHandler)

    def server_bind(self) -> None:
        """Bind without HTTPServer's unnecessary reverse-DNS lookup.

        HTTPServer.server_bind() calls socket.getfqdn() after bind and before
        listen.  macOS 15+ local-network privacy can stall that lookup for
        roughly 35 seconds on hosted runners, leaving the socket bound but not
        listening.  This fixture has a fixed loopback identity, so resolving a
        public hostname is both unnecessary and undesirable.
        """

        socketserver.TCPServer.server_bind(self)
        bound_host, bound_port = self.server_address[:2]
        if bound_host != LOOPBACK_HOST:
            self.server_close()
            raise RuntimeError("HTTP-auth fixture escaped the loopback bind")
        self.server_name = LOOPBACK_HOST
        self.server_port = bound_port


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
        if self.state.role == "primary" and path == "/subresource/page":
            return self._subresource_page()

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
        if space.key == "subresource":
            return self._subresource_image()
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
        return _basic_matches(authorization, space.key)

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

    def _subresource_page(self) -> int:
        body = (
            "<!doctype html><meta charset=utf-8>"
            "<title>Ahoi subresource authentication fixture</title>"
            "<h1>Subresource authentication fixture</h1>"
            "<p>The image below is the only protected subresource.</p>"
            '<img src="/subresource/protected.svg" '
            'alt="Protected fixture image">'
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'none'; img-src 'self'")
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header(
            "X-Ahoi-Fixture-Subresource", "/subresource/protected.svg"
        )
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return 200

    def _subresource_image(self) -> int:
        body = (
            '<svg xmlns="http://www.w3.org/2000/svg" width="240" height="80">'
            '<rect width="240" height="80" fill="#0b6bcb"/>'
            '<text x="16" y="48" fill="white" font-size="18">'
            "Protected fixture loaded</text></svg>"
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Type", "image/svg+xml")
        self.send_header("X-Ahoi-Fixture-Protection-Space", "subresource")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return 200

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


def _basic_matches(authorization: Optional[str], credential_key: str) -> bool:
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
    expected = CREDENTIALS[credential_key]
    return hmac.compare_digest(username, expected["username"]) and hmac.compare_digest(
        password, expected["password"]
    )


class ProxyFixtureRequestHandler(FixtureRequestHandler):
    """Synthetic proxy-auth endpoint which never forwards arbitrary traffic."""

    def do_CONNECT(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._proxy_response()

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._proxy_response()

    def _proxy_response(self) -> None:
        authorization = self.headers.get("Authorization")
        proxy_authorization = self.headers.get("Proxy-Authorization")
        accepted = _basic_matches(proxy_authorization, "proxy")
        if accepted:
            status = self._json(
                200,
                {
                    "origin_authorization_present": authorization is not None,
                    "proxy_authenticated": True,
                    "synthetic_non_forwarding": True,
                },
                {"X-Ahoi-Fixture-Proxy-Auth": "accepted"},
            )
        else:
            status = self._json(
                407,
                {"proxy_authenticated": False, "synthetic_non_forwarding": True},
                {
                    "Connection": "close",
                    "Proxy-Authenticate": (
                        'Basic realm="Ahoi Proxy", charset="UTF-8"'
                    ),
                },
            )
        self.state.event_log.request(
            server_role=self.state.role,
            method=self.command,
            path=self.path,
            status=status,
            authorization_present=authorization is not None,
            proxy_authorization_present=proxy_authorization is not None,
        )
