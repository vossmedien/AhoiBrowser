# H3 Workspace-switch trace run ws16 — build 45 (validation)

28 Sep 2026 ~19:34 CEST. Setup (second Workspace via inserted name) and the
driver's switches completed and the guard completed, but the trace holds no
`Ahoi.Workspace.Switch` event (only PipelineReporter frames), so no samples.
The sidebar menu path reaches `SessionBridge::SetActiveWorkspaceForWindow`
(`browser_sidebar_host_command_dispatch.cc:559`), which carries the event, so
the cause is open; the next run dumps all `Ahoi.*` events.
