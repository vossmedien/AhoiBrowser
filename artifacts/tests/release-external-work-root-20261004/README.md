# External work-root release CLI acceptance — 4 October 2026

Source cc8bef2b. The configured SSD work root was ignored by both release CLI
Sparkle lookup and the public update testbed. Actual baseline failed looking for
fetch-receipt.json under the removed internal worktree .work directory. The common
resolver now honors absolute AHOI_WORK_ROOT and prevents explicit-root fallback;
production release policy, signatures and material gates remain unchanged.

Actual corrected public CLI on MacbookPro2026.local, approved SSD root:
26/26 PASS, exit0. Real pinned Sparkle2.9.6 tools generated/verified local signed
stand-in archives/appcast and rejected tampering/replay/channel violations.
Relative and deliberately missing roots independently reject before scenarios.
This rerun was justified by a reproduced CLI setup failure and changed path
resolution, not a replay of already green native browser/27-vector suites.

First corrected run reached verified Sparkle material, then failed because target
/usr/bin/openssl is LibreSSL3.3.6 without Ed25519 generation. The existing source
OpenSSL3.6.3 native artifact was hash-verified on target, its Mach-O load paths
adapted and ad-hoc signed there under an Ahoi-owned toolchain prefix. Actual
version and cryptographic operations pass. No system installation, source build,
GUI/browser/simulator, production key/feed, account or network action.
Original two failures, artifact transfer hashes and corrected CLI logs retained.

The app archives are explicit stand-ins. This is CLI/material/security boundary
acceptance, not installed-browser N-2/N-1, release, notarization or Update-UI proof.
The console lock and native cross-level journey remain separately parked.
