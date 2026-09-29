"""GET routes for redirects, sign-in, cookies, privacy, storage and headers."""

from __future__ import annotations

import html
from http import HTTPStatus
from typing import Mapping, Sequence, Tuple
from urllib.parse import SplitResult, parse_qs

import pages
from receipts import query_key_summary
from server_support import (
    PRIVATE_DATA_MARKER_COOKIE_NAMES,
    _cookie_names,
    _safe_referrer,
)


class SessionRoutesMixin:
    """Identity and site-data routes of FixtureRequestHandler."""

    def _get_identity_route(self, route: str, split: SplitResult, urls: Mapping[str, str]) -> bool:
        """Redirects, OAuth and passkey challenges. Returns whether the route was handled."""
        if route == "/redirect/same":
            self._send(
                HTTPStatus.FOUND,
                b"",
                headers=(("Location", "/popup?from=same"), ("Cache-Control", "no-store")),
                facts={"redirectKind": "same-origin"},
            )
            return True
        if route == "/redirect/cross":
            self._send(
                HTTPStatus.FOUND,
                b"",
                headers=(("Location", urls["thirdPartyHttpsUrl"] + "/popup?from=cross"), ("Cache-Control", "no-store")),
                facts={"redirectKind": "cross-origin"},
            )
            return True
        if route == "/redirect/dns-failure":
            self._send(
                HTTPStatus.FOUND,
                b"",
                headers=(
                    ("Location", pages.DNS_REDIRECT_FAILURE_URL),
                    ("Cache-Control", "no-store"),
                ),
                facts={"redirectKind": "dns-failure"},
            )
            return True
        if route == "/oauth/authorize":
            state = parse_qs(split.query).get("state", ["public-test-state"])[0][:200]
            self._send(
                HTTPStatus.OK,
                pages.oauth_authorize(urls, state),
                headers=(("Cache-Control", "no-store"),),
                facts={"oauthSimulation": True},
            )
            return True
        if route == "/oauth/callback":
            decision = parse_qs(split.query).get("result", ["unknown"])[0][:40]
            self._send(
                HTTPStatus.OK,
                pages.oauth_callback(urls, decision),
                headers=(("Cache-Control", "no-store"),),
                facts={"oauthSimulation": True, "decision": decision},
            )
            return True
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
            return True
        return False

    def _get_site_data_route(self, route: str, split: SplitResult, urls: Mapping[str, str]) -> bool:
        """Cookies, privacy markers, storage, service worker, headers, CSP and CORS. Returns whether the route was handled."""
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
            return True
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
            return True
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
            return True
        if route == "/privacy/marker/inspect":
            cookie_names = set(_cookie_names(self.headers.get("Cookie", "")))
            markers = {
                kind: cookie_name in cookie_names
                for kind, cookie_name in PRIVATE_DATA_MARKER_COOKIE_NAMES.items()
            }
            self._json(
                HTTPStatus.OK,
                {"markers": markers, "valuesExposed": False},
                headers=(("Cache-Control", "no-store"),),
                facts={
                    "privateDataControl": "inspect",
                    "markerKindsPresent": sorted(
                        kind for kind, present in markers.items() if present
                    ),
                    "valuesRetained": False,
                },
            )
            return True
        if route == "/counter/storage":
            value = self.context.increment("storage")
            self._json(HTTPStatus.OK, {"counter": "storage", "value": value}, facts={"counter": "storage", "value": value})
            return True
        if route == "/assets/v1/data.json":
            count = self.context.increment("asset-v1")
            self._json(
                HTTPStatus.OK,
                {"assetVersion": "v1", "content": "deterministic fixture asset", "accessCount": count},
                headers=(("Cache-Control", "public, max-age=31536000, immutable"), ("ETag", '"ahoi-asset-v1"')),
                facts={"assetVersion": "v1", "accessCount": count},
            )
            return True
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
            return True
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
            return True
        if route == "/csp/strict":
            body = pages.strict_csp(urls)
            self._send(
                HTTPStatus.OK,
                body,
                headers=(("Content-Security-Policy", "default-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'self'"), ("Cache-Control", "no-store")),
                facts={"cspControl": "strict-self"},
            )
            return True
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
            return True
        return False
