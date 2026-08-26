#!/usr/bin/env python3
# Copyright 2026 The AhoiBrowser Authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Local-only HTTP-auth fixture with redacted receipts.

All accepted credentials are synthetic constants. Authorization headers and
passwords are never logged or persisted. Bind addresses are loopback only.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import ssl
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


SYNTHETIC_ACCOUNTS = {
    "alpha": {"captain": "synthetic-alpha-1", "deckhand": "synthetic-alpha-2"},
    "beta": {"captain": "synthetic-beta-1"},
    "shared": {"shared-user": "synthetic-shared-1"},
    "rotating-v1": {"rotating-user": "synthetic-old"},
    "rotating-v2": {"rotating-user": "synthetic-new"},
    "digest": {"digest-user": "synthetic-digest"},
    "subresource": {"image-user": "synthetic-image"},
    "proxy": {"proxy-user": "synthetic-proxy"},
}

DIGEST_NONCE = "ahoi-fixture-nonce-v1"


@dataclass(frozen=True)
class Receipt:
    timestamp: int
    transport: str
    realm: str
    username_hash: str
    outcome: str
    path: str


class FixtureState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._receipts: list[Receipt] = []

    def record(self, transport: str, realm: str, username: str,
               outcome: str, path: str) -> None:
        username_hash = hashlib.sha256(username.encode("utf-8")).hexdigest()[:12]
        receipt = Receipt(int(time.time()), transport, realm, username_hash,
                          outcome, path.split("?", 1)[0])
        with self._lock:
            self._receipts.append(receipt)
            self._receipts = self._receipts[-200:]

    def snapshot(self) -> list[dict[str, object]]:
        with self._lock:
            return [receipt.__dict__.copy() for receipt in self._receipts]


STATE = FixtureState()


def _constant_time_basic(header: str | None,
                         account_set: str) -> tuple[bool, str]:
    if not header or not header.startswith("Basic "):
        return False, ""
    try:
        decoded = base64.b64decode(header[6:], validate=True).decode("utf-8")
        username, password = decoded.split(":", 1)
    except (ValueError, UnicodeDecodeError):
        return False, ""
    expected = SYNTHETIC_ACCOUNTS.get(account_set, {}).get(username, "")
    accepted = bool(expected) and hashlib.sha256(password.encode()).digest() == \
        hashlib.sha256(expected.encode()).digest()
    return accepted, username


def _digest_fields(header: str) -> dict[str, str]:
    result: dict[str, str] = {}
    if not header.startswith("Digest "):
        return result
    for part in header[7:].split(","):
        key, separator, value = part.strip().partition("=")
        if separator:
            result[key] = value.strip().strip('"')
    return result


def _constant_time_digest(header: str | None, method: str,
                          expected_uri: str) -> tuple[bool, str]:
    fields = _digest_fields(header or "")
    username = fields.get("username", "")
    password = SYNTHETIC_ACCOUNTS["digest"].get(username, "")
    if not password or fields.get("realm") != "digest" or \
            fields.get("nonce") != DIGEST_NONCE or \
            fields.get("uri") != expected_uri:
        return False, username
    ha1 = hashlib.md5(f"{username}:digest:{password}".encode()).hexdigest()
    ha2 = hashlib.md5(f"{method}:{expected_uri}".encode()).hexdigest()
    if fields.get("qop"):
        expected = hashlib.md5(
            f"{ha1}:{DIGEST_NONCE}:{fields.get('nc', '')}:"
            f"{fields.get('cnonce', '')}:{fields['qop']}:{ha2}".encode()
        ).hexdigest()
    else:
        expected = hashlib.md5(f"{ha1}:{DIGEST_NONCE}:{ha2}".encode()).hexdigest()
    return bool(fields.get("response")) and hmac.compare_digest(
        fields["response"], expected), username


