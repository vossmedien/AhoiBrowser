# Arc history import: design and status

Status (7 October 2026): main donor28deb798 ported to pinned M155 in Root850aa587, **not yet built or run**. The integration follows the captured-current-time cutoff below; the affected native reader regression is still NOT_RUN.
Overlay code lives in
[the Arc import module](../overlay/chromium/src/ahoi/browser/importer/arc/README.md)
(`arc_history_*`, `arc_import_service_history.cc`); the upstream seams are
[patch 0084](../patches/chromium/0084-ahoi-arc-imported-visit-source.patch).
Open gates: guarded build, `ahoi_arc_import_unittests`, the Settings WebUI
test, installed-product import of the real Arc profile with a visible no-op
replay.

The master contract allows history "über Chromiums Importer-Seams als getrennt
auswählbare Kategorie". The first version of this note (plan only) explained
why a pure overlay change was not enough. The findings still hold; the design
below resolves them with one small patch and an Ahoi-owned transaction.

## Findings (Chromium M153, read-only inspection)

1. **Snapshot input already exists.** `CreateArcImportBackup()` copies each
   selected profile's `History`, `History-wal` and `History-shm` into an
   owner-only (0700/0600) backup directory, captures one complete source
   generation twice and refuses while Arc holds a profile file open. That copy
   is the only read source. Arc's live files are never opened.
2. **Writer seam.** `ProfileWriter::AddHistoryPage()` forwards to
   `HistoryService::AddPagesWithDetails()`. Arc is not a
   `user_data_importer::ImporterType` and runs in the browser process, so the
   seam is `HistoryBackend::AddPagesWithDetails()`, reached through one
   `HistoryDBTask`.
3. **Not idempotent upstream.** `AddPagesWithDetails()` adds one LINK visit at
   `last_visit` per row, also for existing URLs.
4. **Not transactional upstream.** The backend batches writes in a singleton
   transaction that commits on a 10 s timer; `Commit()` is private. There is
   no "delete visits by source" API, and `ExpireHistory()`/`RemoveVisits()`
   decrement `visit_count` of the URL row although `AddPagesWithDetails()`
   never incremented it for existing URLs, so they are not an exact inverse.
5. **No Arc visit source.** `history::VisitSource` ended at
   `SOURCE_OS_MIGRATION_IMPORTED = 7`.
6. **Only the latest visit per URL.** `URLRow` carries counters and one
   `last_visit`; the per-visit timeline cannot pass this seam. That matches
   Chromium's own importers; the UI says "one entry per page".

## Implemented design

### Category and UI

- Checkbox "Browserverlauf (ein Eintrag pro Seite)" / "Browsing history (one
  entry per page)" in the existing compact preview, shown only when at least
  one Arc profile has a `History` file (presence check, no read) and history
  saving is not disabled by policy.
- **Default on.** The contract allows history as a separately selectable
  category and names no default; Chromium's standard import preselects
  history, and the owner asked for history. The earlier plan said "default
  off"; this is a deliberate change.
- History can be committed alone or together with the sidebar. The primary
  import action is the only confirmation, as for the sidebar.
- **Preview shows availability, not counts.** Counting pages before the
  commit would mean reading Arc's live database or taking a second snapshot
  outside the backup. The result reports new, already present and
  too old/excluded pages instead.
- The strings are TypeScript fallbacks (DE/EN) behind `loadTimeData` keys,
  like `ahoiArcImportFoldersAsWorkspaces`, because the Settings strings share
  `generated_resources_*.xtb` with patch 0083. A later strings patch adds
  `ahoiArcImportHistoryCategory`, `...HistorySuccess`, `...HistoryAdded`,
  `...HistoryPresent`, `...HistorySkipped`, `...HistoryPending`,
  `...HistoryFailed` and `...PrivacySublabelWithHistory`.

### Transaction boundary

- History is its **own transaction after the sidebar transaction committed**,
  not part of the sidebar selection fingerprint. Adding history later
  therefore never re-runs a committed sidebar import, and a history failure
  never rolls back a committed sidebar; the result reports it separately.
- It reuses the backup the sidebar commit created, or creates one (sidebar
  not selected, or a sidebar no-op).

### Reader (`arc_history_reader.{h,cc}`, `CopyArcHistoryFromBackup()`)

- The backup is verified like tree recovery (manifest digest, every payload
  hash, exact directory listing). `History` and its WAL are copied into a
  private temporary directory; SHM is not copied because SQLite rebuilds it
  from the WAL. The copy is opened with `PRAGMA query_only=1`,
  `PRAGMA quick_check` must return `ok`, `meta.version` must be 40-99 and the
  `urls` columns in use must exist; otherwise `kUnsupportedSchema`.
