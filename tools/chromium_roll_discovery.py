"""Official-source discovery for the non-mutating Chromium roll CLI."""

from __future__ import annotations

import datetime as dt
import json
import pathlib
import re
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

from verify_chromium_pin import (
    OFFICIAL_SOURCE,
    exact_footer_values,
    is_eligible_release,
    require_sha1,
    validate_config,
    verify_commit_metadata,
    verify_release,
    verify_remote_refs,
    verify_version_file,
)


VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)$")
COMMIT_POSITION_RE = re.compile(r"^refs/branch-heads/(\d+)@\{#(\d+)\}$")
BRANCHED_FROM_RE = re.compile(
    r"^([0-9a-f]{40})-refs/heads/main@\{#(\d+)\}$"
)
TIMESTAMP_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?Z$"
)
RELEASE_API = (
    "https://versionhistory.googleapis.com/v1/chrome/platforms/mac_arm64/"
    "channels/stable/versions/all/releases?filter=endtime%3Dnone&"
    "order_by=version%20desc"
)
DASH_API = (
    "https://chromiumdash.appspot.com/fetch_releases?channel=Stable&"
    "platform=Mac&num=100&offset=0"
)
GITILES_BASE = "https://chromium.googlesource.com/chromium/src"
MAX_RESPONSE_BYTES = 4 * 1024 * 1024


class DiscoveryError(ValueError):
    """Official metadata did not prove a unique roll candidate."""


class _RejectRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        del req, fp, code, msg, headers, newurl
        return None


def json_payload(raw: bytes, label: str) -> Any:
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise DiscoveryError(f"{label} is not valid UTF-8") from error
    if text.startswith(")]}'"):
        text = text.split("\n", 1)[1] if "\n" in text else ""
    try:
        return json.loads(text)
    except json.JSONDecodeError as error:
        raise DiscoveryError(f"{label} is not valid JSON: {error}") from error


def fixture_json(path: pathlib.Path | None, label: str) -> Any:
    if path is None:
        raise DiscoveryError(f"offline discovery requires --{label.replace('_', '-')}")
    try:
        return json_payload(path.read_bytes(), label)
    except OSError as error:
        raise DiscoveryError(f"could not read {label}: {error}") from error


def fixture_bytes(path: pathlib.Path | None, label: str) -> bytes:
    if path is None:
        raise DiscoveryError(f"offline discovery requires --{label.replace('_', '-')}")
    try:
        return path.read_bytes()
    except OSError as error:
        raise DiscoveryError(f"could not read {label}: {error}") from error


def version_tuple(value: Any) -> tuple[int, int, int, int]:
    match = VERSION_RE.fullmatch(value) if isinstance(value, str) else None
    if match is None:
        raise DiscoveryError(
            f"invalid four-part Chromium version in release feed: {value!r}"
        )
    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def release_records(payload: Any) -> list[dict[str, Any]]:
    if not isinstance(payload, dict) or not isinstance(payload.get("releases"), list):
        raise DiscoveryError("VersionHistory response has no releases array")
    records: list[dict[str, Any]] = []
    for index, record in enumerate(payload["releases"]):
        if not isinstance(record, dict):
            raise DiscoveryError(f"VersionHistory release {index} is not an object")
        version_tuple(record.get("version"))
        records.append(record)
    return records


def select_release(payload: Any) -> str:
    groups: dict[str, list[dict[str, Any]]] = {}
    for record in release_records(payload):
        groups.setdefault(record["version"], []).append(record)
    versions = [
        version
        for version, group in groups.items()
        if any(is_eligible_release(record) for record in group)
    ]
    if not versions:
        raise DiscoveryError(
            "VersionHistory has no active fully rolled, pinnable release"
        )
    version = max(versions, key=version_tuple)
    eligible = [record for record in groups[version] if is_eligible_release(record)]
    if len(eligible) != 1:
        raise DiscoveryError(
            "highest eligible version is ambiguous: "
            f"{version} has {len(eligible)} fully rolled, pinnable records"
        )
    return version


def dash_records(payload: Any) -> list[dict[str, Any]]:
    values = payload.get("releases") if isinstance(payload, dict) else payload
    if not isinstance(values, list):
        raise DiscoveryError("Chromium Dash response is not a release array")
    if any(not isinstance(value, dict) for value in values):
        raise DiscoveryError("Chromium Dash contains a non-object release")
    return values


