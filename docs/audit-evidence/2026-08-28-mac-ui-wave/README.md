# macOS UI recovery wave — visible audit contract

Date: 2026-08-28  
Candidate before this wave: `decd87c581df...`  
Scope: native macOS AhoiBrowser only

## Captured baseline

- `01-user-reported-navigation-seam.png` — user-provided installed-browser
  screenshot. The dark strip between docked sidebar and content is exactly
  8 px wide, matching Chromium's second split-content inset on top of Ahoi's
  existing 8 px content-card inset.
- `02-cmdt-active-tab-before.jpeg` — installed-browser `Cmd+T` state before
  this wave. The current tab and the command-bar selection are visually
  conflated and the active tab is not persistent when selection moves.

## Findings and implementation acceptance

1. **Content-card seam — high visibility**
   - Ahoi owns the outer card gutter outside fullscreen.
   - The inner Chromium split inset must be zero only for that Ahoi state.
   - Stock Chromium, fullscreen and side-panel policies remain unchanged.
   - In docked, floating and hidden/revealed sidebar modes, the first pane and
     floating navigation surface must align without a second dark strip.

2. **Stacked split resize — interaction blocker**
   - The native resize and persistence path exists, but its 10-DIP target is
     invisible until already hit.
   - A restrained handle must remain visible in both stacked and side-by-side
     layouts without widening the event-capturing area into page content.
   - Real pointer drag must update both WebViews live, snap near 50 percent and
     persist across tab/workspace switching and browser restart.

3. **Sidebar page tint — motion discontinuity**
   - Active-tab and active-pane changes currently replace the tint in one
     frame.
   - Transitions must retarget from the currently displayed color, finish in
     180 ms and fade through a transparent peer hue when one endpoint has no
     tint.
   - Initial paint, high contrast, reduced motion, reduce-transparency policy,
     theme-base changes and teardown must snap safely.

4. **Command bar current tab — weak state hierarchy**
   - The live active tab must keep a persistent semantic surface, accent title
     and fixed-size marker.
   - Hover, pressed state and keyboard selection must remain separately
     recognizable with no result-row layout shift.

5. **Application identity — visual quality**
   - Replace the glossy, detailed raster with the reviewed flat two-sail `A`
     and one restrained coral accent.
   - Validate the same identity in Dock/Finder, 16/20/32 px runtime resources,
     Settings and native helper/application assets.

## Required visible order

The newly installed signed candidate is exercised with Computer Use before any
focused unit/browser suite is run. A failed visible journey is fixed first,
then that journey is repeated; only after the affected journey is green may
its programmatic regression tests execute.
