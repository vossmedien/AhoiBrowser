# 150 — Trace every Workspace switch path (PERF-04 coverage)

Status: integrated by desktop (owner): one `Ahoi.Workspace.Switch` in
`WorkspaceService::SetActiveWorkspace` after the no-op returns, which
`ActivateRelative` also reaches (so no second event there); removed from
`SessionBridge`. Rides with build 46; H3 rerun pending.
Owner lane: desktop

## Finding (H3 validation ws16/ws17, installed build 45)

The H3 Workspace-switch driver completed 20 switches and the guard completed,
but the trace held **no** `Ahoi.Workspace.Switch` event (0 samples;
`artifacts/perf/4746af81-20260928-validation-ws17/trace-r0a0.json` keeps all
`Ahoi.*` events and has none). The event lives only in
`SessionBridge::SetActiveWorkspaceForWindow`
(`session/session_bridge_workspace.cc:69`). The sidebar Workspace indicators
(`ActivateWorkspaceAtIndex`, `ui/sidebar/browser_sidebar_host_workspace_transition.cc:20-47`),
keyboard/gesture relative switches and the animated transition use
`SessionBridge::ActivateRelativeWorkspaceForWindow` →
`WorkspaceService::ActivateRelative` (`session_bridge_workspace.cc:78-91`),
which is not instrumented. So PERF-04 measures only menu/command switches and
misses the most common paths.

## Minimal change

Move the single `TRACE_EVENT("browser", "Ahoi.Workspace.Switch")` to the
commit points every path shares: `WorkspaceService::SetActiveWorkspace` and
`WorkspaceService::ActivateRelative` (`navigation/workspace_service.cc`), and
remove it from `SessionBridge::SetActiveWorkspaceForWindow`, so nested
same-name events cannot double-count a switch. Keep the scope around the
synchronous observer notification (that is the commit PERF-04 means).

## Acceptance

Unit (optional): a trace-enabled test observing one event per switch for
both entry points. H3: rerun the Workspace-switch trace (Crest driver) on the
next candidate; expect 20 `workspace_switch_ms` samples and per-compositor
presented latencies.
