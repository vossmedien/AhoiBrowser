"""GET routes for failure injection, downloads and media."""

from __future__ import annotations

import hashlib
from http import HTTPStatus
from typing import Mapping
from urllib.parse import SplitResult

import pages
from payloads import (
    DISCONNECT_AFTER_BYTES,
    DOWNLOAD_BYTES,
    LARGE_ZIP_BYTES,
    LARGE_ZIP_THROTTLE_SECONDS,
    MEDIA_BYTES,
    PDF_BYTES,
    WARNING_BYTES,
)
from server_support import (
    SLOW_RESOURCE_BYTES,
    SLOW_RESOURCE_THROTTLE_SECONDS,
    _http_recovery_query,
)


class TransferRoutesMixin:
    """Failure recovery, download, PDF and media routes of FixtureRequestHandler."""

    def _get_failure_route(self, route: str, split: SplitResult, urls: Mapping[str, str]) -> bool:
        """Subframe failures and recover-once pages. Returns whether the route was handled."""
        if route in {"/failure/subframe-404", "/failure/subframe-500"}:
            status = (
                HTTPStatus.NOT_FOUND
                if route.endswith("404")
                else HTTPStatus.INTERNAL_SERVER_ERROR
            )
            body = pages.document(
                "Subframe HTTP %d" % status,
                "<p id='subframe-http-%d-loaded'>Subframe HTTP %d loaded.</p>"
                % (status, status),
                urls,
            )
            self._send(
                status,
                body,
                headers=(("Cache-Control", "no-store"),),
                facts={"subframeFailureStatus": status},
            )
            return True
        if route == "/failure/recover-once":
            parameters = _http_recovery_query(split.query)
            if parameters is None:
                self._json(
                    HTTPStatus.BAD_REQUEST,
                    {"error": "invalid recover-once query"},
                    headers=(("Cache-Control", "no-store"),),
                    facts={"httpRecoveryQueryValid": False},
                )
                return True
            requested_status, token = parameters
            token_digest = hashlib.sha256(token.encode("ascii")).hexdigest()
            attempt = self.context.increment(
                "http-recover-once:%d:%s" % (requested_status, token_digest)
            )
            complete = attempt >= 2
            if complete:
                body = pages.document(
                    "HTTP recovery complete",
                    "<p id='http-recovery-ready'>HTTP recovery complete</p>",
                    urls,
                )
                response_status = HTTPStatus.OK
            else:
                body = pages.document(
                    "Intentional HTTP %d failure" % requested_status,
                    "<p id='http-recovery-pending'>Reload this page to complete HTTP recovery.</p>",
                    urls,
                )
                response_status = requested_status
            self._send(
                response_status,
                body,
                headers=(("Cache-Control", "no-store"),),
                facts={
                    "httpRecoveryAttempt": attempt,
                    "httpRecoveryComplete": complete,
                    "requestedStatus": requested_status,
                },
            )
            return True
        return False

    def _get_transfer_route(self, route: str, split: SplitResult, urls: Mapping[str, str]) -> bool:
        """Downloads, slow resources, PDF and media. Returns whether the route was handled."""
        if route == "/download/deterministic.bin":
            self._asset(DOWNLOAD_BYTES, "application/octet-stream", "ahoi-range.bin", attachment=True)
            return True
        if route == "/slow-resource.svg":
            self._asset(
                SLOW_RESOURCE_BYTES,
                "image/svg+xml",
                "ahoi-slow-resource.svg",
                attachment=False,
                throttle_seconds=SLOW_RESOURCE_THROTTLE_SECONDS,
                cache_control="no-store",
            )
            return True
        if route == "/download/large-range.zip":
            self._asset(
                LARGE_ZIP_BYTES,
                "application/zip",
                "ahoi-large-range.zip",
                attachment=True,
                throttle_seconds=LARGE_ZIP_THROTTLE_SECONDS,
                cache_control="no-store",
            )
            return True
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
            return True
        if route == "/document/synthetic.pdf":
            self._asset(PDF_BYTES, "application/pdf", "ahoi-synthetic.pdf", attachment=False)
            return True
        if route == "/download/harmless-warning.exe":
            self._asset(WARNING_BYTES, "application/x-msdownload", "ahoi-harmless-warning.exe", attachment=True)
            return True
        if route == "/media/sample.mp4":
            self._asset(MEDIA_BYTES, "video/mp4", "ahoi-h264-aac.mp4", attachment=False)
            return True
        return False
