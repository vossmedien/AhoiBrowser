# Bookmark effective authorization correction — 2026-09-05

Status: **FIXED IN SOURCE / NOT BUILT / TESTS NOT RUN**.

Exact committed/pushed freeze: `225df88f4e6dbe692390f52b8687b653541723ac`.
Desktop integration handoff: `01a07231-8c67-79e2-b983-2e38c5a2e2f8`.
There are 25 owned changed files, including this report and the checkpoint;
the list below names the 23 product/test files. No uncommitted WIP is required.

This is the bounded follow-up to the Desktop review
`01a0719b-0215-7cf0-947d-3696483fe29d`. It is separate from the compile-only
commit `3035529d21fd03ebdf29977ecbe5de842261e6cb` and does not change Bookmark
wire-v2, schema, policy defaults or either canonical golden.

## Confirmed cause and intended local semantics

Core account/consent revocation is immediate, but backend
`bookmark_sync_enabled_` and the Service's preferences/status can still be
approved until a later reply. Previously MergeLocalBookmarks,
ReadBookmarkProjection and AcknowledgeNativeBookmarks used only that cache.
An already-issued projection could likewise arrive during that window.

Local BookmarkModel editing remains usable. Revocation blocks further sync
journal/apply/ack operations from the old scope; it does not delete already
committed local bookmarks, records, pending outbox or retained encrypted input.
The report does not claim a provider upload/decryption network leak.

## Correction

- SyncProvider exposes a local thread-safe authorization callback. Mac captures
  the existing Core transport generation and weak lifetime, and checks the
  current BookmarkAllowed/account/recovery/shutdown state when invoked.
  Reapproval cannot revive an earlier generation. A provider without this
  contract fails closed for protected Bookmark operations.
- Backend adds its own cancel scope for global/category changes, shutdown,
  recovery and transition from provider-free preparation to a real provider.
  It combines that scope with the effective provider check. Provider-free local
  preparation still requires both explicit local opt-ins.
- Merge/read/ack check the effective scope before accessing the journal.
  Journal transactions check it again at commit, rolling back tentative
  bindings, receipts, records and outbox changes if authorization expired.
- Projections carry the original local authorization; Service checks it directly
  before starting the synchronous native apply. ACK requests and delayed ACK
  replies retain that same scope. Neither a cached preference nor a later
  reapproval can authorize an old reply.
- A further concrete asynchronous hop exists between Core dispatch and
  SyncPump's `BindPostTask` consumer. The pump now captures the original scope
  before the request and checks it before acknowledging Bookmark outbox entries
  or importing Bookmark input/advancing its token. Non-Bookmark batches keep
  their existing behavior; a default-off provider violation remains
  `provider_error`, while revoked queued work is `cancelled`.
- UI shows an account/approval-change issue rather than calling a rejected old
  projection successful. No wire creator field, v3 writer or new transport exists.

Commit/apply entry is the authorization linearization boundary. Work completed
before revocation is not retroactively erased; this does not promise rollback of
an already-started synchronous native apply. The concrete delayed-reply and
cached-approval cases are rejected before starting their mutation.

## Nine new regression sources

Four `BookmarkSyncAuthorizationTest` cases use real backend/journal methods,
the same open SQLite connection, and a loaded native BookmarkModel:

1. Provider revocation with a deliberately still-approved backend cache blocks
   Merge/Read/ACK and preserves records, outbox, bindings and receipts.
2. A delayed Service reply cannot apply after revoke or reapproval even with
   stale-approved Service preferences; a fresh scope can materialize a real node.
3. Category/global suspension independently cancels the backend scope.
4. An existing provider without an authorization guard fails closed.

Two Mac Core cases test immediate account revocation without a facade status
refresh and old-scope rejection across opt-out/reapproval/provider destruction.
One journal test revokes authority between entry and commit and checks rollback.
Two pump cases revoke after an upload reply was already posted or revoke and
reapprove after a posted download, without refreshing the pump's cached flag;
outbox ACK, record import and token advancement must remain absent.

These nine tests are written, NOT executed. Combined with the earlier source,
the Bookmark package has 104 sync cases plus three popup cases: 107 source cases,
not 107 passing tests. Existing shelf 11/11 and Mobile Build15 evidence do not
cover this correction.

## Preparation and next gate

Main reviewed the helper's actual test code. Independent source review found no
remaining concrete issue in the backend/Core/Service scope patch or the later
three-file pump/test delta. Pinned clang-format, GN formatting and owned diff
whitespace are source preparation only. The largest touched implementation
module remains below 800 lines. Golden SHA-256 values are unchanged:

- Bookmark: `b09a5f898a07351f4cd80a68521dffadb05e21abb9799c0d86d61672d244e443`.
- Shared-tab contract: `f640a7223c8bcb894625c2fc3041b2c561116c6879333b01ebe3a0b9b72f6777`.

Desktop retains checkout/out/build/sign/install/UI ownership. Build `90068`
on `dc01cb5` ended EXIT1 with four compile causes and no new successful receipt.
The three common compile-only fixes are already separately handed off in
`3035529`; Desktop's `1ea90da` fixes its fourth cause. Do not replay the old
proposed patch over this source. Integrate this correction only from its exact
committed handoff into the coordinated next candidate, not from uncommitted WIP.

The exact candidate needs its visible Bookmark/consent journey first, then
`ahoi_sync_unittests` and the affected sidebar suites, or a fresh documented
technical-E2E exception. No own build, test, runtime, account, key, server,
installation or Chromium pin action was performed for this correction.
Candidate-bound cross-account/real CloudKit and Mac–Mobile roundtrip acceptance
remain OPEN until executed; source review is not that evidence.

## Exact source scope

```text
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_authorization_unittest.cc
overlay/chromium/src/ahoi/browser/sync/BUILD.gn
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_bridge_types.h
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_consent_unittest.cc
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_journal.cc
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_journal.h
overlay/chromium/src/ahoi/browser/sync/bookmark_sync_journal_unittest.cc
overlay/chromium/src/ahoi/browser/sync/cloudkit_bookmark_sync_consent_unittest.mm
overlay/chromium/src/ahoi/browser/sync/cloudkit_sync_provider_mac.h
overlay/chromium/src/ahoi/browser/sync/cloudkit_sync_provider_mac.mm
overlay/chromium/src/ahoi/browser/sync/cloudkit_sync_provider_mac_consent.mm
overlay/chromium/src/ahoi/browser/sync/cloudkit_sync_provider_mac_internal.h
overlay/chromium/src/ahoi/browser/sync/profile_sync_backend.cc
overlay/chromium/src/ahoi/browser/sync/profile_sync_backend.h
overlay/chromium/src/ahoi/browser/sync/profile_sync_backend_bookmarks.cc
overlay/chromium/src/ahoi/browser/sync/profile_sync_service.h
overlay/chromium/src/ahoi/browser/sync/profile_sync_service_bookmarks.cc
overlay/chromium/src/ahoi/browser/sync/sync_provider.cc
overlay/chromium/src/ahoi/browser/sync/sync_provider.h
overlay/chromium/src/ahoi/browser/sync/sync_pump.cc
overlay/chromium/src/ahoi/browser/sync/sync_pump.h
overlay/chromium/src/ahoi/browser/sync/sync_store.h
overlay/chromium/src/ahoi/browser/ui/sidebar/sidebar_bookmark_sync_control.cc
```
