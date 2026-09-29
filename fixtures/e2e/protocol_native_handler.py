#!/usr/bin/env python3
"""Native, codesign-bound custom URL handler bundle for the E2E fixture."""

from __future__ import annotations

import hashlib
import json
import os
import plistlib
import re
import secrets
import stat
import subprocess
from pathlib import Path
from typing import Callable, Mapping, Optional, Sequence, Tuple

from protocol_launch_services import LaunchServicesError, run, run_result
from protocol_state import FileStamp, ProtocolState, read_at, stamp, write_new_at


SCHEME = "ahoi-e2e-safe"
ACCEPTED_URL = "ahoi-e2e-safe://open/fixture"
BUNDLE_ID = "app.ahoibrowser.fixture.custom-protocol"
APP_NAME = "AhoiBrowser E2E Protocol Handler.app"
EXECUTABLE_NAME = "AhoiProtocolHandler"
MARKER_NAME = "ahoi-fixture-handler.json"
RECEIPT_NAME = "protocol-handler-receipt.json"
EVENTS_NAME = "protocol-handler-events.jsonl"
MARKER_SCHEMA_VERSION = 4
RECEIPT_SCHEMA_VERSION = 4
MANAGED_BY = "AhoiBrowser fixtures/e2e/custom_protocol.py"
MAX_ARTIFACT_BYTES = 64 * 1024 * 1024
HASH_KEYS = (
    "compiledHandlerSha256",
    "infoPlistSha256",
    "markerSha256",
)


class NativeHandlerError(RuntimeError):
    """The native handler bundle cannot be built or authenticated safely."""


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _stable_file(
    path: Path,
    *,
    maximum_bytes: int = MAX_ARTIFACT_BYTES,
    owner_only: bool = False,
) -> Optional[Tuple[bytes, FileStamp]]:
    if not hasattr(os, "O_NOFOLLOW"):
        return None
    flags = os.O_RDONLY | os.O_NOFOLLOW | int(getattr(os, "O_CLOEXEC", 0))
    try:
        descriptor = os.open(path, flags)
    except OSError:
        return None
    try:
        before_raw = os.fstat(descriptor)
        before = stamp(before_raw)
        if (
            not stat.S_ISREG(before.mode)
            or before.uid != os.getuid()
            or before.links != 1
            or before.size < 0
            or before.size > maximum_bytes
            or (owner_only and stat.S_IMODE(before.mode) & 0o077)
        ):
            return None
        payload = bytearray()
        while len(payload) <= maximum_bytes:
            chunk = os.read(
                descriptor,
                min(64 * 1024, maximum_bytes + 1 - len(payload)),
            )
            if not chunk:
                break
            payload.extend(chunk)
        after = stamp(os.fstat(descriptor))
    except OSError:
        return None
    finally:
        os.close(descriptor)
    if len(payload) > maximum_bytes or before != after or len(payload) != before.size:
        return None
    return bytes(payload), before


def _sha256_file(path: Path) -> Optional[str]:
    value = _stable_file(path)
    return None if value is None else _sha256_bytes(value[0])


def _bytes_initializer(payload: bytes) -> str:
    return ",".join(str(value) for value in payload)


def _octal_literal(value: str) -> str:
    return '"%s"' % "".join("\\%03o" % byte for byte in value.encode("utf-8"))


