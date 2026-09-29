# Portable Workspace structure roundtrip — bounded visible journey

Date: 2026-09-23. Exact M153 source `455652bc0bde2bf9cb59cb3c8bd92a366e845822`;
guarded product-only build EXIT0, receipt
`artifacts/build/native-m153-workspace-reconcile-455652b-20260923/build-receipt.json`.
The Apple-Development-signed test copy at
`/private/tmp/ahoi-workspace-reconcile.ZPGZD0/AhoiBrowser.app` passed deep
signature verification and matched binary SHA-256
`fa975013dfa9a879287a4ffebf549d4604a77b4e0fd53d65d4429d11380a8112`.
Only a fresh disposable `complex-profile` and public example domains were used;
the installed app, CloudKit, Keychain and normal browser profile were untouched.

The 0600, 2,912-byte [source file](source-public.ahoi.json) (SHA-256
`b4dcd15ea2bd1ac0e418408e3d2388bd80b13bf5dd836d97d125fb8334515b85`)
contains one Tide Workspace with a Research folder, two saved HTTP(S) pages,
one Home URL, a horizontal two-page split (ordered IDs, 620000/500000 ratios),
and one manually archived two-page vertical split (400000/500000 ratios).
The archive ID is the deterministic ID required by the version-1 portable
codec. No secrets, local file targets or private URLs are present.

Visible native Settings Open/Import/Export/Repeat/Restart journey:

1. The native picker accepted the file. Preview showed Tide selected and
   `Neu: 6`, `Splits: 1`, `Archive: 1`, zero conflicts. One deliberate Import
   click returned `Import abgeschlossen`.
2. The Tide Workspace appeared. Research expanded to show Tide A and Tide B;
   archived pages did not appear as live Sidebar rows. Read-only SQLite showed
   the exact Workspace/folder/page IDs, parent relationships, Home URL and
   `quick_check=ok`. The existing structure-state field held the live split,
   archive entry and archive split with their exact ordered IDs and ratios.
3. Selecting only Tide plus `Archive einschließen` in normal Settings showed
   one Workspace, two pages, one split and one archive. Native Save produced
   the 0600, 2,136-byte [roundtrip file](roundtrip-public.ahoi.json) (SHA-256
   `c91127aa6b9c5a16b7b2abe7b68e101f347452a053c6feff0f92825c395c886b`).
   `diff -u <(jq -S . source-public.ahoi.json) <(jq -S . roundtrip-public.ahoi.json)`
   exited 0: every portable JSON field, including nested archive topology,
   matched semantically.
4. Reopening the roundtrip file showed Tide `Bereits identisch`, `Neu: 0`,
   `Bereits identisch: 6`, zero conflicts. The deliberate repeat returned
   `Bereits vorhanden – keine Änderungen`; SQLite still had one Tide Workspace,
   its five stored nodes (three live tree nodes and two archive-private page
   snapshots), three structure entries and `quick_check=ok`.
5. A normal `⌘Q`, exact-copy relaunch and `Fortsetzen` showed the ordinary
   startup choice, not crash recovery. Tide and Research returned; Tide A/B
   were visible after expanding the folder. The exact Home URL and three
   structure IDs were still present. Opening both saved pages left the
   logical split pending while a member was active; after switching to the
   Inbox Settings tab, the existing protected native projection materialized
   it. Returning to Tide visibly showed both real Chromium `WebContents`
   side by side with the imported 62% primary divider.

Limits: archive *restoration* and deletion UI, injected rollback failure,
cross-profile import, installed-app acceptance and Mac–iOS Sync remain open.
From a Tide Workspace with no live tab, the Settings command navigated to
`chrome://settings/` but the empty-workspace overlay remained over it; choosing
Inbox exposed Settings. This is a separate visible product failure, not a pass
for zero-tab Settings access. The files here are public-only test artifacts;
the disposable profile remains isolated under `/private/tmp` for correction.
