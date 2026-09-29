# Arc history import: assessment and plan

Status: plan only (29 September 2026). Nothing described here is implemented.
The Arc import still excludes browsing history, as documented in
[the Arc import module](../overlay/chromium/src/ahoi/browser/importer/arc/README.md).

The master contract allows history "über Chromiums Importer-Seams als getrennt
auswählbare Kategorie". This note records why a clean implementation is not
possible as a pure overlay change today and what a correct one needs.

## Findings (Chromium M153, read-only inspection)

1. **Snapshot input already exists.** `CreateArcImportBackup()` copies each
   selected profile's `History`, `History-wal` and `History-shm` into an
   owner-only (0700/0600) backup directory. It captures one complete source
   generation twice, compares size, mtime and SHA-256 per file including the
   presence of the WAL/SHM sidecars, and refuses while Arc or any helper holds
   a profile file open. That copy is a consistent, quiescent SQLite triple and
   is the only acceptable read source. Arc's live files must never be opened.
2. **Writer seam.** `ProfileWriter::AddHistoryPage(URLRows, VisitSource)`
   forwards to `HistoryService::AddPagesWithDetails()`. The utility-process
   importers reach it through `ImporterBridge::SetHistoryItems()` with
   `user_data_importer::ImporterURLRow`. Arc is not a
   `user_data_importer::ImporterType`, and the Arc import runs in the browser
   process through `ArcImportService`, so the natural seam for Ahoi is
   `HistoryService::AddPagesWithDetails()` (the same call `ProfileWriter`
   makes), not a new utility importer.
3. **Not idempotent.** `HistoryBackend::AddPagesWithDetails()` adds one
   synthetic LINK visit at `last_visit` for every row, including rows whose URL
   already exists. Replaying the same snapshot duplicates visits.
4. **Not transactional.** The backend stops at the first failed
   `AddURL`/`AddVisit` and schedules a commit for what was written. There is
   no rollback, and no public "delete visits by source" API. Only
   `HistoryService::ExpireHistory(std::vector<ExpireHistoryArgs>)` can remove
   visits, restricted by URL set and time range.
5. **No Arc visit source.** `history::VisitSource` ends at
   `SOURCE_OS_MIGRATION_IMPORTED = 7`. Reusing `SOURCE_BROWSED` would make
   imported visits indistinguishable from local browsing. The
   `SOURCE_*_IMPORTED` values of other browsers would be dishonest.
6. **Only the latest visit per URL.** `URLRow` carries `visit_count`,
   `typed_count` and one `last_visit`. The per-visit timeline of Arc's
   `visits` table cannot pass this seam. That matches Chromium's own importers
   and is acceptable, but the preview must say "one entry per page".

Conclusion: reading is clean and testable in the overlay. Writing needs one
upstream enum value (a patch) plus an Ahoi-owned idempotence and rollback
layer. Without both, history import would break the Arc import's invariants
(no duplicates on replay, complete rollback on failure, honest provenance).

## Proposed design

### Category and UI

- New category checkbox "Verlauf" / "History" in the existing compact preview,
  default off, shown only when at least one selected profile has a `History`
  file in the backup set. The preview shows the planned page count and the
  oldest/newest visit date. It never shows URLs in logs or evidence.
- History can be committed alone or together with the sidebar. It shares the
  sidebar's backup, source revalidation and preview token.
- The category is part of `ArcImportTransactionSelection` and therefore of the
  selection fingerprint (append `history=1` only when on, as done for
  `folders_as_workspaces`).

### Reader (overlay, `//ahoi/browser/importer/arc:history_reader`)

- `arc_history_reader.{h,cc}` on a `MayBlock` sequence. Input: the verified
  backup directory and profile key. Copy the triple into a fresh private
  0700 temp directory (SQLite may rewrite `-shm` and checkpoint the WAL, so
  the backup itself stays untouched), then open it with `sql::Database`
  (`PRAGMA query_only=1`) and run `PRAGMA quick_check`.
- Accept only a Chromium history schema whose `meta` `version` is in a known
  range; otherwise fail closed with `kUnsupportedSchema`.
- Query `urls(url, title, visit_count, typed_count, last_visit_time, hidden)`
  ordered by `id`, bounded: at most 200 000 rows, URL ≤ `kMaxUrlBytes`,
  title ≤ `kMaxTitleBytes`, valid UTF-8, database file ≤ 1 GiB.
- Keep only credential-free HTTP(S) rows (`IsSafeImportUrl`), `hidden = 0`,
  and `last_visit_time` inside Chromium's history retention window. Count
  every drop reason in the stats; never log the rows.
- Convert WebKit-epoch microseconds with
  `base::Time::FromDeltaSinceWindowsEpoch()`. Delete the temp copy after
  reading, on success and failure.

### Writer and transaction (overlay plus one patch)

- **Patch proposal** (`patches/chromium`, new file after the current series):
  add `SOURCE_ARC_IMPORTED = 8` to `history::VisitSource`, map it in
  every switch that enumerates sources, and extend any histogram enum that
  mirrors it. No behaviour change for existing sources.
- **Idempotence:** before writing, resolve all planned URLs with the bulk
  `HistoryService::QueryUrlIds()`. For URLs that already exist, use
  `QueryURLAndVisits()` and drop the row if a visit at exactly `last_visit`
  exists. Record a history import key
  (`sha256(history triple hashes ‖ selection fingerprint)`) in the Arc
  journal; a replay with the same key is a journal-level no-op, exactly like
  the sidebar replay path.
- **Rollback:** the prepared journal lists, by count and a SHA-256 over the
  sorted URL set only, which (URL, last_visit) pairs this attempt adds. On
  failure or crash recovery, remove them with `ExpireHistory()` using one
  `ExpireHistoryArgs` per distinct timestamp restricted to the added URLs.
  URLs that did not exist before the attempt are deleted with `DeleteURLs()`.
  The journal never stores URLs or titles; the recovery path re-derives the
  pair list from the retained backup copy and the journal key.
- Write in bounded batches (for example 5 000 rows) through
  `AddPagesWithDetails(rows, SOURCE_ARC_IMPORTED)` and finish with the same
  flush barrier the sidebar import uses before marking the journal committed.

### Tests

- Reader unit tests (`ahoi_arc_import_unittests`): build a synthetic Chromium
  history database with `sql::Database` in a `ScopedTempDir`, leave rows only
  in the WAL (no checkpoint), copy the triple, and assert the reader sees
  them; malformed schema, oversize values, unsafe URLs, hidden rows, time
  conversion and the row limit each fail or drop as specified. No real Arc
  data.
- Transaction unit tests with `history::HistoryService` from
  `//components/history/core/test` (`HistoryServiceTestBase`-style setup):
  first import adds pages, replay adds nothing, a failure after one batch is
  fully expired again, pre-existing URLs keep their own visits.
- Browser test (`ahoi_arc_import_browsertests`): the category is off by
  default, the selection fingerprint differs with history on, and the
  journal contains no URL.

## Why not now

The seam needs the `VisitSource` patch for honest provenance, and the
idempotence and rollback layer is a new, multi-file transaction path that must
be built and run before it can be trusted. Under the current constraints (no
builds, patches only as proposals) it would ship untested code into the
commit path of an import that promises "complete rollback or unchanged
profile". The sidebar import is unaffected by this decision.