def native_source(state: ProtocolState) -> bytes:
    identity = state.identity
    source = r'''#import <AppKit/AppKit.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char kAcceptedUrl[] = %s;
static const char kStatePath[] = %s;
static const char kLockName[] = %s;
static const char kMarkerName[] = %s;
static const char kEventsName[] = %s;
static const unsigned char kMarkerBytes[] = {%s};
static const size_t kMarkerLength = %d;
static const dev_t kStateDevice = (dev_t)%d;
static const ino_t kStateInode = (ino_t)%d;
static const dev_t kLockDevice = (dev_t)%d;
static const ino_t kLockInode = (ino_t)%d;

static bool WriteAll(int descriptor, const char* payload, size_t length) {
  size_t written = 0;
  while (written < length) {
    ssize_t count = write(descriptor, payload + written, length - written);
    if (count <= 0) return false;
    written += (size_t)count;
  }
  return true;
}

static bool SecureRecord(void) {
  bool result = false;
  int state_fd = -1;
  int lock_fd = -1;
  int marker_fd = -1;
  int event_fd = -1;
  struct stat state_before;
  struct stat lock_before;
  struct stat marker_before;
  struct stat event_before;
  struct stat event_after;
  state_fd = open(kStatePath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (state_fd < 0 || fstat(state_fd, &state_before) != 0) goto cleanup;
  if (!S_ISDIR(state_before.st_mode) || state_before.st_uid != getuid() ||
      (state_before.st_mode & 077) != 0 || state_before.st_dev != kStateDevice ||
      state_before.st_ino != kStateInode) goto cleanup;
  lock_fd = openat(state_fd, kLockName, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
  if (lock_fd < 0 || fstat(lock_fd, &lock_before) != 0) goto cleanup;
  if (!S_ISREG(lock_before.st_mode) || lock_before.st_uid != getuid() ||
      lock_before.st_nlink != 1 || (lock_before.st_mode & 0777) != 0600 ||
      lock_before.st_dev != kLockDevice || lock_before.st_ino != kLockInode) {
    goto cleanup;
  }
  if (flock(lock_fd, LOCK_EX) != 0) goto cleanup;
  marker_fd = openat(state_fd, kMarkerName, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (marker_fd < 0 || fstat(marker_fd, &marker_before) != 0) goto cleanup;
  if (!S_ISREG(marker_before.st_mode) || marker_before.st_uid != getuid() ||
      marker_before.st_nlink != 1 || (marker_before.st_mode & 077) != 0 ||
      (size_t)marker_before.st_size != kMarkerLength) goto cleanup;
  unsigned char marker_buffer[sizeof(kMarkerBytes)];
  size_t marker_read = 0;
  while (marker_read < kMarkerLength) {
    ssize_t count = read(marker_fd, marker_buffer + marker_read,
                         kMarkerLength - marker_read);
    if (count <= 0) goto cleanup;
    marker_read += (size_t)count;
  }
  unsigned char extra;
  if (read(marker_fd, &extra, 1) != 0 ||
      memcmp(marker_buffer, kMarkerBytes, kMarkerLength) != 0) goto cleanup;
  event_fd = openat(state_fd, kEventsName,
                    O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                    0600);
  if (event_fd < 0 || fstat(event_fd, &event_before) != 0) goto cleanup;
  if (!S_ISREG(event_before.st_mode) || event_before.st_uid != getuid() ||
      event_before.st_nlink != 1 || (event_before.st_mode & 077) != 0) {
    goto cleanup;
  }
  if (fchmod(event_fd, 0600) != 0) goto cleanup;
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) goto cleanup;
  long long milliseconds = ((long long)now.tv_sec * 1000LL) + now.tv_nsec / 1000000LL;
  char payload[256];
  int length = snprintf(
      payload, sizeof(payload),
      "{\"event\":\"exact-custom-protocol-open\","
      "\"incomingValueRetained\":false,\"observedAtUnixMs\":%%lld,"
      "\"schemaVersion\":1}\n",
      milliseconds);
  if (length <= 0 || (size_t)length >= sizeof(payload) ||
      !WriteAll(event_fd, payload, (size_t)length) || fsync(event_fd) != 0 ||
      fstat(event_fd, &event_after) != 0) goto cleanup;
  if (!S_ISREG(event_after.st_mode) || event_after.st_uid != getuid() ||
      event_after.st_nlink != 1 || (event_after.st_mode & 0777) != 0600 ||
      event_after.st_dev != event_before.st_dev ||
      event_after.st_ino != event_before.st_ino) goto cleanup;
  result = true;
cleanup:
  if (event_fd >= 0) close(event_fd);
  if (marker_fd >= 0) close(marker_fd);
  if (lock_fd >= 0) {
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
  }
  if (state_fd >= 0) close(state_fd);
  return result;
}

@interface AhoiProtocolDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AhoiProtocolDelegate
- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  [[NSAppleEventManager sharedAppleEventManager]
      setEventHandler:self
          andSelector:@selector(handleGetUrl:withReply:)
        forEventClass:kInternetEventClass
           andEventID:kAEGetURL];
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}
- (void)handleGetUrl:(NSAppleEventDescriptor*)event
           withReply:(NSAppleEventDescriptor*)reply {
  NSString* incoming = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
  NSString* accepted = [NSString stringWithUTF8String:kAcceptedUrl];
  if (incoming != nil && [incoming isEqualToString:accepted]) SecureRecord();
  [NSApp terminate:nil];
}
@end

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSApplication* application = [NSApplication sharedApplication];
    AhoiProtocolDelegate* delegate = [[AhoiProtocolDelegate alloc] init];
    [application setDelegate:delegate];
    [application run];
  }
  return 0;
}
''' % (
        _octal_literal(ACCEPTED_URL),
        _octal_literal(str(state.path)),
        _octal_literal(".state.lock"),
        _octal_literal("state-identity.json"),
        _octal_literal(EVENTS_NAME),
        _bytes_initializer(state.marker_payload),
        len(state.marker_payload),
        int(identity["stateDevice"]),
        int(identity["stateInode"]),
        int(identity["lockDevice"]),
        int(identity["lockInode"]),
    )
    return source.encode("utf-8")