- Limits: 200 000 pages (newest kept, rest counted), 2 000 000 source rows
  (more fail closed), 1 GiB per database and WAL, URL 32 KiB, title 4 KiB of
  valid UTF-8 (otherwise imported untitled).
- Only `hidden = 0`, credential-free HTTP(S) rows inside Chromium's history
  retention window (`HistoryBackend::kExpireDaysThreshold`, 90 days) are
  kept; future timestamps count as invalid. WebKit-epoch microseconds are
  converted with `base::Time::FromDeltaSinceWindowsEpoch()`.
- Profiles merge deterministically: one entry per canonical URL, the newest
  visit and title win, counters add up. The content key is SHA-256 over the
  backed-up History and WAL hashes per profile key.

### Writer (`arc_history_writer.{h,cc}`) and patch 0084

- Patch 0084 adds `SOURCE_ARC_IMPORTED = 8` (mapped in
  `VisitSourceFromInt()`, the only exhaustive switch),
  `HistoryBackend::CommitForAhoiImport()` (actual singleton-commit result plus a successfully opened successor, not a void Commit wrapper) and
  the sql histogram tag `AhoiArcHistoryImport` for the reader's database.
- One `HistoryDBTask` run: commit pending unrelated work, require the
  singleton transaction (`HasActiveTransactions()` on M155; no overlay-created nested transaction), skip expired pages, skip pages whose URL
  already has a visit at exactly `last_visit` (idempotence), write the rest
  with `AddPagesWithDetails(rows, SOURCE_ARC_IMPORTED)`, verify every page
  (URL row plus an Arc-sourced visit at that time), commit. No other history
  work can interleave inside that run.
- **Rollback (refined).** Instead of `ExpireHistory()`, which is not exact
  (finding 4): visits added to existing URLs are deleted with
  `VisitDatabase::DeleteVisit()` (the exact inverse of `AddVisit()`; no
  observer was notified for them), and URLs created by the batch are deleted
  with `HistoryBackend::DeleteURLs()`, which notifies observers that were told
  about them. Unit tests compare the full URL row and visit list before and
  after.
- No 5 000-row batches: interleaving between batches would make the exact
  rollback impossible. Chromium's own importers also write all rows in one
  backend task.

### Journal and recovery (`arc_history_journal.{h,cc}`, runner)

- `Ahoi/ArcHistoryImportJournal.json`, separate from the sidebar journal,
  owner-only, atomically replaced. States: `prepared` (content key, backup
  identifier, manifest and snapshot digests, previous committed state) and
  `committed` (content key, counters). No URL, title, path or profile label.
- A commit with the committed content key is a no-op without touching the
  history database. Any other replay is a data no-op by the exact-visit rule.
- Exact rollback restores the previous journal. A crash leaves `prepared`;
  the batch is atomic in SQLite, and the next discovery re-reads the bound
  backup and finishes the import (pair idempotence adds only what is missing)
  before any preview. If the backup can no longer be verified, the marker is
  released: every page already written is a complete Arc visit, and a later
  import skips it. Commit refuses while a history import is prepared.

### Tests

- `ahoi_arc_import_unittests` (`//ahoi/browser/importer/arc:unit_tests`):
  `arc_history_reader_unittest.cc` (filters, limits, schema/damage, merge,
  WAL-only rows through the real backup path, Arc file unchanged),
  `arc_history_writer_unittest.cc` (`HistoryService` from
  `//components/history/core/test`: first import, replay no-op, existing page
  keeps its row, exact rollback, expired and unsafe input) and
  `arc_history_journal_unittest.cc` (round trip, owner-only, restore, fail
  closed, no URLs). All fixtures are synthetic.
- Settings WebUI test `ahoi_arc_import_section_test.ts`: default-on history
  checkbox only when available, history-only commit, count-only result and a
  history failure beside a committed sidebar.
- Public-commit browser cases for history-only, combined and sidebar-no-op are now Source in the existing target; none have run.

## Current integration boundaries

The current [M155 handoff](reviews/2026-10-07-arc-history-m155-handoff.md) supersedes the older donor implementation details. Optional argument10 remains the selected separated-profile list; optional argument11 selects history. Each selected target uses its own validated active Profile/HistoryService, saving policy, journal, keepalive and shutdown; missing separated targets never fall back to main. Source-filtered backups and prepared recovery retain that ownership. Sidebar, separated creation and history share the operation guard until the terminal reply, and history failure preserves an already committed sidebar.

Root follows the current [integration contract](reviews/2026-10-07-desktop-main-integration-contract.md): one combined Crest/M155/History candidate, visible history-only/combined/replay/separated/rollback/recovery paths, then focused checks and a native review before default integration and guarded delivery. Source checks and historical main receipts do not establish those passes.