def one_footer(message: Any, name: str) -> str:
    values = exact_footer_values(message, name)
    if len(values) != 1:
        raise DiscoveryError(f"Gitiles commit must contain exactly one {name} footer")
    return values[0]


def require_object(payload: Any, label: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise DiscoveryError(f"{label} must be a JSON object")
    return payload


def normalize_timestamp(value: str | None) -> str:
    if value is None:
        return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
            "+00:00", "Z"
        )
    if TIMESTAMP_RE.fullmatch(value) is None:
        raise DiscoveryError(
            "retrieved-at must be an ISO-8601 UTC timestamp ending in Z"
        )
    try:
        dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise DiscoveryError("retrieved-at is not a valid timestamp") from error
    return value


def derive_candidate(
    *,
    release_payload: Any,
    dash_payload: Any,
    tag_payload: Any,
    branch_payload: Any,
    commit_payload: Any,
    branch_point_payload: Any,
    version_text: bytes,
    retrieved_at: str | None,
) -> dict[str, Any]:
    version = select_release(release_payload)
    parts = version_tuple(version)
    milestone, branch_head = parts[0], parts[2]
    tag = require_object(tag_payload, "Gitiles tag response")
    branch = require_object(branch_payload, "Gitiles branch response")
    commit = require_object(commit_payload, "Gitiles commit response")
    branch_point_payload = require_object(
        branch_point_payload, "Gitiles branch-point response"
    )
    tag_commit = require_sha1(tag.get("commit"), "Gitiles tag target")
    branch_commit = require_sha1(branch.get("commit"), "Gitiles branch-head target")
    release_commit = require_sha1(commit.get("commit"), "Gitiles release commit")
    if tag_commit != release_commit:
        raise DiscoveryError("Gitiles tag and release commit responses disagree")
    position = COMMIT_POSITION_RE.fullmatch(
        one_footer(commit.get("message"), "Cr-Commit-Position")
    )
    if position is None or int(position.group(1)) != branch_head:
        raise DiscoveryError("release commit is not on the version's branch head")
    branched = BRANCHED_FROM_RE.fullmatch(
        one_footer(commit.get("message"), "Cr-Branched-From")
    )
    if branched is None:
        raise DiscoveryError("release commit has an invalid Cr-Branched-From footer")
    branch_point, branch_position = branched.group(1), int(branched.group(2))
    candidate: dict[str, Any] = {
        "schemaVersion": 1,
        "channel": "Stable",
        "platform": "Mac",
        "milestone": milestone,
        "version": version,
        "tag": f"refs/tags/{version}",
        "commit": release_commit,
        "branchHead": branch_head,
        "branchHeadPosition": int(position.group(2)),
        "branchPoint": branch_point,
        "branchPosition": branch_position,
        "source": OFFICIAL_SOURCE,
        "releaseApi": RELEASE_API,
        "rolloutFraction": 1.0,
        "pinnable": True,
        "verifiedVersionFile": "chrome/VERSION",
        "retrievedAt": normalize_timestamp(retrieved_at),
    }
    matching_dash = [
        record
        for record in dash_records(dash_payload)
        if record.get("version") == version
        and str(record.get("channel", "")).lower() == "stable"
        and str(record.get("platform", "")).lower() == "mac"
    ]
    if len(matching_dash) != 1:
        raise DiscoveryError(
            f"Chromium Dash must contain exactly one Stable Mac record for {version}"
        )
    dash = matching_dash[0]
    hashes = dash.get("hashes")
    if not isinstance(hashes, dict) or hashes.get("chromium") != release_commit:
        raise DiscoveryError("Chromium Dash commit does not match the Gitiles tag")
    if dash.get("milestone") != milestone:
        raise DiscoveryError("Chromium Dash milestone does not match the selected version")
    if dash.get("chromium_main_branch_position") != branch_position:
        raise DiscoveryError("Chromium Dash main branch position does not match Gitiles")
    validate_config(candidate)
    refs = (
        f"{tag_commit}\t{candidate['tag']}\n"
        f"{branch_commit}\trefs/branch-heads/{branch_head}\n"
    )
    verify_remote_refs(candidate, refs)
    verify_commit_metadata(candidate, commit, branch_point_payload)
    verify_version_file(candidate, version_text)
    verify_release(candidate, release_payload)
    return candidate


