# Crest-hardening handoffs

Contributions of the `crest-hardening` lane to paths owned by other lanes.
The lane never writes those paths directly (see
[`config/agent-lanes.json`](../../config/agent-lanes.json)).

Each handoff lives in `<NNN>-<topic>/`:

- `HANDOFF.md` with `Status:` (`draft`, `ready`, `deferred <reason>`,
  `integrated <commit>`), owner lane, purpose, target paths, how to apply,
  expected tests and risks;
- the files to apply, mirrored under their repository paths (for example
  `files/overlay/chromium/src/...`) or as a `.patch` against a named base commit.

The owning lane takes `ready` handoffs into its next planned package, updates
only the `Status:` line of `HANDOFF.md`, and records the integration in its own
checkpoint. Taking over a handoff never justifies an extra build.
