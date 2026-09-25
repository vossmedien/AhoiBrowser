# 007 – Acceptance catalogue for Workspace isolation levels (H6)

Status: ready
Owner lane: desktop (Master acceptance matrix, `config/test-registry.json`)
Base: ADR 0011 at `f811604`

## Purpose

ADR 0011 adds the levels `website-sessions` and `isolated`. These 13 cases make
the contract testable across creation, separation, extensions, switching,
moves, routing, deletion, crash recovery, conversion, import, cost and sync.

## Apply

Both target files currently hold uncommitted desktop work, so this handoff is
content, not a patch:

1. Append the lines of `master-matrix-lines.md` to the section
   `### Tree, Tabs und Workspaces` of `outputs/AhoiBrowser-Master-Zielprompt.md`.
2. Append the objects of `registry-entries.json` to `tests` in
   `config/test-registry.json`, keeping the registry's order convention.
3. Run `python3 -m unittest tests/repository/test_requirement_audit.py`.

All cases start `NOT_RUN`. WS-ISO-12 is shared with the Sync and Mobile
owners; WS-ISO-13 is the acceptance of handoff 003.
