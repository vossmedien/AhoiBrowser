# Native shared-tab candidate — 2026-09-08

## Current result

The corrected6ae4070 candidate passed the bounded **local visible lifecycle**:
new tab -> Save -> Unsave -> normal Quit -> restart/Continue -> explicit close.
It is not multi-device or release acceptance. The originalc20 failure remains
below. Correction272385f adds the missing coalesced notification after a bound
page changes saved state/workspace. No new tests or test binaries preceded the
corrected visible journey. The12 focused follow-up cases are now green, with
the original failed fault-injection run and its correction retained below.

## Corrected candidate and visible repeat

- Clean source6ae40701d93727f8915668a99a3aaf22aef16de0 is c20 plus ONLY the
  native10-line presentation fix. Overlay63476/build91795/install29501 EXIT0.
- Build receipt `artifacts/build/desktop-shared-tabs-6ae4070-20260908/build-receipt.json`,
  SHA334a51d3a9a79b5b18ff1954e5869d8ed7c4b2c68af6353c179f5d9eaff8cf62,
  builtAt2026-09-08T09:39:43.465105+00:00.
- Install receipt `artifacts/install/ahoi-dev-6ae4070-20260908.json`,
  SHA2814734670336c7f2c77cf27031211a2e7c77fe1693820e0ee5ee1f8d82d8195.
- Installed binary9870d0a9b99d849c060dad9e42e612a3a7841f98d5e57b730b1cca03599e6b8e;
  tree5bb026f6f612e7b43248b84b46d89322120bb60888659466f5511bac04da00f7.
  Stable development signature and source stamp verified. Priorc20 and4cb
  remain in their receipt-bound rollback bundles.
- Same isolated test profile as below; runtime97371/PID14984 and restart52424/
  PID20449 both ended with normal Cmd+Q and EXIT0. No bound CUA read after Quit.

After Continue, the savedcom page was visible. A neworg tab was opened through
Cmd+T and actual `typeText` input (AX setValue alone did not recompute command
results, so that preliminary attempt is excluded). It appeared exactly once
as temporary. Cmd+D immediately moved the row into saved pages without changing
the active org page. Its native “Loslösen” action returned it to temporary.
The first menu handle expired; one fresh menu open/click succeeded. The original
expiry is retained, not erased by the retry.

At the following observation there was an unexplained org->com focus change,
width264->293 and removed infobar without our intervening UI action. Concurrency
was reported to the coordinator; this does not establish a product cause or
an uninterrupted no-focus-change pass. The stable final state was explicitly
reselected to org before Quit. The ensuing restart/Continue visibly retained
activeorg, one savedcom and one temporaryorg, with no duplicate rows. Cmd+W then
removed onlyorg; com remained open/saved.

Read-only native-store corroboration after the visible steps:

| Step | org Page ID | is_temporary | tombstone |
| --- | --- | --- | --- |
| Created | 5e58f845-b569-48ca-9edd-9d28a65e4051 | 1 | 0 |
| Cmd+D | same | 0 | 0 |
| Loslösen | same | 1 | 0 |
| Normal Quit and restart | same | 1 | 0 |
| Explicit Cmd+W | same | 1 | 1 |

The savedcom ID328d769c-4934-4cb9-a8ea-47de1dbc4ee0 remained saved/live throughout.
Real Default DB/journal hashes still matched the originals below after this
repeat. No CloudKit account/key/configuration action or cross-client transport
ran. Dormant remote rows/origin filtering, concurrent no-focus-shift and the
real Arc failed-import recovery remain separate open acceptance.

## Focused checks after visible E2E

Test-onlye215186 is integrated as clean detacheded84ec43eafea325cd38f222b6fbe7e75cd3e5b4;
the ONLY delta from6ae is the existing Session test. Guarded overlay81032 and
focused two-target build2870 EXIT0. No app stamp/sign/install was repeated.
The loaded out/installed libchrome_dll hashes both match
cb843fbda0a6bbae5e7f01fb9074b8682d96cfe3bed6e71e5c7537cb981b7c42.

Explicit nonempty suite listing selected8 Session/target-policy tests and4
native receipt tests; each run used jobs1/retries0. Session run75097 EXIT0:
8/8 SUCCESS, including the Save/Unsave notification/global-ID regression, the
second native backup flush and all6 bounded target-policy cases. Native receipt
run EXIT1:3/4 SUCCESS, with the intentional SQL-error case failing because no
SQL error was actually injected. Its unchanged assertions correctly caught
that omission. Logs and machine-readable summaries:
`artifacts/tests/native-tabs-6ae4070-20260908/`.

