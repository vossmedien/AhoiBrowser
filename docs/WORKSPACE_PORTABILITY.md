# Portable workspace files

Status: bounded export and read-only file preview visibly accepted on installed
Mac source `820cf4e`. The newer additive transaction and whole-Workspace
selection are built and visibly exercised in separate signed, isolated
profiles: an independent new Workspace can be imported while a conflicting
Workspace is explicitly skipped, then replayed as NoChanges after restart.
The corrected candidate is source `455652b`; it is not installed. A bounded
complex file with nested hierarchy, Home URL, live split and archived split
has also passed native import, semantic re-export, NoChanges replay, restart
and protected native split projection. Rename/merge choices and broader
archive restoration/rollback acceptance remain open.
The active product requirement is in
[the master goal](../outputs/AhoiBrowser-Master-Zielprompt.md#portabler-workspace-export-und-wiederimport).

## One file format

The file is UTF-8 JSON with exactly these top-level fields:
`format: "ahoi-workspaces"`, `version: 1`, `workspaces`, `nodes`, `splits`,
`archives`. IDs are lowercase UUIDs. The format is independent of Ahoi Sync
wire records, SQLite snapshots and Arc backups. Its maximum encoded size is
16 MiB; the decoder rejects unknown versions and malformed or oversized data
before any destination-profile action.

- `workspaces`: ID, name, icon, manual `sort_key`, optional hexadecimal ARGB
  accent and archive policy.
- `nodes`: ID, workspace/optional folder parent, `folder` or `page`, title,
  icon, manual order and optional accent. Pages additionally carry the saved/
  temporary distinction and a target of `web` with credential-free HTTP(S)
  URL or explicit `new-tab`. An optional saved Home is a web target.
- `splits`: logical ID, workspace, two to four ordered page IDs, axis,
  arrangement and two ratios in integer millionths. All members must be
  selected normal pages in that workspace.
- `archives`: deterministic archive ID, workspace, reason, Windows-epoch
  microsecond timestamp, one to four complete page snapshots and optional
  matching split topology. Archived pages are not duplicated as live nodes.

The encoder receives only explicit portable types. It never receives private
nodes, cookies, credentials, permissions, extension storage, file paths,
native window/tab handles, Sync clocks, pairing keys or the local undo log.
Nonportable local targets and Home values are excluded and counted by the
selection stage. The output is **not encrypted**; the save UI must say so and
must not upload or share it automatically.

The detached decoder does not grant import authority. The current UI shows a
local destination preview, and only a conflict-free proposal can acquire a
one-use commit token. On click, the SessionBridge replans against the durable
profile and writes tree plus split/archive structure through one existing
SQLite transaction. The source file is never mutated. Changed IDs or same-name
Workspace conflicts block the selected proposal's commit. Explicit
whole-Workspace skip is available; rename/merge decisions remain to be
implemented. No real-profile data is changed merely by parsing a file.

The installed `820cf4e` candidate adds a bounded native Open dialog and read-only preview:
symlinks, non-regular/oversized files, invalid JSON and unsupported graphs are
rejected before any profile mutation. It intentionally offers no Import button
until the separate atomic transaction and rollback path are implemented.
The isolated real file roundtrip and a rejected unknown-version file are
recorded in [the Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md).
The newer target comparison distinguishes new, identical and colliding IDs for
Workspaces, nodes, splits and archives without a mutation. A short real
disposable-profile import, repeat and restart journey on a signed M153 clone
proves one public page addition and NoChanges replay, with stable ID and valid
SQLite readback. See the [candidate-bound evidence](../artifacts/e2e/portable-workspace-import-51d7e79-20260923/README.md).
The installed-app pass and the nontrivial conflict/split/archive/rollback
journeys remain open.

Source `9f17ada` additionally offers explicit whole-Workspace selection in
the same compact preview. It skips a visibly conflicting Workspace while
including an independent safe Workspace, with backend UUID/subtree validation
before the existing atomic commit. The mixed-file Harbor-only commit was
visibly executed on signed `38eebb9`. A separate Sidebar/TabStrip reentrancy
crash on switching from Harbor to the existing Settings tab was corrected in
`455652b`; its exact signed clone passed that changed navigation, identical
file NoChanges replay and normal restart on the same isolated profile. See
the [current Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md). This simple
mixed-file case alone does not prove rename/merge, nontrivial structure,
rollback fault handling or installed-app acceptance.
The separate [complex-structure journey](../artifacts/e2e/portable-workspace-complex-455652b-20260923/README.md)
proves one nontrivial portable split/archive file roundtrip and normal restart;
it does not yet prove archive-restore actions or fault-injected rollback.
