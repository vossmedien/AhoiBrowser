# 072 – A restored split comes back when its member is opened

Status: accepted on build 39 (79e35f3): split-archive-restore-journey 5/5, the restored split comes back when its member is opened
Owner lane: desktop (apply, build, split journey)
Base: HEAD `5551bff` (`git apply --check` passes; builds on `3dfeb17`).
Syntax-checked read-only (0 errors), not built.

## Finding (build 37, split-archive-restore journey)

Archive, listing and restore pass (034, 76f6d94). "restore_brings_split_back"
still fails, although `3dfeb17` works: the window menu lists both
`Ahoi split left` and `Ahoi split right` as open tabs after the click, and
only the left page is visible.

Cause: the split is rebuilt only by the passive `MaterializeSplits` →
`MaterializeNativeSplit`, which requires `CanArchiveTab()` for every member
(`native_workspace_structure.cc:166-175`). `ResourcePolicyService::CanArchiveTab`
is false for `IsActivated()` and `IsLoading()`
(`resource_policy_service.cc:182,200`). The page the user just opened is
both, so the split is deferred for as long as the user stays on it. The
gate is right for passive and remote changes ("never disturb an active,
loading or form pane"), but here the user's activation **is** the
operation. By the single-writer rule, that operation should commit the
structure itself instead of waiting for an observer.

## Change

- `MaterializeNativeSplit(..., bool user_initiated = false)`: only
  `user_initiated` skips the `CanArchiveTab` gates. Every other check stays
  (members bound, one Workspace, one window, same pinned state and group,
  authority).
- `WorkspaceStructureController::MaterializeSplitForActivation(node)` finds
  the live split record containing the node (skips archived members) and
  materializes it with `LocalAuthority()` inside the `applying` scope. On
  success it records the native token and observed metadata, like
  `MaterializeSplits` does.
- `SessionBridge::MaterializeSplitForActivation(node)`.
- `ActivateSavedPage` calls it after opening the requested page whenever
  the page belongs to a live split with more than one member (partners
  opened just now or already open).

## Tests

No unit harness for the controller or sidebar host. Acceptance is the split
journey's `restore_brings_split_back`, with both panes visible after
clicking the restored row. A second check: activating a split member whose
partner is open but not split (e.g. after a manual unsplit) must not re-split
it. It does not, because a manual dissolve tombstones the record and only
live records qualify (and 058 lets archive tokens expire).
