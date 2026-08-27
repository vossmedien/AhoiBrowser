# Arc sidebar import

This macOS-only module implements the explicit Settings workflow for importing
Arc sidebar workspaces into the current regular AhoiBrowser profile. Discovery,
preview, backup, commit, native split reconstruction, and the import journal are
kept behind one profile-scoped `ArcImportService`; the Settings handler never
owns import state.

The user-visible workflow is deliberately two-phase:

1. Discovery verifies that the Arc app, its helpers, and all Arc profile file
   handles are closed. It reads an immutable, bounded snapshot and returns a
   detached preview with selectable Chromium profiles, categories, destination
   workspaces, conflict counts, degradations, and excluded-item counts. This
   stage cannot mutate AhoiBrowser or Arc.
2. Commit requires the preview token plus separate backup and import
   confirmations. It revalidates the source, creates an owner-only safety
   backup, performs one additive tab-tree transaction, reconstructs validated
   native splits, durably flushes the result, and writes a privacy-minimal
   journal. A repeated commit of the same snapshot is a no-op.

Security invariants:

- only schema version 1 of `Arc/StorableSidebar.json` below the macOS
  Application Support directory is accepted;
- traversal, a symlinked Application Support root, Arc root, or source file is
  rejected;
- selectable browser profiles are immediate, regular `Default` or `Profile N`
  children of Arc's `User Data` directory;
- a running Arc bundle process, helper, open sidebar file, or open selected
  profile database blocks both discovery and commit;
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
  are mode 0700 and files/manifests are mode 0600;
- the journal contains only snapshot hash, state, and counters. It never stores
  imported titles, URLs, source identifiers, profile paths, or secrets;
- any transaction, persistence, or runtime reconstruction error restores the
  exact previous tab tree and closes tabs opened by that attempt. Recovery of a
  durably committed tree with a missing journal reconstructs only the
  deterministic pending split plan before finalizing the journal.

Arc remains read-only throughout. Passwords, cookies, form data, browsing
history, extension state, credential-bearing URLs, local files, and unsupported
Arc items are excluded. The module also does not install extensions.

The Settings surface is registered through the canonical Chromium integration
patch and provides DE/EN strings, keyboard-accessible controls, progress/result
announcements, and two independent confirmations. Source and WebUI tests cover
the real schema-1 serialized-pair form, partial split factors, preview
non-mutation, conflict policies, rollback/idempotence contracts, and the
two-confirmation UI contract. Installed-product and native interaction evidence
must still be collected with the real app before running those programmatic
tests.
