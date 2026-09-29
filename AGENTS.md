# AhoiBrowser agent entry points

These are repository-specific pointers; the global Codex working rules still apply.

## Find the current contract and owner

- Use [README.md](README.md) for orientation and command entry points. Consult the affected sections of [the master product contract](outputs/AhoiBrowser-Master-Zielprompt.md) for scope or product decisions; it remains normative subject to its more specific current decisions and gates.
- For desktop work, start with the current contract, ownership and affected candidate sections of [the desktop checkpoint](docs/ACTIVE_DESKTOP_CHECKPOINT.md). Before using the shared Chromium checkout or `out/AhoiDev`, read the current handoff in [the bookmarks checkpoint](docs/ACTIVE_BOOKMARKS_CHECKPOINT.md) and establish the current owner's handoff. Load older chronology only for a specific unresolved question.
- For mobile work, start with the current-work section of [the mobile checkpoint](docs/ACTIVE_MOBILE_CHECKPOINT.md) and the relevant linked implementation handoff. Its historical archive is evidence, not a resume instruction. Mobile product code is under `apps/AhoiMobile/`; preserve the explicit desktop, mobile and shared-sync ownership boundaries.
- Parallel lanes and their write boundaries are in [`config/agent-lanes.json`](config/agent-lanes.json). The `crest-hardening` lane ([goal](outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md), [checkpoint](docs/ACTIVE_CREST_HARDENING_LANE.md)) delivers changes to other lanes' paths only as handoffs under `handoffs/crest-hardening/`; owners review `ready` handoffs before cutting their next package. Commits carry a `Lane: <name>` trailer; check with `python3 tools/check_lane_boundaries.py --all --since <base>`.
- Checkpoints are handoff pointers, not proof that an old PID, source revision, installed app, or test result is still current. Verify the relevant live state before acting.

## Source, builds, and evidence

- Durable standalone Ahoi desktop changes belong in [the tracked overlay](overlay/chromium/README.md); upstream Chromium changes belong in [the patch stack](patches/chromium/README.md). `patches/chromium/series` owns patch order. Do not leave unique product changes only in `.work/chromium/src/`.
- Read [BUILDING.md](docs/BUILDING.md) before source integration or builds. Use the guarded scripts, including `./scripts/build-ahoi.sh dev` for the documented development path; do not bypass overlay, pin, toolchain, or provenance checks.
- Preserve Chromium's native profile, WebContents, sandbox, and permission ownership. Consult the relevant architecture document before changing those boundaries.
- Bind visible E2E and subsequent focused tests to the exact integrated candidate. `./scripts/test-repository.sh` is a repository regression entry point, not installed-browser acceptance. Build, package, install, visible runtime, and release are separate evidence gates.
