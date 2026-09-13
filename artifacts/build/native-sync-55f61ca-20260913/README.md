# Provider correction with compatible diagnostic output

55f61cae5b7c397860a6fe175065f3d97cd7097d is eb54d94 plus only the diagnostic
compiler correction. Apple's os_log_with_type macros triggered Chromium's
unsafe-buffer/format check. Original guarded run95153 EXIT1/log is preserved
at ../native-sync-eb54d94-20260913/. The corrected code uses Foundation NSLog
and NSString arguments, matching existing Chromium code; no warning is disabled.

Provider semantics, exact current-mutation accounting, unresolved-error gates,
remote persistence and original-lease checks remain the eb54 contract. Diagnostics
remain fixed stages/domain classes, numeric codes and counts only. No identifiers,
payload, keys, raw NSError, schema/API changes or hidden resets.

Guarded app-only run61547 reuses existing output with3jobs and the documented
low-disk override (32GiB hard floor unchanged). Installed ccd, its originals,
CloudKit copy and all rollback bundles are preserved. No UI or transport pass is
inferred from this corrective compilation.
