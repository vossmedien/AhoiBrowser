# Bound CloudKit partial-failure correction

Source eb54d94ac61d04ae88d1b4bd284d3db5f4723ef6 is ccd plus the two explicitly
released provider files. Source is preserved under refs/ahoi-preserved and the
same isolated temporary worktree. ccd original and entitled copies remain safe.

Hypothesis comes from the actual ccd CKSend PartialFailure2 and a concrete source
contract contradiction: HandleSent may accept server-equal/newer records while
CompleteUpload discards their ACKs because the aggregate error remains.
The new contract normalizes only a complete exact current send with all failed
saves resolved and no unresolved/zone/delete failures. Newer remote data must
persist successfully before ACK. Original Setting consent is captured through
the final posted callback. Unknown failures keep outbox and normal backoff.

Local diagnostics contain only fixed stages/domain classes, numeric codes and
Saved/Resolved/Unresolved/ACK/Expected counts. No record IDs, account names,
payload, keys, tokens or raw NSError text. No schema/API/SQLite flag changes.

Guarded app-only run uses three jobs on existing out. Capacity at start58%idle;
57 GiB free requires the documented low-disk override, retaining32 GiB hard floor.
No cleanup or foreign process action. Build/sign/install/runtime remain separate.
Next real journey uses preserved MacA Sync ON, account/zone flagsfalse, outbox13
and acknowledged0. No recovery reset, peer activation or design implementation.
