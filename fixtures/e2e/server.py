#!/usr/bin/env python3
"""Loopback-only multi-origin HTTPS server for deterministic browser journeys."""

from __future__ import annotations

import hashlib
import hmac
import html
import json
import re
import socket
import socketserver
import ssl
import threading
import time
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple
from urllib.parse import parse_qs, urlencode, urlsplit

import pages
from payloads import (
    ASSET_CHUNK_BYTES,
    DISCONNECT_AFTER_BYTES,
    DOWNLOAD_BYTES,
    DOWNLOAD_SHA256,
    LARGE_ZIP_BYTES,
    LARGE_ZIP_SHA256,
    LARGE_ZIP_THROTTLE_SECONDS,
    MEDIA_BYTES,
    MEDIA_SHA256,
    PDF_BYTES,
    PDF_SHA256,
    WARNING_BYTES,
    WARNING_SHA256,
    parse_range as _parse_range,
)
from receipts import ReceiptStore, query_key_summary


LOOPBACK_HOST = "127.0.0.1"
FIRST_HOST_NAME = "first-party.localhost"
THIRD_HOST_NAME = "third-party.localhost"
MEDIA_HOST_NAME = "media.localhost"
MAX_UPLOAD_BYTES = 16 * 1024 * 1024
SYNTHETIC_USERNAME = "fixture-user"
SYNTHETIC_PASSWORD = "fixture-password"


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _safe_filename(value: str) -> str:
    basename = value.replace("\\", "/").rsplit("/", 1)[-1]
    cleaned = re.sub(r"[^A-Za-z0-9._ -]", "_", basename)[:120]
    return cleaned or "unnamed-upload.bin"


def _cookie_names(value: str) -> Sequence[str]:
    return sorted(
        {
            part.split("=", 1)[0].strip()
            for part in value.split(";")
            if "=" in part and part.split("=", 1)[0].strip()
        }
    )


def _safe_referrer(value: str) -> Optional[str]:
    if not value:
        return None
    split = urlsplit(value)
    if split.scheme not in {"http", "https"} or not split.netloc:
        return "present-but-invalid"
    return "%s://%s%s" % (split.scheme, split.netloc, split.path)


