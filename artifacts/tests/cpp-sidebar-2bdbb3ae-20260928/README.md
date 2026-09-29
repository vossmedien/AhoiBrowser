# ahoi_sidebar_tree_unittests on 2bdbb3ae — sidebar drift closed

Focused owner run (`.work/agent-queue/sidebar-run.sh`, build.lock held,
`AHOI_JOBS=4`) in the build-43 journey window. **168/168 pass, EXIT 0**
([tests](tests.txt), [progress](progress.txt)).

Against the first native run on `682192c8` (11 failures, 2 timeouts,
1 crash, [evidence](../cpp-sidebar-682192c8-20260928/README.md)) this
covers the test-maintenance commits:

- `f1831778`: split pane minimum 30 px (d84d8e3b) derived from the visual
  constants; density test checks split height via layout instead of
  feeding plain views into the composite row (CHECK); bookmark shelf
  mounted at its full preferred height (heading row + scrolling item row),
  which also cleared both context-action timeouts.
- `52bc5099`: M153 scales `gfx::LinearAnimation` durations by the global
  multiplier (motion tests now use `NORMAL_DURATION` with their manual
  clocks); macOS renders rich animation unless reduced motion is preferred
  (the tree fixture defaults to `--force-prefers-reduced-motion`; motion
  tests force it on).

No product code changed for this package.