def validate_official_url(url: str) -> None:
    parsed = urllib.parse.urlparse(url)
    allowed_hosts = {
        "chromiumdash.appspot.com",
        "versionhistory.googleapis.com",
        "chromium.googlesource.com",
    }
    if (
        parsed.scheme != "https"
        or parsed.hostname not in allowed_hosts
        or parsed.username is not None
        or parsed.password is not None
        or parsed.port is not None
        or parsed.fragment
    ):
        raise DiscoveryError(f"refusing non-official discovery URL: {url}")


def fetch(url: str, timeout: int) -> bytes:
    validate_official_url(url)
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "AhoiBrowser-Chromium-Roll/1", "Connection": "close"},
    )
    opener = urllib.request.build_opener(_RejectRedirects())
    try:
        with opener.open(request, timeout=timeout) as response:
            raw = response.read(MAX_RESPONSE_BYTES + 1)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as error:
        raise DiscoveryError(f"official discovery request failed: {error}") from error
    if len(raw) > MAX_RESPONSE_BYTES:
        raise DiscoveryError("official discovery response exceeds the 4 MiB limit")
    return raw


def online_inputs(timeout: int) -> dict[str, Any]:
    if timeout < 1 or timeout > 60:
        raise DiscoveryError("network-timeout must be between 1 and 60 seconds")
    release_payload = json_payload(
        fetch(RELEASE_API + "&page_size=200", timeout), "VersionHistory response"
    )
    version = select_release(release_payload)
    tag_payload = json_payload(
        fetch(f"{GITILES_BASE}/+show/refs/tags/{version}?format=JSON", timeout),
        "Gitiles tag response",
    )
    commit_id = require_sha1(
        require_object(tag_payload, "Gitiles tag response").get("commit"),
        "Gitiles tag target",
    )
    commit_payload = json_payload(
        fetch(f"{GITILES_BASE}/+/{commit_id}?format=JSON", timeout),
        "Gitiles commit response",
    )
    branched = BRANCHED_FROM_RE.fullmatch(
        one_footer(
            require_object(commit_payload, "Gitiles commit response").get("message"),
            "Cr-Branched-From",
        )
    )
    if branched is None:
        raise DiscoveryError("release commit has an invalid Cr-Branched-From footer")
    branch_point = branched.group(1)
    branch_head = version_tuple(version)[2]
    return {
        "release_payload": release_payload,
        "dash_payload": json_payload(fetch(DASH_API, timeout), "Chromium Dash response"),
        "tag_payload": tag_payload,
        "branch_payload": json_payload(
            fetch(
                f"{GITILES_BASE}/+show/refs/branch-heads/{branch_head}?format=JSON",
                timeout,
            ),
            "Gitiles branch response",
        ),
        "commit_payload": commit_payload,
        "branch_point_payload": json_payload(
            fetch(f"{GITILES_BASE}/+/{branch_point}?format=JSON", timeout),
            "Gitiles branch-point response",
        ),
        "version_text": fetch(
            f"{GITILES_BASE}/+/{commit_id}/chrome/VERSION?format=TEXT", timeout
        ),
    }


def offline_inputs(args: Any) -> dict[str, Any]:
    return {
        "release_payload": fixture_json(args.release_json, "release_json"),
        "dash_payload": fixture_json(args.dash_json, "dash_json"),
        "tag_payload": fixture_json(args.tag_ref_json, "tag_ref_json"),
        "branch_payload": fixture_json(args.branch_ref_json, "branch_ref_json"),
        "commit_payload": fixture_json(args.commit_json, "commit_json"),
        "branch_point_payload": fixture_json(
            args.branch_point_json, "branch_point_json"
        ),
        "version_text": fixture_bytes(args.version_text, "version_text"),
    }


def discover(args: Any) -> dict[str, Any]:
    fixtures = (
        args.release_json,
        args.dash_json,
        args.tag_ref_json,
        args.branch_ref_json,
        args.commit_json,
        args.branch_point_json,
        args.version_text,
    )
    if args.online:
        if any(value is not None for value in fixtures):
            raise DiscoveryError("--online cannot be combined with offline fixture inputs")
        inputs = online_inputs(args.network_timeout)
    else:
        inputs = offline_inputs(args)
    return derive_candidate(**inputs, retrieved_at=args.retrieved_at)
