# Portable workspace files

Status: built and visibly accepted for bounded export and read-only file
preview on installed Mac source `820cf4e`; additive import is NOT implemented.
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

The detached decoder does not grant import authority. A later UI must show a
redacted preview and omissions, obtain the user's workspace/category/conflict
choices, then use an atomic additive commit and preserve rollback/idempotence.
No real-profile data is changed merely by parsing a file.

The installed candidate adds a bounded native Open dialog and read-only preview:
symlinks, non-regular/oversized files, invalid JSON and unsupported graphs are
rejected before any profile mutation. It intentionally offers no Import button
until the separate atomic transaction and rollback path are implemented.
The isolated real file roundtrip and a rejected unknown-version file are
recorded in [the Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md).
