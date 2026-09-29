# Arc sidebar import

This macOS-only module implements the explicit Settings workflow for importing
Arc sidebar workspaces into the current regular AhoiBrowser profile. Discovery,
preview, backup, commit, native split reconstruction, and the import journal are
kept behind one profile-scoped `ArcImportService`; the Settings handler never
owns import state.

The user-visible workflow is deliberately two-phase:

1. Discovery verifies that the Arc app, its helpers, and all Arc profile file
   handles are closed. The application bundle must satisfy Arc's Apple code
   requirement for `company.thebrowser.Browser` and Team ID `S6N382Y83G`; an
   Info.plist claim alone is not installation evidence. Discovery then reads
   an immutable, bounded snapshot and returns a
   detached preview with selectable Chromium profiles, categories, destination
   workspaces, conflict counts, degradations, and excluded-item counts. This
   ordinary discovery path cannot mutate AhoiBrowser or Arc. If a durable
   prepared journal exists, the service resolves that recovery gate before
   discovery: it may roll an exact importer-owned expected tree back to the
   verified previous backup, but never mutates an unrecognized tree.
2. Commit requires the preview token plus separate backup and import
   confirmations. It revalidates the source, creates an owner-only safety
   backup, performs one additive tab-tree transaction, reconstructs validated
   native splits, durably flushes the Ahoi tab tree, resets and reads back the
   exact Chromium Current Session file, and writes a privacy-minimal journal.
   A repeated commit is a no-op only after both the live split model and the
   durable native-session receipt match the selected snapshot.

Security invariants:

- only schema version 1 of `Arc/StorableSidebar.json` below the macOS
  Application Support directory is accepted;
- only an intact Apple-signed Arc bundle matching the official identifier and
  Team ID is accepted; unsigned, ad-hoc, differently signed, malformed, or
  structurally spoofed bundles fail closed;
- traversal, a symlinked Application Support root, Arc root, or source file is
  rejected;
- selectable browser profiles are immediate, regular `Default` or `Profile N`
  children of Arc's `User Data` directory;
- a running Arc bundle process/helper or any positively observed open sidebar
  or selected-profile handle blocks both discovery and commit; an inaccessible
  unrelated process is not treated as proof that Arc is running;
- source size, JSON depth, workspaces, items, children, identifiers, text, URLs,
  and tree depth are bounded;
- a source file changed while being read is rejected;
- serialized-map duplicates, missing references, cycles, multi-workspace
  ownership, and parent/child disagreements reject the complete parse;
- only credential-free HTTP(S) URLs are planned; unsafe URLs are counted and
  omitted;
- generated Ahoi identities are deterministic and domain-separated;
- validated split plans contain two to four ordered members, orientation,
  focus, and normalized ratios; an invalid split is retained as a named folder
  without synthetic tabs;
- conflict handling is explicit (`rename`, `skip`, or `merge`) and never
  silently overwrites an existing node;
- the safety backup includes the selected Arc profile databases and their
  present WAL/SHM sidecars plus the current Ahoi tab-tree database. Directories
  are mode 0700 and files/manifests are mode 0600. A complete hash, size,
  timestamp, creation-time, and presence fingerprint is captured before and
  after the whole copy window, so any database/WAL/SHM generation change
  rejects and removes the backup;
- each backup is limited to 2 GiB across at most 384 present payload files and
  must leave at least 256 MiB free after its payload estimate. Before creating
  a backup, retention keeps room for a maximum of three validated backups;
  directories referenced by a prepared journal are never deleted and can
  temporarily exceed that ordinary retention count;
- journal schema 5 stores transaction/selection/idempotency metadata, backup and
  manifest identifiers, affected deterministic Ahoi IDs, state/counters,
  canonical SHA-256 fingerprints of the previous and expected tab trees, and
  privacy-safe expected/native-session receipt hashes. It never stores imported
  titles, URLs, source identifiers, profile paths, native split IDs, or secrets.
  Schema-1/2/3/4 committed records remain readable; older prepared records are
  manual recovery gates and never authorize mutation;
- prepared recovery recomputes the verified backup and current tree
  fingerprints off the UI sequence. A current previous tree restores only the
  prior journal, a current expected tree is rolled back exactly once and
  durably read back, and every foreign tree remains untouched behind a manual
  recovery gate;
- transaction and tab-tree persistence failures restore the exact previous tree
  only before native split/window mutation may have started and while the live
  tree is still importer-owned. After that boundary, no one-sided compensation
  is attempted: both stores remain untouched behind a durable manual-recovery
  gate. A `runtimeMayHaveStarted` restart is therefore always manual. A live
  transaction may publish after its freshly flushed `runtimePersisted` Current
  Session receipt, but a restart cannot: schema 5 does not store the full
  runtime plan needed for exact revalidation, so startup preserves the prepared
  journal as a manual-recovery gate rather than reporting false success. A
  successful live commit requires exact tree, topology, order, visual data,
  the focused tab in the active target window, flushed Current Session
  readback, and post-worker live revalidation;

Arc remains read-only throughout. Passwords, cookies, form data, browsing
history, extension state, credential-bearing URLs, local files, and unsupported
Arc items are excluded. The module also does not install extensions.

The Settings surface is registered through the canonical Chromium integration
patch and provides DE/EN strings, keyboard-accessible controls, progress/result
announcements, and two independent confirmations. Source and WebUI tests cover
the real schema-1 serialized-pair form, partial split factors, preview
non-mutation outside prepared recovery, conflict policies, canonical tree
fingerprints, journal durability/privacy, the pure prepared-recovery state
machine, backup tamper/link rejection, and the two-confirmation UI contract.
Installed-product and native interaction evidence must still be collected with
the real app in addition to those programmatic tests.

Recovery deliberately remains fail-closed after the native mutation boundary:
a crash after Current Session persistence but before final committed-journal
publication requires explicit manual recovery. Startup never infers window
focus from a stale receipt and never guesses at native-window rollback.