class FixtureContext:
    def __init__(self, runtime_directory: Path) -> None:
        self.runtime_directory = runtime_directory
        self.instance_id = str(uuid.uuid4())
        self.receipts = ReceiptStore(
            runtime_directory / "receipts.jsonl", run_id=self.instance_id
        )
        self.urls: Dict[str, str] = {}
        self._counter_lock = threading.Lock()
        self._counters: Dict[str, int] = {}
        self.challenge_id = "ahoi-local-passkey-challenge-v1"

    def increment(self, name: str) -> int:
        with self._counter_lock:
            self._counters[name] = self._counters.get(name, 0) + 1
            return self._counters[name]

    def reset(self) -> None:
        with self._counter_lock:
            self._counters.clear()
        self.receipts.reset()

    def manifest(self) -> Mapping[str, object]:
        first = self.urls["firstPartyHttpsUrl"]
        third = self.urls["thirdPartyHttpsUrl"]
        media = self.urls["mediaHttpsUrl"]
        return {
            "schemaVersion": 1,
            "loopbackOnly": True,
            "tlsValidationMustRemainEnabled": True,
            "urls": dict(self.urls),
            "payloads": {
                "rangeDownload": {
                    "url": first + "/download/deterministic.bin",
                    "bytes": len(DOWNLOAD_BYTES),
                    "sha256": DOWNLOAD_SHA256,
                    "supportsRange": True,
                },
                "syntheticPdf": {
                    "url": first + "/document/synthetic.pdf",
                    "bytes": len(PDF_BYTES),
                    "sha256": PDF_SHA256,
                    "supportsRange": True,
                },
                "largeRangeZip": {
                    "url": first + "/download/large-range.zip",
                    "bytes": len(LARGE_ZIP_BYTES),
                    "sha256": LARGE_ZIP_SHA256,
                    "supportsRange": True,
                    "throttled": True,
                },
                "disconnectResumeZip": {
                    "url": first + "/download/disconnect-once.zip",
                    "bytes": len(LARGE_ZIP_BYTES),
                    "sha256": LARGE_ZIP_SHA256,
                    "supportsRange": True,
                    "intentionalDisconnectsBeforeFirstFullResponse": True,
                },
                "harmlessWarning": {
                    "url": first + "/download/harmless-warning.exe",
                    "bytes": len(WARNING_BYTES),
                    "sha256": WARNING_SHA256,
                    "containsExecutableCode": False,
                },
                "h264AacMp4": {
                    "url": media + "/media/sample.mp4",
                    "bytes": len(MEDIA_BYTES),
                    "sha256": MEDIA_SHA256,
                    "codecs": ["H.264", "AAC"],
                },
            },
            "journeys": {
                "downloadUploadDnD": first + "/download-upload",
                "threePaneSplitDnD": first + "/split",
                "redirectPopup": first + "/navigation",
                "syntheticOAuth": first + "/oauth/authorize",
                "simulatedPasskey": first + "/passkey",
                "mediaMsePip": media + "/media",
                "webrtcAndCapture": first + "/webrtc",
                "permissions": first + "/permissions",
                "cookiesChipsPrivacy": first + "/privacy",
                "storageCacheServiceWorker": first + "/storage",
                "headersCspCors": first + "/developer",
                "developerInjection": first + "/injection",
                "syntheticLogin": first + "/login",
                "safeCustomProtocol": "ahoi-e2e-safe://open/fixture",
            },
            "boundaries": {
                "passkey": "local simulation only; platform WebAuthn remains ASSISTED_E2E",
                "webrtc": "loopback peer only; real conferencing remains ASSISTED_E2E",
                "media": "fixture capability only; licensed codec/legal acceptance remains external",
                "customProtocol": "requires the separately consented, fixture-only macOS handler; arbitrary URLs are rejected",
            },
            "receipts": first + "/__fixture/receipts",
        }


class FixtureHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(
        self,
        server_address: Tuple[str, int],
        *,
        role: str,
        context: FixtureContext,
    ) -> None:
        self.role = role
        self.fixture_context = context
        super().__init__(server_address, FixtureRequestHandler, bind_and_activate=False)
        self.server_bind()
        self.server_activate()

    def server_bind(self) -> None:
        socketserver.TCPServer.server_bind(self)


class FixtureRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "AhoiLocalE2E/1"

    @property
    def fixture_server(self) -> FixtureHTTPServer:
        return self.server  # type: ignore[return-value]

    @property
    def context(self) -> FixtureContext:
        return self.fixture_server.fixture_context

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def _headers_for_receipt(self) -> Mapping[str, str]:
        return {key.lower(): value for key, value in self.headers.items()}

    def _send(
        self,
        status: int,
        body: bytes = b"",
        *,
        content_type: str = "text/html; charset=utf-8",
        headers: Sequence[Tuple[str, str]] = (),
        facts: Optional[Mapping[str, object]] = None,
        send_body: bool = True,
    ) -> None:
        self.context.receipts.record(
            role=self.fixture_server.role,
            method=self.command,
            target=self.path,
            status=status,
            headers=self._headers_for_receipt(),
            facts=facts,
        )
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if send_body and self.command != "HEAD" and body:
            self.wfile.write(body)

    def _json(
        self,
        status: int,
        value: object,
        *,
        headers: Sequence[Tuple[str, str]] = (),
        facts: Optional[Mapping[str, object]] = None,
    ) -> None:
        self._send(
            status,
            _json_bytes(value),
            content_type="application/json; charset=utf-8",
            headers=headers,
            facts=facts,
        )

    def _read_body(self) -> Optional[bytes]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "invalid Content-Length"})
            return None
        if length < 0 or length > MAX_UPLOAD_BYTES:
            self._json(
                HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                {"error": "body exceeds fixture limit", "maxBytes": MAX_UPLOAD_BYTES},
                facts={"declaredBytes": max(length, 0)},
            )
            return None
        return self.rfile.read(length)

    def _asset(
        self,
        payload: bytes,
        content_type: str,
        filename: str,
        *,
        attachment: bool,
        throttle_seconds: float = 0,
        disconnect_after_bytes: Optional[int] = None,
    ) -> None:
        range_value = self.headers.get("Range", "")
        try:
            selected = _parse_range(range_value, len(payload))
        except ValueError:
            self._send(
                HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE,
                b"",
                content_type=content_type,
                headers=(("Content-Range", "bytes */%d" % len(payload)),),
                facts={"payloadSha256": hashlib.sha256(payload).hexdigest()},
            )
            return
        start, end = selected or (0, len(payload) - 1)
        response = payload[start : end + 1]
        status = HTTPStatus.PARTIAL_CONTENT if selected else HTTPStatus.OK
        headers: List[Tuple[str, str]] = [
            ("Accept-Ranges", "bytes"),
            ("ETag", '"sha256-%s"' % hashlib.sha256(payload).hexdigest()),
            ("Cache-Control", "public, max-age=3600, immutable"),
        ]
        if attachment:
            headers.append(("Content-Disposition", 'attachment; filename="%s"' % filename))
        if selected:
            headers.append(("Content-Range", "bytes %d-%d/%d" % (start, end, len(payload))))
        should_disconnect = (
            disconnect_after_bytes is not None
            and not selected
            and self.command != "HEAD"
            and 0 < disconnect_after_bytes < len(response)
        )
        planned_bytes = disconnect_after_bytes if should_disconnect else len(response)
        self.context.receipts.record(
            role=self.fixture_server.role,
            method=self.command,
            target=self.path,
            status=status,
            headers=self._headers_for_receipt(),
            facts={
                "payloadSha256": hashlib.sha256(payload).hexdigest(),
                "payloadBytes": len(payload),
                "declaredResponseBytes": len(response),
                "plannedBytesBeforeDisconnect": planned_bytes,
                "intentionalDisconnect": should_disconnect,
                "rangeStart": start if selected else None,
                "rangeEnd": end if selected else None,
                "throttled": throttle_seconds > 0,
            },
        )
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(response)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command == "HEAD":
            return
        limit = int(planned_bytes)
        try:
            for offset in range(0, limit, ASSET_CHUNK_BYTES):
                self.wfile.write(response[offset : min(offset + ASSET_CHUNK_BYTES, limit)])
                self.wfile.flush()
                if throttle_seconds > 0 and offset + ASSET_CHUNK_BYTES < limit:
                    time.sleep(throttle_seconds)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            self.close_connection = True
            return
        if should_disconnect:
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def do_HEAD(self) -> None:
        split = urlsplit(self.path)
        if split.path == "/download/deterministic.bin":
            self._asset(DOWNLOAD_BYTES, "application/octet-stream", "ahoi-range.bin", attachment=True)
        elif split.path == "/download/large-range.zip":
            self._asset(
                LARGE_ZIP_BYTES,
                "application/zip",
                "ahoi-large-range.zip",
                attachment=True,
                throttle_seconds=LARGE_ZIP_THROTTLE_SECONDS,
            )
        elif split.path == "/download/disconnect-once.zip":
            self._asset(
                LARGE_ZIP_BYTES,
                "application/zip",
                "ahoi-disconnect-resume.zip",
                attachment=True,
            )
        elif split.path == "/document/synthetic.pdf":
            self._asset(PDF_BYTES, "application/pdf", "ahoi-synthetic.pdf", attachment=False)
        elif split.path == "/media/sample.mp4":
            self._asset(MEDIA_BYTES, "video/mp4", "ahoi-h264-aac.mp4", attachment=False)
        else:
            self._send(HTTPStatus.NOT_FOUND, b"", send_body=False)

    def do_OPTIONS(self) -> None:
        split = urlsplit(self.path)
        if split.path == "/cors/allow":
            self._send(
                HTTPStatus.NO_CONTENT,
                b"",
                content_type="text/plain",
                headers=(
                    ("Access-Control-Allow-Origin", self.context.urls["firstPartyHttpsUrl"]),
                    ("Access-Control-Allow-Methods", "GET, OPTIONS"),
                    ("Access-Control-Allow-Headers", "X-Ahoi-Test"),
                    ("Access-Control-Max-Age", "60"),
                    ("Vary", "Origin"),
                ),
                facts={"corsControl": "allow"},
            )
            return
        self._send(HTTPStatus.NO_CONTENT, b"", content_type="text/plain")

    def do_GET(self) -> None:
        split = urlsplit(self.path)
        route = split.path
        urls = self.context.urls
        page_routes = {
            "/": pages.index,
            "/download-upload": pages.download_upload,
            "/split": pages.split,
            "/navigation": pages.navigation,
            "/popup": pages.popup,
            "/passkey": pages.passkey,
            "/media": pages.media,
            "/webrtc": pages.webrtc,
            "/permissions": pages.permissions,
            "/privacy": pages.privacy,
            "/storage": pages.storage,
            "/developer": pages.developer,
            "/injection": pages.injection,
            "/login": pages.login,
        }
        if route in page_routes:
            self._send(HTTPStatus.OK, page_routes[route](urls), headers=(("Cache-Control", "no-store"),))
            return
        if route.startswith("/pane/") and route.count("/") == 2:
            self._send(HTTPStatus.OK, pages.pane(route.rsplit("/", 1)[1], urls), headers=(("Cache-Control", "no-store"),))
            return
        if route == "/__fixture/health":
            self._json(HTTPStatus.OK, {"ready": True, "role": self.fixture_server.role})
            return
        if route == "/__fixture/manifest":
            self._json(HTTPStatus.OK, self.context.manifest())
            return
        if route == "/__fixture/receipts":
            snapshot = self.context.receipts.snapshot()
            self._json(HTTPStatus.OK, {"schemaVersion": 1, "receipts": snapshot})
            return
        if route == "/download/deterministic.bin":
            self._asset(DOWNLOAD_BYTES, "application/octet-stream", "ahoi-range.bin", attachment=True)
            return
        if route == "/download/large-range.zip":
            self._asset(
                LARGE_ZIP_BYTES,
                "application/zip",
                "ahoi-large-range.zip",
                attachment=True,
                throttle_seconds=LARGE_ZIP_THROTTLE_SECONDS,
            )
            return
        if route == "/download/disconnect-once.zip":
            disconnect = None
            if not self.headers.get("Range") and self.context.increment("disconnect-once") == 1:
                disconnect = DISCONNECT_AFTER_BYTES
            self._asset(
                LARGE_ZIP_BYTES,
                "application/zip",
                "ahoi-disconnect-resume.zip",
                attachment=True,
                throttle_seconds=LARGE_ZIP_THROTTLE_SECONDS,
                disconnect_after_bytes=disconnect,
            )
            return
        if route == "/document/synthetic.pdf":
            self._asset(PDF_BYTES, "application/pdf", "ahoi-synthetic.pdf", attachment=False)
            return
        if route == "/download/harmless-warning.exe":
            self._asset(WARNING_BYTES, "application/x-msdownload", "ahoi-harmless-warning.exe", attachment=True)
            return
        if route == "/media/sample.mp4":
            self._asset(MEDIA_BYTES, "video/mp4", "ahoi-h264-aac.mp4", attachment=False)
            return
        if route == "/redirect/same":
            self._send(
                HTTPStatus.FOUND,
                b"",
                headers=(("Location", "/popup?from=same"), ("Cache-Control", "no-store")),
                facts={"redirectKind": "same-origin"},
            )
            return
        if route == "/redirect/cross":
            self._send(
                HTTPStatus.FOUND,
                b"",
                headers=(("Location", urls["thirdPartyHttpsUrl"] + "/popup?from=cross"), ("Cache-Control", "no-store")),
                facts={"redirectKind": "cross-origin"},
            )
            return
        if route == "/oauth/authorize":
            state = parse_qs(split.query).get("state", ["public-test-state"])[0][:200]
            self._send(
                HTTPStatus.OK,
                pages.oauth_authorize(urls, state),
                headers=(("Cache-Control", "no-store"),),
                facts={"oauthSimulation": True},
            )
            return
        if route == "/oauth/callback":
            decision = parse_qs(split.query).get("result", ["unknown"])[0][:40]
            self._send(
                HTTPStatus.OK,
                pages.oauth_callback(urls, decision),
                headers=(("Cache-Control", "no-store"),),
                facts={"oauthSimulation": True, "decision": decision},
            )
            return
        if route == "/passkey/challenge":
            kind = parse_qs(split.query).get("kind", ["authenticate"])[0]
            self._json(
                HTTPStatus.OK,
                {
                    "challengeId": self.context.challenge_id,
                    "kind": kind if kind in {"register", "authenticate"} else "authenticate",
                    "rpId": "first-party.localhost",
                    "simulated": True,
                    "platformWebAuthnPerformed": False,
                },
                facts={"passkeySimulation": True},
            )
            return
        if route == "/cookies/set":
            self._send(
                HTTPStatus.OK,
                pages.document("First-party cookies set", "<p id='cookie-set'>Synthetic first-party cookies set.</p>", urls),
                headers=(
                    ("Set-Cookie", "ahoi_first=synthetic; Path=/; Secure; SameSite=Lax"),
                    ("Set-Cookie", "ahoi_strict=synthetic; Path=/; Secure; SameSite=Strict"),
                    ("Set-Cookie", "ahoi_http_only=synthetic; Path=/; Secure; HttpOnly; SameSite=Lax"),
                    ("Cache-Control", "no-store"),
                ),
                facts={"cookieAttributes": ["Secure", "HttpOnly", "SameSite=Lax", "SameSite=Strict"]},
            )
            return
        if route == "/cookies/third-party":
            names = _cookie_names(self.headers.get("Cookie", ""))
            body = pages.document(
                "Third-party CHIPS control",
                "<p id='chips-control'>A Secure, SameSite=None, Partitioned cookie was offered. Seen cookie names: %s</p>"
                % html.escape(", ".join(names) or "none"),
                urls,
            )
            self._send(
                HTTPStatus.OK,
                body,
                headers=(("Set-Cookie", "ahoi_partitioned=synthetic; Path=/; Secure; SameSite=None; Partitioned"), ("Cache-Control", "no-store")),
                facts={"cookieNames": names, "partitionedCookieOffered": True},
            )
            return
        if route == "/privacy/echo":
            summary = query_key_summary(self.path)
            self._json(
                HTTPStatus.OK,
                {
                    "gpc": self.headers.get("Sec-GPC") == "1",
                    "referrerWithoutQuery": _safe_referrer(self.headers.get("Referer", "")),
                    **summary,
                },
                headers=(("Cache-Control", "no-store"),),
                facts={"privacyEcho": True},
            )
            return
        if route == "/counter/storage":
            value = self.context.increment("storage")
            self._json(HTTPStatus.OK, {"counter": "storage", "value": value}, facts={"counter": "storage", "value": value})
            return
        if route == "/assets/v1/data.json":
            count = self.context.increment("asset-v1")
            self._json(
                HTTPStatus.OK,
                {"assetVersion": "v1", "content": "deterministic fixture asset", "accessCount": count},
                headers=(("Cache-Control", "public, max-age=31536000, immutable"), ("ETag", '"ahoi-asset-v1"')),
                facts={"assetVersion": "v1", "accessCount": count},
            )
            return
        if route == "/service-worker.js":
            script = (
                "const CACHE='ahoi-e2e-v1';const ASSET='/assets/v1/data.json';"
                "self.addEventListener('install',e=>e.waitUntil(caches.open(CACHE).then(c=>c.add(ASSET))));"
                "self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));"
                "self.addEventListener('fetch',e=>{if(new URL(e.request.url).pathname===ASSET)e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)))})\n"
            ).encode("utf-8")
            self._send(
                HTTPStatus.OK,
                script,
                content_type="text/javascript; charset=utf-8",
                headers=(("Cache-Control", "no-cache"), ("Service-Worker-Allowed", "/")),
                facts={"serviceWorkerVersion": "v1"},
            )
            return
        if route == "/headers/echo":
            lowered = {key.lower(): value for key, value in self.headers.items()}
            self._json(
                HTTPStatus.OK,
                {
                    "allowedValues": {"x-ahoi-test": lowered.get("x-ahoi-test")},
                    "presenceOnly": {
                        "authorization": "authorization" in lowered,
                        "cookie": "cookie" in lowered,
                        "origin": "origin" in lowered,
                        "referer": "referer" in lowered,
                    },
                    "redacted": ["authorization", "cookie"],
                },
                headers=(("Cache-Control", "no-store"), ("X-Ahoi-Response", "public-fixture-value")),
                facts={"echoedHeaderNames": ["x-ahoi-test"] if "x-ahoi-test" in lowered else []},
            )
            return
        if route == "/csp/strict":
            body = pages.strict_csp(urls)
            self._send(
                HTTPStatus.OK,
                body,
                headers=(("Content-Security-Policy", "default-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'self'"), ("Cache-Control", "no-store")),
                facts={"cspControl": "strict-self"},
            )
            return
        if route in {"/cors/allow", "/cors/deny"}:
            extra: Sequence[Tuple[str, str]] = ()
            control = "deny"
            if route.endswith("allow"):
                control = "allow"
                extra = (("Access-Control-Allow-Origin", urls["firstPartyHttpsUrl"]), ("Vary", "Origin"))
            self._json(
                HTTPStatus.OK,
                {"corsControl": control, "role": self.fixture_server.role},
                headers=extra,
                facts={"corsControl": control},
            )
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "fixture route not found", "path": route})

    def do_POST(self) -> None:
        route = urlsplit(self.path).path
        body = self._read_body()
        if body is None:
            return
        if route == "/upload":
            filename = _safe_filename(self.headers.get("X-Ahoi-Filename", "unnamed-upload.bin"))
            result = {
                "accepted": True,
                "bytes": len(body),
                "sha256": hashlib.sha256(body).hexdigest(),
                "filename": filename,
                "contentType": self.headers.get("Content-Type", "application/octet-stream").split(";", 1)[0],
                "stored": False,
            }
            receipt_facts = {
                "accepted": True,
                "bytes": len(body),
                "sha256": result["sha256"],
                "filenameExtension": Path(filename).suffix.lower()[:20],
                "stored": False,
            }
            self._json(HTTPStatus.OK, result, facts=receipt_facts)
            return
        if route == "/oauth/approve":
            values = parse_qs(body.decode("utf-8", "replace"))
            decision = "allowed" if values.get("decision", [""])[0] == "allow" else "denied"
            state = values.get("state", ["public-test-state"])[0][:200]
            location = "/oauth/callback?" + urlencode(
                {"result": decision, "code": "local-synthetic-code" if decision == "allowed" else "", "state": state}
            )
            self._send(
                HTTPStatus.SEE_OTHER,
                b"",
                headers=(("Location", location), ("Cache-Control", "no-store")),
                facts={"oauthSimulation": True, "decision": decision, "bodyBytes": len(body)},
            )
            return
        if route == "/passkey/verify":
            try:
                value = json.loads(body.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                value = {}
            accepted = (
                isinstance(value, dict)
                and value.get("challengeId") == self.context.challenge_id
                and value.get("credentialId") == "synthetic-local-credential"
                and value.get("kind") in {"register", "authenticate"}
            )
            self._json(
                HTTPStatus.OK if accepted else HTTPStatus.BAD_REQUEST,
                {"accepted": accepted, "simulated": True, "platformWebAuthnPerformed": False},
                facts={"passkeySimulation": True, "accepted": accepted, "bodyBytes": len(body)},
            )
            return
        if route == "/login":
            values = parse_qs(body.decode("utf-8", "replace"))
            accepted = hmac.compare_digest(values.get("username", [""])[0], SYNTHETIC_USERNAME) and hmac.compare_digest(
                values.get("password", [""])[0], SYNTHETIC_PASSWORD
            )
            result = "Synthetic login accepted" if accepted else "Synthetic login rejected"
            self._send(
                HTTPStatus.OK,
                pages.login(self.context.urls, result),
                headers=(("Cache-Control", "no-store"),),
                facts={"syntheticLoginAccepted": accepted, "bodyBytes": len(body)},
            )
            return
        if route == "/csp/report":
            self._json(
                HTTPStatus.OK,
                {"accepted": True, "bodyStored": False},
                facts={"cspReportReceived": True, "bodyBytes": len(body)},
            )
            return
        if route == "/__fixture/reset":
            self.context.reset()
            self._json(HTTPStatus.OK, {"reset": True})
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "fixture POST route not found", "path": route})