def expected_marker(
    logical_app_path: Path,
    installation_id: str,
    state: ProtocolState,
    handler_sha256: str,
    source_sha256: str,
) -> Mapping[str, object]:
    return {
        "schemaVersion": MARKER_SCHEMA_VERSION,
        "managedBy": MANAGED_BY,
        "installationId": installation_id,
        "stateId": state.identity["stateId"],
        "stateMarkerSha256": _sha256_bytes(state.marker_payload),
        "bundleIdentifier": BUNDLE_ID,
        "scheme": SCHEME,
        "acceptedUrl": ACCEPTED_URL,
        "appPath": str(logical_app_path),
        "compiledHandlerSha256": handler_sha256,
        "sourceSha256": source_sha256,
    }


def _info() -> Mapping[str, object]:
    return {
        "CFBundleDisplayName": "AhoiBrowser E2E Protocol Handler",
        "CFBundleExecutable": EXECUTABLE_NAME,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleName": "AhoiBrowser E2E Protocol Handler",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "1.0",
        "CFBundleVersion": "1",
        "CFBundleURLTypes": [
            {"CFBundleURLName": BUNDLE_ID, "CFBundleURLSchemes": [SCHEME]}
        ],
        "LSMinimumSystemVersion": "12.0",
        "LSUIElement": True,
    }


def _write_exclusive(path: Path, payload: bytes, mode: int) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    flags |= int(getattr(os, "O_CLOEXEC", 0))
    descriptor = os.open(path, flags, mode)
    try:
        written = 0
        while written < len(payload):
            count = os.write(descriptor, payload[written:])
            if count <= 0:
                raise NativeHandlerError("native handler artifact write was incomplete")
            written += count
        os.fsync(descriptor)
        os.fchmod(descriptor, mode)
    finally:
        os.close(descriptor)


