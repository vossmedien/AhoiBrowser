# Portable Workspace Import — bounded visible journey

Date: 2026-09-23. Source package `51d7e7902e6beedb2bb76d1e891dc19bc2b5d80d`,
Chromium `153.0.8010.53`. Product-only guarded build exited 0; the exact
receipt is
`artifacts/build/native-m153-portable-import-51d7e79-20260923/build-receipt.json`.
The final signed portable app binary SHA-256 is
`b3ea2d2acf2301f4b7acf300b627d71d8be8534f2a2e468332939963bf59e341`.
Its APFS-cloned app copy verified with `codesign --verify --deep --strict` and
had the same executable SHA-256. No `/Applications` installation or CloudKit
credential, zone or key action belongs to this journey.

The native Settings picker opened an Ahoi-exported, mode-0600, 434-byte JSON
file in a disposable `/private/tmp` directory. The file held one Inbox and
one public `https://example.com/` temporary page, with no split/archive.
SHA-256:
`3476078ca306c6b9b13c23d62c90b33b9ac687ae52f2f3f0286a06580eb55e64`.
The byte-identical, public-only test export is retained as
[`public-example.ahoi.json`](public-example.ahoi.json) for replay; its
Workspace/page IDs are synthetic and it carries no cookies, credentials or
personal URLs.
Neither test profile nor source file came from the user's normal browser
profile. No raw private URLs or file content were logged.

Visible candidate `a6c8835` on a fresh isolated profile:

1. Native file choice produced a destination preview with Inbox identical,
   one new page and zero conflicts; the Import button was enabled.
2. One deliberate click showed “Import abgeschlossen” and added “Example
   Domain — Temporärer Tab” to the Sidebar.
3. SQLite readback showed two active nodes total (including Settings), exactly
   one node with the file's page ID and `is_temporary=1`, and `quick_check=ok`.
4. Reopening the same exact file showed zero new, two identical and zero
   conflicts. A second deliberate import returned “Bereits vorhanden – keine
   Änderungen”; SQLite counts and source-file SHA remained unchanged.
5. After a normal quit and explicit restart of the *same disposable profile*,
   choosing Continue showed the same imported Sidebar row. SQLite still had
   exactly one matching page ID and `quick_check=ok`.

Visible final package `51d7e79` on that same disposable profile: its signed
clone reopened with the exact explicit `--user-data-dir`; the persisted Sidebar
row appeared, the same file read as zero new/two identical/zero conflicts, and
one deliberate repeat returned “Bereits vorhanden – keine Änderungen”. The
matching ID count stayed one and `quick_check=ok`. This covers the changed
valid-file boundary after the final strict-decoder correction; the initial
first-import action was on `a6c8835`, whose import transaction code was not
changed between packages.

The test-only browser unexpectedly received a pre-existing Find-on-page query
after the native Open dialog closed. Its origin could not be attributed. The
query was closed only after it remained unchanged for over 30 minutes; no
import was inferred from that overlay. A later Computer Use observation after
Cmd+Q auto-relaunched the disposable app *without* the explicit profile
argument. That new PID was terminated before further UI actions, and the
profile-bound app was launched explicitly. The unbound launch is not counted
as a restart pass. No other Ahoi process was stopped.
After the final readback, all exact task-owned browser PIDs were terminated.
The three disposable `/private/tmp` app/profile/log directories (about 10 GB)
were moved to macOS Trash, not permanently deleted; they remain recoverable.
Only the public export and this sanitized result were retained in the repo.

An attempted focused `ahoi_session_unittests` target unexpectedly expanded to
3,471 Chromium test actions. Only our own Ninja run was interrupted after
786 actions; its absent exit receipt is **not** a test pass. The product-only
guarded build then exited 0 independently. The single focused regression source
remains in `portable_workspace_import_plan_unittest.cc`, unexecuted.

Limits: no visible user conflict choice or rename/skip path, no nontrivial
split/archive restore or rollback fault injection, no installed-Development
candidate pass, no Mac–iOS Sync roundtrip and no Glass/Omnibox acceptance.
