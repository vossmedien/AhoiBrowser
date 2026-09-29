# ADR 0010: Restore the browser setup, not only an extension inventory

Status: binding additional user scope, 2026-09-05. Implementation/acceptance open.
The user explicitly requests install + link on another computer followed by
automatic restoration of browser settings and extensions, and their own
settings where feasible. Cookies and passwords remain excluded. The current
five Ahoi preferences plus read-only extension inventory do NOT satisfy this.

## Required product outcome

- Two linked macOS Ahoi installations converge their workspaces, normal saved
  and temporary tabs, bookmarks, applicable history/appearance and transferable
  browser configuration while both are already running.
- Browser configuration includes the native Chromium user settings which can
  safely and meaningfully apply on another machine, not just Ahoi's appearance
  switches. Prefer Chromium's syncable-preference metadata and native services.
  Produce an explicit supported/excluded settings catalogue; do not label a
  limited catalogue as all of `chrome://settings` without checking coverage.
- Extensions have a shared desired installation/enabled state. Missing trusted,
  supported extensions are downloaded and installed through native verified
  install machinery. Installed/pending/blocked/confirmation-required/failed are
  real states, not success inferred from a synced inventory record.
- Native permission prompts, signature/ID/source checks, managed policies and
  Ahoi's existing MV2/uBO restrictions remain effective. Never auto-click consent
  or reuse an unrelated Google-account trust path to grant permissions.
- Supported extension-owned settings also converge. `chrome.storage.sync` is
  the first integration seam, not a promise that every extension exposes its
  settings there or that arbitrary data in that namespace is secret-free.
- iOS shares the logical data and applies its supported settings. It preserves
  recognized desktop-only configuration as metadata, but does not pretend to
  install or execute Chromium extensions in WebKit.

## Native reuse and one sync owner

Reuse the actual Chromium Profile, PrefService, extension registry/prefs,
installer/enable flow and storage API. Keep the existing Ahoi CloudKit transport,
encryption and account model. Do not copy profile directories or construct a
second cookie/extension database or a second cloud engine.

The M152 source has `ChromeSyncablePrefsDatabase::GetSyncablePrefMetadata`,
`PrefService::IteratePreferenceValues`/`GetUserPrefValue`, `PrefChangeRegistrar`,
native extension install/enable flows and `StorageFrontend::{GetValues,Set,Remove}`.
These are integration candidates, not implemented/verified adapters. Native
Google Sync processors (`MergeDataAndStartSyncing`, `SyncStorageBackend`) must
not be taken over or run with a competing Ahoi writer. Establish sole ownership
of the applicable native sync surfaces before activation.

`StorageFrontend::GetObserver()` is a callback entry point, not an observer
subscription. A real settings-change hook must precede its no-JS-listener early
return, preserve backend/UI sequence ownership and carry apply origin/authority
across asynchronous calls. Exact upstream patches remain Desktop-owned.

## Safety and fidelity boundaries

Cookies are technically transferable; exclusion is the user's privacy choice,
not a claim of impossibility. Password stores, credentials/tokens, private tabs,
Keychain material and secret headers remain local. This scope does not authorize
raw autofill/payment data, site/session storage, machine paths, OS grants or
managed-policy overrides. Cookie-behavior preferences are distinct from cookies.

Use actual user preference values rather than effective managed/extension-
controlled values. Apply only compatible registered types, preserving policy
and local-only/platform constraints. Reset-to-default must remain a real shared
user intent, distinct from an empty fresh installation or lack of sync consent.

For hard secret exclusion, extension settings require positive, reviewed
extension-ID/key/value schemas or an equally explicit safe contract. A namespace
name or denylist of suspicious keys cannot prove arbitrary values contain no
password/API token. Unknown or opaque extension data stays local and the support
gap is reported. Do not silently copy `storage.local`, `storage.session`,
`storage.managed`, extension IndexedDB or raw profile files.

An empty extension inventory on a fresh machine is not uninstall intent. Native
restore/apply callbacks must not reauthor themselves; genuine later user
uninstall/disable/default-reset must not be undone by an older linked client.
Capture local intent durably, fetch the shared setup before publishing fresh
defaults, then restore through native authorities with restart/retry receipts.

## Format, ownership and acceptance

ADR 0009's ONE active format 3 and fresh isolated acceptance remain binding.
The earlier 13-class golden is a base fixture, NOT a ceiling on this new scope
or proof that full setup restoration exists. Publish matching C++/Swift field
maps and extend the same canonical fixture before activating new data types.
Do not overload device-specific observed inventory as shared desired state.

The unified owner retains Common C++, Swift, contract/policy/goldens and the
overall implementation. Desktop retains its current native Tree/Session/UI,
patch-stack and build/install/runtime ownership. New native settings/install/
storage hooks require a concrete file-level handoff; this ADR grants no competing
checkout/build or live extension installation, portal/key or Production action.

Acceptance: from an isolated configured Mac A, install/link a fresh Mac B and
show settings actually applied and a supported extension actually installed and
usable; change a supported extension preference and observe B update. Exercise
both directions, already-open clients, offline/restart, disable/uninstall and
default-reset without echo/reinstallation. Show unsupported/prompt-required
states honestly. Then verify iOS's matching logical data and supported mappings.
Visible candidate journeys precede programmatic suites; no existing Build15 or
26-payload fixture result substitutes for this expanded runtime proof.

Reference: Chrome's [storage API documentation](https://developer.chrome.com/docs/extensions/reference/api/storage)
separates local/session/managed/sync areas and identifies sync as an intended
settings surface; [permission documentation](https://developer.chrome.com/docs/extensions/develop/concepts/declare-permissions)
describes installation/runtime and file/incognito consent boundaries.
