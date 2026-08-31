# Zen import compatibility

Zen appears in Chromium's normal `Browserdaten importieren` dialog only when an
exact, non-symlinked Zen/Zen Twilight application bundle and a safe regular
profile expose categories already supported by Chromium's Firefox importer.
Live-process discovery recognizes only executables inside those exact bundle
names; stale Firefox lock files are not treated as process evidence. On macOS
the currently advertised categories are history/bookmarks from
`places.sqlite` and form history from `formhistory.sqlite`; password import is
not advertised because modern macOS code signing cannot load Firefox/Zen NSS
libraries into Chromium.

The structure boundary is intentionally narrower. Discovery recognizes the
bounded Mozilla-LZ4 container `zen-sessions.jsonlz4` used by upstream Zen at
revision `e89bd7796e2dcecaf0c483a795225ed9ec549bbd`, but does not claim that a
header match proves a compatible schema. Spaces, folders, pins and split data
remain disabled until a versioned decoder/parser, transaction/rollback path and
a visible real-profile acceptance run exist. Fixtures prove only application,
process, profile, capability and path-safety behavior.