def _signature_integrity(
    app_path: Path,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> bool:
    try:
        run_result(
            [
                "/usr/bin/codesign",
                "--verify",
                "--deep",
                "--strict",
                "--verbose=2",
                str(app_path),
            ],
            runner=runner,
        )
        detail = run_result(
            ["/usr/bin/codesign", "-d", "--verbose=4", str(app_path)],
            runner=runner,
        )
    except LaunchServicesError:
        return False
    metadata = (detail.stdout or "") + "\n" + (detail.stderr or "")
    return re.search(
        r"(?m)^Identifier=" + re.escape(BUNDLE_ID) + r"$", metadata
    ) is not None


def _load_info(app_path: Path) -> Optional[Mapping[str, object]]:
    value = _stable_file(app_path / "Contents" / "Info.plist")
    if value is None:
        return None
    try:
        decoded = plistlib.loads(value[0])
    except (ValueError, plistlib.InvalidFileException):
        return None
    return decoded if isinstance(decoded, dict) else None


def artifact_hashes(app_path: Path) -> Optional[Mapping[str, str]]:
    values = {
        "compiledHandlerSha256": _sha256_file(
            app_path / "Contents" / "MacOS" / EXECUTABLE_NAME
        ),
        "infoPlistSha256": _sha256_file(app_path / "Contents" / "Info.plist"),
        "markerSha256": _sha256_file(
            app_path / "Contents" / "Resources" / MARKER_NAME
        ),
    }
    if any(value is None for value in values.values()):
        return None
    return {key: str(value) for key, value in values.items()}


def _owned_containers(app_path: Path) -> bool:
    for path in (
        app_path,
        app_path / "Contents",
        app_path / "Contents" / "MacOS",
        app_path / "Contents" / "Resources",
    ):
        try:
            metadata = os.lstat(path)
        except OSError:
            return False
        if not stat.S_ISDIR(metadata.st_mode) or metadata.st_uid != os.getuid():
            return False
    return True


def valid_receipt(
    receipt: Mapping[str, object],
    logical_app_path: Path,
    state: ProtocolState,
) -> bool:
    hashes = receipt.get("artifactHashes")
    installation_id = receipt.get("installationId")
    return bool(
        receipt.get("schemaVersion") == RECEIPT_SCHEMA_VERSION
        and receipt.get("managedBy") == MANAGED_BY
        and receipt.get("explicitConsent") is True
        and receipt.get("stateId") == state.identity["stateId"]
        and receipt.get("stateMarkerSha256") == _sha256_bytes(state.marker_payload)
        and receipt.get("appPath") == str(logical_app_path)
        and receipt.get("bundleIdentifier") == BUNDLE_ID
        and receipt.get("scheme") == SCHEME
        and receipt.get("acceptedUrl") == ACCEPTED_URL
        and isinstance(installation_id, str)
        and re.fullmatch(r"[0-9a-f]{32}", installation_id) is not None
        and isinstance(hashes, dict)
        and set(hashes) == set(HASH_KEYS)
        and all(
            isinstance(hashes.get(key), str)
            and re.fullmatch(r"[0-9a-f]{64}", str(hashes[key])) is not None
            for key in HASH_KEYS
        )
    )


def ownership_marker(
    physical_app_path: Path,
) -> Optional[Tuple[Mapping[str, object], bytes, FileStamp]]:
    value = _stable_file(
        physical_app_path / "Contents" / "Resources" / MARKER_NAME,
        owner_only=True,
    )
    if value is None:
        return None
    payload, generation = value
    try:
        decoded = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(decoded, dict):
        return None
    return decoded, payload, generation


def owns_app(
    physical_app_path: Path,
    logical_app_path: Path,
    receipt: Mapping[str, object],
    state: ProtocolState,
) -> bool:
    if not valid_receipt(receipt, logical_app_path, state):
        return False
    if not _owned_containers(physical_app_path):
        return False
    marker_value = ownership_marker(physical_app_path)
    if marker_value is None:
        return False
    marker, payload, _generation = marker_value
    hashes = receipt["artifactHashes"]
    return bool(
        _sha256_bytes(payload) == hashes["markerSha256"]
        and marker
        == expected_marker(
            logical_app_path,
            str(receipt["installationId"]),
            state,
            str(hashes["compiledHandlerSha256"]),
            str(marker.get("sourceSha256", "")),
        )
    )


def valid_app(
    physical_app_path: Path,
    logical_app_path: Path,
    receipt: Mapping[str, object],
    state: ProtocolState,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> bool:
    if not owns_app(physical_app_path, logical_app_path, receipt, state):
        return False
    info = _load_info(physical_app_path)
    return bool(
        info == _info()
        and artifact_hashes(physical_app_path) == receipt.get("artifactHashes")
        and _signature_integrity(physical_app_path, runner=runner)
    )


def build_app(
    state: ProtocolState,
    staging_name: str,
    logical_app_path: Path,
    installation_id: str,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> Mapping[str, object]:
    if Path(staging_name).name != staging_name:
        raise NativeHandlerError("native handler staging name is unsafe")
    staging = state.path / staging_name
    source_name = ".native-source-%s.m" % secrets.token_hex(16)
    source_path = state.path / source_name
    source_payload = native_source(state)
    write_new_at(state, source_name, source_payload)
    try:
        os.mkdir(staging_name, 0o700, dir_fd=state.directory_descriptor)
        (staging / "Contents" / "MacOS").mkdir(parents=True)
        (staging / "Contents" / "Resources").mkdir()
        executable = staging / "Contents" / "MacOS" / EXECUTABLE_NAME
        try:
            run(
                [
                    "/usr/bin/clang",
                    "-fobjc-arc",
                    "-fblocks",
                    "-framework",
                    "AppKit",
                    "-framework",
                    "Foundation",
                    str(source_path),
                    "-o",
                    str(executable),
                ],
                runner=runner,
            )
        except LaunchServicesError as error:
            raise NativeHandlerError("native handler compilation failed") from error
        handler_sha256 = _sha256_file(executable)
        if handler_sha256 is None:
            raise NativeHandlerError("native handler executable cannot be hashed")
        source_sha256 = _sha256_bytes(source_payload)
        info_payload = plistlib.dumps(dict(_info()), sort_keys=True)
        _write_exclusive(staging / "Contents" / "Info.plist", info_payload, 0o600)
        marker = expected_marker(
            logical_app_path,
            installation_id,
            state,
            handler_sha256,
            source_sha256,
        )
        marker_payload = (json.dumps(marker, indent=2, sort_keys=True) + "\n").encode(
            "utf-8"
        )
        _write_exclusive(
            staging / "Contents" / "Resources" / MARKER_NAME,
            marker_payload,
            0o600,
        )
        try:
            run(
                [
                    "/usr/bin/codesign",
                    "--force",
                    "--sign",
                    "-",
                    "--identifier",
                    BUNDLE_ID,
                    str(staging),
                ],
                runner=runner,
            )
        except LaunchServicesError as error:
            raise NativeHandlerError("native handler signing failed") from error
        hashes = artifact_hashes(staging)
        if hashes is None or hashes["compiledHandlerSha256"] != handler_sha256:
            raise NativeHandlerError("signed native handler hashes did not verify")
        receipt: Mapping[str, object] = {
            "schemaVersion": RECEIPT_SCHEMA_VERSION,
            "managedBy": MANAGED_BY,
            "installationId": installation_id,
            "explicitConsent": True,
            "stateId": state.identity["stateId"],
            "stateMarkerSha256": _sha256_bytes(state.marker_payload),
            "appPath": str(logical_app_path),
            "bundleIdentifier": BUNDLE_ID,
            "scheme": SCHEME,
            "acceptedUrl": ACCEPTED_URL,
            "artifactHashes": hashes,
        }
        if not valid_app(staging, logical_app_path, receipt, state, runner=runner):
            raise NativeHandlerError("signed native handler failed integrity validation")
        return receipt
    finally:
        try:
            os.unlink(source_name, dir_fd=state.directory_descriptor)
        except OSError:
            pass
