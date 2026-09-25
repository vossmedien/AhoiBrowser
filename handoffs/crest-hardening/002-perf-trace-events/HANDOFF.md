# 002 – Trace events for command bar and Workspace switch (H3)

Status: integrated d425c3b
Owner lane: desktop
Base: overlay files at `8821ed1` (patch applies with `git apply --check`)

## Purpose

`PERF-03` (command bar p95 < 50 ms) and `PERF-04` (Workspace switch commit
< 100 ms) need timings from inside the browser. The patch adds three scoped
Perfetto trace events in Chromium's existing `browser` category (registered in
M153 `base/trace_event/builtin_categories.h`), so no upstream category patch
is needed and nothing runs unless tracing is on:

| Event | Where | Metric in `tools/perf` |
| --- | --- | --- |
| `Ahoi.CommandBar.RebuildSuggestions` | `CommandBarView::RebuildSuggestions` | `command_bar_ms` |
| `Ahoi.CommandBar.HistoryItems` | `CommandBarController::OnHistoryQueryCompleted` | diagnostic only |
| `Ahoi.Workspace.Switch` | `SessionBridge::SetActiveWorkspaceForWindow` | `workspace_switch_ms` |

`RebuildSuggestions` covers ranking and row construction, not the following
paint; `Workspace.Switch` covers the commit, not the first presented frame.
The methodology (`docs/PERFORMANCE_METHODOLOGY.md`) records that limit.

## Apply

```sh
git apply handoffs/crest-hardening/002-perf-trace-events/trace-events.patch
```

Include in the next planned desktop package; no extra build.

## Expected tests

- Compiles with the existing `//base` dependency of both targets.
- After the package build: `tools/perf/run_desktop_perf.py --scenario trace
  --trace-metric Ahoi.CommandBar.RebuildSuggestions=command_bar_ms
  --trace-metric Ahoi.Workspace.Switch=workspace_switch_ms --driver <journey>`
  reports samples (measurement itself needs the `host-quiet` and
  `installed-app` leases).

## Risks

None functional; scoped trace events are no-ops while tracing is off.
