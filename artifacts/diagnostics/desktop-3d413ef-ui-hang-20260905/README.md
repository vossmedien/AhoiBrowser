# Desktop UI timeout / high-CPU diagnostic

Date: 2026-09-05, approximately 13:41–13:50 CEST. This is a failed baseline
reproduction, not acceptance of the unbuilt correction package.

- Installed plist rechecked: `3d413efb5b6f196403e92f51631c346c9c55b2e5`,
  Chromium `152.0.7977.65`, dev profile. No new build/install was performed.
- Fresh Computer Use inventory succeeded. The installed application was opened
  normally; the startup choice **Fortsetzen** retained the previous session.
- Initial AX state exposed the Ahoi sidebar, workspaces, tree and one open NTP.
  Floating-sidebar checkbox was off; native resize value was `302`. Raising
  the window yielded a real screenshot of the sidebar/NTP. The toolbar was
  hidden on the new-tab page, so this did not reproduce the reported seam.
- A batch of Cmd+L, paste `chrome://settings/`, Return and UI readback timed
  out after 120 seconds. Which of those steps completed is not established.
  A subsequent read-only AX request timed out after 10 seconds and reset this
  thread's JS kernel. No further UI action or speculative click was sent.
- Native process `37773`, launched at 13:41:27, remained running at roughly
  95–105% CPU across later readbacks. No Chromium compiler or Mobile test host
  was observed. Do not conflate this with a proven infinite loop or solely a
  broken Computer Use service.
- A single read-only `/usr/bin/sample 37773 3 1` run ended exit 0. Its report
  has only five main-thread samples: compositor BeginMainFrame/UpdateLayers/
  PropertyTreeBuilder/AddEffectNode work. That small sample cannot establish
  the source of sustained invalidation, a deadlock or the screenshot defect.
  It must not be used to justify an unrelated compositor rewrite.
- Sample SHA-256:
  `dd7e5f84e7e63dc33cd13c338e506c6cbf52ad2785c9d3e85b0152719d02b3e5`.
- Read-only journal metadata still reports schema 5,
  `prepared/manual_recovery_required`. No import, recovery, backup deletion or
  journal edit was performed. The app was not killed or force-quit.

The installed app is **running**, contrary to the earlier checkpoint's last
quit state. Recheck before any install; normal UI shutdown is still required.
Source `ef0f965` plus `96b5a2f` remains frozen/unbuilt, awaiting the common
Bookmark source handoff. No visible folder-motion, seam or recovery pass.

## Source finding and bounded correction (not yet executed)

`BrowserViewTabbedLayoutImpl::CalculateProposedLayout` supplies toolbar height
on every layout. The Ahoi branch in
`VerticalTabStripRegionView::SetToolbarHeightForLayout` always set the same
margin property and invalidated layout upward. Unlike Chromium's own
`VerticalTabStripTopContainer`, it lacked both a value comparison and
`avoid_propagate_during_layout`. Patch `0033` adds those guards without changing
the docked/floating/edge-revealed margin calculation. Three native browser
regressions directly observe invalidation notifications, real changed-margin
layout and presentation transitions; no idle-loop settling hides the defect.

The newly prepared material reapplication also needed value guards:
`ui::Layer::SetIsFastRoundedCorner` always schedules a draw, and zero-duration
`LayerAnimator::SetOpacity` calls a delegate that always schedules one. Radius,
fast-corner mode and opacity now use target/value comparisons. A real-compositor
unit regression checks that identical reapplication does not request another
commit, while an actual radius change still does. Native background setters
already have their own comparisons and are retained.

These are source-verified redundant invalidations, not a measured attribution
of all 95–105% CPU or a resolved Computer Use/renderer hang. They join the same
pending combined build; source is not installed or accepted yet.