This failure is in the fixture: Chromium DatabaseOptions disables triggers
(`sql/database.h:362`, applied in `database.cc:2474`), so its CREATE TRIGGER never
ran on the production connection. Test-onlyf97b661 replaces it with an enforced
CHECK constraint rejecting exactly the new receipt value; expected error
observation and exact whole-tree/baseline rollback assertions stay intact.
No production DB option, warning, assertion or test was disabled. Correction
is integrated as a9d6ad7. Overlay19904 and the one-target guarded corrective
build85696 both EXIT0. A fresh listing selected the same four names; the
corrected receipt run EXIT0,4/4 SUCCESS in254ms, jobs1/retries0. It reached the
actual expected CHECK failure and exact post-reload rollback assertions.
`tree-corrected-summary.json` SHA
754df569162ffcc19460e690bb8952d2bebc6c81d6890948ef87c2b0ce5f0b87;
the unchanged8-case `session-summary.json` SHA
8fef14bb12e5a386deb7f0c7fa4054e191d6260609e85589871f071e0bdc01d6.
Corrected Tree binary5266eedca993ce59252862f041b0ee4429b5be73dfc65542d09513184e550cb5.
The original3/4 failure is preserved. No production code/package changed for
these test-only fixes; installed6ae binary hash was rechecked unchanged, so its
visible journey remains applicable. No further app build/install was needed.

Original tested binary hashes: Session
50d0be6e11f0d0a8bc9c8070470614f83df241ce54750b3d73f3e7a3f99f7eda,
Tree812b3aadee7c1e97865fad7f75d7b06b9184d3255d6552de2f9079bc54dfcdfc.

## Exact first candidate

- Clean detached source: c20a759dd936cfa93d5fedeb4c9dcd52e876bcd7.
  Native74ceb plus5a15614, Inbox7a47063 and Common compilerdfcc32e;
  no settings/catalogue WIP.
- Guarded app-only build82463: EXIT0. Receipt builtAt
  2026-09-08T09:19:48.289439+00:00. Existing12-core machine had adequate
  aggregate capacity; jobs2, no extra test targets or warning suppression.
- Canonical build receipt:
  `artifacts/build/desktop-shared-tabs-c20a759-20260908/build-receipt.json`,
  SHA2567d67a8bbe97204c08535283792719ca2d7a0560666574f60279beba6e93205ce.
  Copied byte-identically from the owned build snapshot before reuse.
- Guarded atomic install99703: EXIT0. Receipt
  `artifacts/install/ahoi-dev-c20a759-20260908T092211Z.json`,
  SHA2563a1499158e861a556424b3a7df11805f82445ccf9f9b2c47801d7c160174a3cd.
  Filename is a label, not the transaction timestamp.
- Installed `/Applications/AhoiBrowser.app`: Source stamp matches above,
  binary7f34223ff06d7b430acab72d6f57d34072efeb3db5b1ff9363d566e1396ef189,
  tree d17efb433b0fa822fbde0ad0be476acb49d1bef85f4564870f1ca8be19583010.
  Verified stable Apple Development signature; Chromium152.0.7977.65.
- Previous4cb bundle remains at the install receipt's exact rollback path.
  No notarized release, CloudKit capability or Sync roundtrip is asserted.

## Visible steps and smallest diagnostic boundary

Started the installed executable using its normal `--user-data-dir` option:
`/private/tmp/ahoi-native-tabs-c20a759.Z10Mne`. Runtime32203/PID78973;
all subsequent browser interactions used native CUA, not hidden WebUI/CDP.

1. Fresh Inbox and one temporary New Tab appeared; no startup crash.
2. The initial combined Cmd+L/paste/Return automation used a stale clipboard
   value. This attempt is excluded. Subsequent navigation used a separately
   observed command-bar field, `setValue` with `https://example.com`, then
   Return. Example Domain visibly loaded with one temporary sidebar row.
3. Cmd+D left that row visibly temporary. After a fresh observation, the native
   row context menu's “Lesezeichen hinzufügen” also left the displayed state
   unchanged. No success is inferred from the menu closing.
4. Only THEN a read-only query of the isolated native SQLite store found
   Page328d769c-4934-4cb9-a8ea-47de1dbc4ee0, is_temporary=0, target_kind=0,
   tombstone=0, one Undo operation. This proves the storage boundary, not UI
   success or the full identity-preservation journey.
5. Source diagnosis: SessionBridge updates its binding in OnTabTreeChanged,
   but did not notify the runtime presentation when the same global Page ID
   changed temporary/saved status. Existing rows and tree suppression could
   survive until an unrelated native tab event. No Common code change needed.

The app was normally quit. Calling the bound CUA target's getAXState after
Quit caused an unwanted normal-profile relaunch into the startup chooser.
No Continue/Empty-start choice was selected; that process was immediately
quit normally, without another bound-target read. Both app PIDs are terminal.
Future quit verification must use process inventory, and restart explicitly
with the same isolated `--user-data-dir` before reattaching CUA.

Real Default DB and Arc journal hashes were identical before/after this episode:

- `Ahoi Tab Tree`:6b67d44e1bc95374a90fc625e73e2b23dd49b1f1b2fe3f919fc33fc575847e6e.
- `Ahoi/ArcImportJournal.json`:1768e20f145a7e949281eea58f33ef14a2a80e34a1430b1caf3d704a9cddd5a3.

This is not a claim of whole-profile byte identity. No backup/journal recovery,
key/account/CloudKit action, forced Arc shutdown or direct database write ran.
CUA screenshots/AX observations are in the conversation; no formal release
PASS record is manufactured from them.