class AuthFixtureHandler(BaseHTTPRequestHandler):
    server_version = "AhoiHttpAuthFixture/1"

    def log_message(self, _format: str, *_args: object) -> None:
        # BaseHTTPRequestHandler would log request paths. Keep the fixture
        # silent so Authorization-like query mistakes cannot become evidence.
        return

    @property
    def transport(self) -> str:
        return "https" if isinstance(self.connection, ssl.SSLSocket) else "http"

    def _json(self, status: HTTPStatus, payload: object,
              headers: dict[str, str] | None = None) -> None:
        body = json.dumps(payload, sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _challenge_basic(self, realm: str,
                         account_set: str | None = None) -> None:
        accepted, username = _constant_time_basic(
            self.headers.get("Authorization"), account_set or realm)
        STATE.record(self.transport, realm, username, "accepted" if accepted
                     else "challenged", self.path)
        if not accepted:
            self._json(HTTPStatus.UNAUTHORIZED, {"authenticated": False},
                       {"WWW-Authenticate": f'Basic realm="{realm}"'})
            return
        self._json(HTTPStatus.OK, {
            "authenticated": True,
            "realm": realm,
            "username_hash": hashlib.sha256(username.encode()).hexdigest()[:12],
        })

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = urlsplit(self.path).path
        if path == "/healthz":
            self._json(HTTPStatus.OK, {"ok": True, "transport": self.transport})
        elif path == "/receipts":
            self._json(HTTPStatus.OK, STATE.snapshot())
        elif path.startswith("/alpha/"):
            if path == "/alpha/redirect":
                self.send_response(HTTPStatus.FOUND)
                self.send_header("Location", "/alpha/redirect-target")
                self.end_headers()
            else:
                self._challenge_basic("alpha")
        elif path.startswith("/beta/"):
            self._challenge_basic("beta")
        elif path.startswith("/shared/"):
            self._challenge_basic("shared")
        elif path.startswith("/password-v1/"):
            self._challenge_basic("rotating", "rotating-v1")
        elif path.startswith("/password-v2/"):
            self._challenge_basic("rotating", "rotating-v2")
        elif path == "/always-401":
            STATE.record(self.transport, "alpha", "", "forced-401", path)
            self._json(HTTPStatus.UNAUTHORIZED, {"authenticated": False},
                       {"WWW-Authenticate": 'Basic realm="alpha"'})
        elif path == "/digest":
            accepted, username = _constant_time_digest(
                self.headers.get("Authorization"), self.command, path)
            STATE.record(self.transport, "digest", username,
                         "accepted" if accepted else "challenged", path)
            if accepted:
                self._json(HTTPStatus.OK, {"authenticated": True,
                                           "realm": "digest"})
            else:
                challenge = (f'Digest realm="digest", nonce="{DIGEST_NONCE}", '
                             'algorithm=MD5, qop="auth"')
                self._json(HTTPStatus.UNAUTHORIZED, {"authenticated": False},
                           {"WWW-Authenticate": challenge})
        elif path == "/subresource-page":
            body = (b"<!doctype html><meta charset=utf-8><title>Auth fixture</title>"
                    b"<img src=/subresource/protected alt=fixture>")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/subresource/protected":
            self._challenge_basic("subresource")
        elif path == "/cross-origin-redirect":
            port = self.server.server_address[1]
            foreign_port = port + 1
            self.send_response(HTTPStatus.FOUND)
            self.send_header("Location", f"http://127.0.0.1:{foreign_port}/healthz")
            self.end_headers()
        else:
            self._json(HTTPStatus.NOT_FOUND, {"error": "unknown fixture path"})


class ProxyFixtureHandler(AuthFixtureHandler):
    def do_CONNECT(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._proxy_response()

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._proxy_response()

    def _proxy_response(self) -> None:
        accepted, username = _constant_time_basic(
            self.headers.get("Proxy-Authorization"), "proxy")
        STATE.record("proxy", "proxy", username,
                     "accepted" if accepted else "challenged", "/proxy")
        if not accepted:
            self._json(HTTPStatus.PROXY_AUTHENTICATION_REQUIRED,
                       {"authenticated": False},
                       {"Proxy-Authenticate": 'Basic realm="proxy"'})
            return
        # Deliberately does not forward arbitrary traffic.
        self._json(HTTPStatus.OK, {"authenticated": True, "synthetic": True})


def _serve(port: int, handler: type[BaseHTTPRequestHandler],
           cert: str | None = None, key: str | None = None) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    if cert and key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--http-port", type=int, default=17651)
    parser.add_argument("--secondary-http-port", type=int, default=17652)
    parser.add_argument("--https-port", type=int, default=17653)
    parser.add_argument("--proxy-port", type=int, default=17654)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    if bool(args.cert) != bool(args.key):
        parser.error("--cert and --key must be provided together")

    servers = [
        _serve(args.http_port, AuthFixtureHandler),
        _serve(args.secondary_http_port, AuthFixtureHandler),
        _serve(args.proxy_port, ProxyFixtureHandler),
    ]
    if args.cert:
        servers.append(_serve(args.https_port, AuthFixtureHandler,
                              args.cert, args.key))
    print(json.dumps({
        "ready": True,
        "bind": "127.0.0.1",
        "http_ports": [args.http_port, args.secondary_http_port],
        "https_port": args.https_port if args.cert else None,
        "proxy_port": args.proxy_port,
    }, sort_keys=True), flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        for server in servers:
            server.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
