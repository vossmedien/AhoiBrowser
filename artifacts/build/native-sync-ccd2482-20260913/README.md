# Chromium SQLite ACK correction

Source ccd24827be87ab2cdcc0e2579137eb7d83700b4b is e4 plus exactly two Common
SQL corrections. The real predecessor crash and original evidence are in
../../e2e/native-sync-e4de9e1-20260913/README.md. Root expressly released these
two files; separate Native Structure/Mobile/UX work remains excluded.

The pin's SQLITE_OMIT_UPSERT rejects ON CONFLICT…DO UPDATE. Both replacements
use supported INSERT OR REPLACE on complete receipt-only tables without
foreign keys or triggers. ACK selects nothing if an existing receipt has a
newer clock; receipt and outbox deletion remain one transaction. No weakening
of SQLite checks, schema changes, migration, Raze, recovery/store/key reset.

Guarded app-only run uses the same clean isolated source worktree and existing
AhoiDev output with three jobs. Explicit overlay/build exits and a successful
receipt are produced by run-build.sh; separate signing never recompiles.
e4 original/CloudKit artifacts and every installed rollback remain protected.

Baseline immediately before this candidate's runtime, after ordinary e4 quit:
MacA outbox10, acknowledged0, native_observations0; accountTransitionPending=false,
zoneRecoveryPending=false. Retained Sync is ON. Existing MacA is not fresh/unused.
Next proof must exercise this pending real upload/ACK path and ordinary shutdown.

check-sql.py is a narrow companion to run after that visible journey. It loads
the actual built Chromium SQLite library, uses only synthetic :memory: rows,
verifies the original unsupported SQL fails, and checks preserved ACK clock
ordering plus native observation replacement. It is not transport acceptance.