class FixtureCluster:
    """Three distinct HTTPS origins sharing one generated localhost leaf."""

    def __init__(
        self,
        *,
        runtime_directory: Path,
        leaf_certificate: Path,
        leaf_private_key: Path,
        first_port: int = 0,
        third_port: int = 0,
        media_port: int = 0,
    ) -> None:
        self.runtime_directory = runtime_directory
        self.context = FixtureContext(runtime_directory)
        self._first = FixtureHTTPServer((LOOPBACK_HOST, first_port), role="first-party", context=self.context)
        self._third = FixtureHTTPServer((LOOPBACK_HOST, third_port), role="third-party", context=self.context)
        self._media = FixtureHTTPServer((LOOPBACK_HOST, media_port), role="media", context=self.context)
        self._servers = (self._first, self._third, self._media)
        self.context.urls.update(
            {
                "firstPartyHttpsUrl": "https://%s:%d" % (FIRST_HOST_NAME, self._first.server_address[1]),
                "thirdPartyHttpsUrl": "https://%s:%d" % (THIRD_HOST_NAME, self._third.server_address[1]),
                "mediaHttpsUrl": "https://%s:%d" % (MEDIA_HOST_NAME, self._media.server_address[1]),
            }
        )
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.minimum_version = ssl.TLSVersion.TLSv1_2
        tls.load_cert_chain(str(leaf_certificate), str(leaf_private_key))
        for server in self._servers:
            server.socket = tls.wrap_socket(server.socket, server_side=True)
        self._threads: List[threading.Thread] = []

    def start(self) -> "FixtureCluster":
        if self._threads:
            return self
        for server in self._servers:
            thread = threading.Thread(
                target=server.serve_forever,
                name="ahoi-e2e-%s" % server.role,
                daemon=True,
            )
            thread.start()
            self._threads.append(thread)
        return self

    def stop(self) -> None:
        if not self._threads:
            for server in self._servers:
                server.server_close()
            return
        shutdowns = [threading.Thread(target=server.shutdown, daemon=True) for server in self._servers]
        for thread in shutdowns:
            thread.start()
        for thread in shutdowns:
            thread.join(timeout=5)
        for server in self._servers:
            server.server_close()
        for thread in self._threads:
            thread.join(timeout=5)
        self._threads.clear()

    def describe(self, *, ca_certificate: Path, leaf_certificate: Path) -> Mapping[str, object]:
        return {
            "schemaVersion": 1,
            "instanceId": self.context.instance_id,
            "pid": None,
            "runtimeDirectory": str(self.runtime_directory),
            "caCertificate": str(ca_certificate),
            "leafCertificate": str(leaf_certificate),
            "receiptsFile": str(self.context.receipts.path),
            **self.context.urls,
            "manifestUrl": self.context.urls["firstPartyHttpsUrl"] + "/__fixture/manifest",
            "trustInstalled": False,
            "note": "Local synthetic data only; TLS validation must remain enabled.",
        }

    def __enter__(self) -> "FixtureCluster":
        return self.start()

    def __exit__(self, *_args: object) -> None:
        self.stop()
