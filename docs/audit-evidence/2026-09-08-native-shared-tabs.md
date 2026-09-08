# Native shared-tab candidate — 2026-09-08

## Current result

Local visible journey is **RED**, not multi-device or release acceptance.
The installed app starts and loads a page. Saving persists successfully but
the sidebar keeps the stale temporary row. Native correction272385f adds the
missing coalesced presentation notification after a bound page changes saved
state or workspace. The corrected candidate must repeat the visible journey
before focused programmatic checks. No new tests or test binaries preceded it.

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
