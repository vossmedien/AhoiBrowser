#!/usr/bin/env python3
"""Privacy-safe machine-readable receipts for the local E2E fixture."""

from __future__ import annotations

import json
import threading
import time
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence
from urllib.parse import parse_qsl, urlsplit


TRACKING_KEYS = frozenset(
    {
        "fbclid",
        "gclid",
        "mc_cid",
        "mc_eid",
        "utm_campaign",
        "utm_content",
        "utm_medium",
        "utm_source",
        "utm_term",
    }
)
KNOWN_PATHS = frozenset(
    {
        "/",
        "/__fixture/health",
        "/__fixture/manifest",
        "/__fixture/receipts",
        "/__fixture/reset",
        "/assets/v1/data.json",
        "/cookies/set",
        "/cookies/third-party",
        "/cors/allow",
        "/cors/deny",
        "/counter/storage",
        "/csp/report",
        "/csp/strict",
        "/developer",
        "/download-upload",
        "/download/deterministic.bin",
        "/download/harmless-warning.exe",
        "/headers/echo",
        "/injection",
        "/login",
        "/media",
        "/media/sample.mp4",
        "/navigation",
        "/oauth/approve",
        "/oauth/authorize",
        "/oauth/callback",
        "/passkey",
        "/passkey/challenge",
        "/passkey/verify",
        "/permissions",
        "/popup",
        "/privacy",
        "/privacy/echo",
        "/redirect/cross",
        "/redirect/same",
        "/service-worker.js",
        "/split",
        "/storage",
        "/upload",
        "/webrtc",
    }
)


def sanitized_path(target: str) -> str:
    path = urlsplit(target).path
    if path in KNOWN_PATHS:
        return path
    if path.startswith("/pane/"):
        return "/pane/:id"
    return "/__unmatched__"


def query_key_summary(target: str) -> Mapping[str, object]:
    """Return names and tracking classification without retaining values."""

    keys = sorted({key for key, _value in parse_qsl(urlsplit(target).query)})
    tracking = sorted(set(keys) & TRACKING_KEYS)
    return {
        "queryKeys": keys,
        "trackingParameterKeys": tracking,
        "trackingParametersPresent": bool(tracking),
    }


class ReceiptStore:
    """Append-only receipt log with a bounded in-memory readback window."""

    def __init__(self, path: Path, *, run_id: str, max_entries: int = 1000) -> None:
        self.path = path
        self.run_id = run_id
        self.max_entries = max_entries
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._next_id = 1
        self._entries: List[Mapping[str, object]] = []

    def record(
        self,
        *,
        role: str,
        method: str,
        target: str,
        status: int,
        headers: Mapping[str, str],
        facts: Optional[Mapping[str, object]] = None,
    ) -> Mapping[str, object]:
        lowered = {key.lower(): value for key, value in headers.items()}
        entry: Dict[str, object] = {
            "schemaVersion": 1,
            "fixtureRunId": self.run_id,
            "receiptId": 0,
            "observedAtUnixMs": int(time.time() * 1000),
            "role": role,
            "method": method,
            "path": sanitized_path(target),
            "status": status,
            "requestSignals": {
                "authorizationPresent": "authorization" in lowered,
                "cookiePresent": "cookie" in lowered,
                "gpc": lowered.get("sec-gpc") == "1",
                "originPresent": "origin" in lowered,
                "refererPresent": "referer" in lowered,
            },
            **query_key_summary(target),
        }
        if facts:
            entry["facts"] = dict(facts)
        with self._lock:
            entry["receiptId"] = self._next_id
            self._next_id += 1
            self._entries.append(entry)
            del self._entries[:-self.max_entries]
            with self.path.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(entry, sort_keys=True) + "\n")
            return dict(entry)

    def snapshot(self) -> Sequence[Mapping[str, object]]:
        with self._lock:
            return [dict(entry) for entry in self._entries]

    def reset(self) -> None:
        with self._lock:
            self._entries.clear()
            self.path.write_text("", encoding="utf-8")
