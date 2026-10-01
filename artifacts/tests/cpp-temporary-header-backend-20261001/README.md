# Temporary developer header backend, 1 October 2026

Source-only desktop change; the installed app remains 9acb43c8/M154. No build,
signing, installation, real Keychain call, simulator, CloudKit or release action.

Request/response rules can now be tab-local (`headers_persistent=false`). The
actual tab helper assigns a runtime owner token; Preferences reject temporary
rules/tokens and sync projection strips them even for malformed opt-in inputs.
Other persistent profile fields remain in their existing store. Header scope
stays exact-origin, secret values materialize only per request, and dedicated
worker routing retains its native ancestor-frame binding. Shared/service-worker
ownership remains open; no arbitrary parent is assigned.

Navigation/document/request paths use the actual tab's merged profile; temporary
snapshot revalidation retires reset/removed/replaced owners. A helper belonging
to another PrefService cannot attach over the proper marker, read or mutate that
profile through this tab, or clear its native user-agent decision. Later binding
registers master activation observation. Header-only navigation preserves other
native user-agent decisions; only Ahoi's own override changes its navigation flag.

Store/asset routines were moved into `developer_profile_runtime_store.cc` to
keep every implementation/test source below the 800-line budget. The runtime
GN source set includes it. Persistent local/wire schema stays unchanged.

Nine new native regressions cover tab separation, persistence/sync refusal,
helper retirement/replacement, reset/persistence conversion, mismatched contexts,
foreign/own user-agent decisions, and activation after later binding. They are
written and syntax-checked, **not executed**. The final serial pinned-Clang phase
checks eight actual source files, all exit 0; exact command/source/header/log
hashes are retained in the receipt. Earlier syntax phases are narrower historical
steps, not extra native test passes.

Next product work: expose temporary/persistent header lifetime in the native
editor, keep temporary sync controls disabled, add editor/real Mojo lifetime
coverage, then one exact guarded candidate and required native/visible gates.
No claim that the current editor offers this choice or that full DEV/Master
acceptance is complete. Existing Keychain entries are not deleted by this source
change; no credential ownership or external permission is expanded.
