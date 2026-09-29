"""Owner-build helper: record the effective PGO/ThinLTO configuration.

Called by build_provenance only in the owner's regular release build. Crest
tests this with local fixtures; it must not invoke GN in the shared checkout.
"""

from __future__ import annotations

import json
import pathlib
import re
import subprocess

from .build_evidence import EvidenceError, source_file


def file_sha256(path: pathlib.Path) -> str:
    import hashlib
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def collect(src: pathlib.Path, out: pathlib.Path, gn: pathlib.Path,
            chromium_commit: str) -> dict:
    """Only the Mac ARM64 phase-2 reference currently contracted by H3."""
    def query(*args):
        try:
            return subprocess.check_output([str(gn), *map(str, args)], cwd=src,
                                           stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError as error:
            raise EvidenceError("cannot read effective GN optimization configuration") from error

    wanted = {"chrome_pgo_phase", "use_thin_lto", "target_cpu", "target_os"}
    values = {}
    for line in query("args", out, "--list", "--short").splitlines():
        match = re.fullmatch(r"\s*(\w+)\s*=\s*(.+?)\s*", line)
        if match and match[1] in wanted:
            if match[1] in values:
                raise EvidenceError("duplicate effective GN optimization argument")
            values[match[1]] = json.loads(match[2])
    if (values.get("target_cpu") != "arm64" or values.get("target_os") != "mac"
            or type(values.get("chrome_pgo_phase")) is not int
            or values["chrome_pgo_phase"] != 2
            or type(values.get("use_thin_lto")) is not bool):
        raise EvidenceError("H3 needs Mac ARM64 phase-2 PGO and an effective ThinLTO value")

    flags = [line.strip() for line in query(
        "desc", out, "//build/config/compiler/pgo:pgo_optimization_flags", "cflags").splitlines()]
    selected = [flag.partition("=")[2] for flag in flags if flag.startswith("-fprofile-use=")]
    if len(selected) != 1:
        raise EvidenceError("GN did not identify exactly one compiler PGO profile")
    selected_path = selected[0]
    if selected_path.startswith("//"):
        profile = src / selected_path[2:]
    elif pathlib.Path(selected_path).is_absolute():
        profile = pathlib.Path(selected_path)
    else:
        profile = out / selected_path

    pointer_path = "chrome/build/mac-arm.pgo.txt"
    pinned = source_file(src, chromium_commit, pointer_path)
    if (src / pointer_path).read_bytes() != pinned:
        raise EvidenceError("working PGO pointer differs from the Chromium pin")
    name = pinned.decode().strip()
    if not re.fullmatch(r"[^/\\]+\.profdata", name):
        raise EvidenceError("invalid pinned Mac ARM PGO profile name")
    expected = src / "chrome/build/pgo_profiles" / name
    # Symlinks/overrides outside the pinned directory are not a substitute.
    if expected.is_symlink() or profile.resolve() != expected.resolve():
        raise EvidenceError("compiler PGO profile differs from the Chromium pin")
    return {
        "chromePgoPhase": values["chrome_pgo_phase"],
        "useThinLto": values["use_thin_lto"],
        "pgoProfile": {"target": "mac-arm", "name": name,
                       "sha256": file_sha256(expected), "matchesChromiumPin": True},
    }
