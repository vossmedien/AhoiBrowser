# Sidebar presentation: visible journey, then five focused regressions

Date: 2026-09-06 local / 2026-09-05 22:08 UTC test phase.

## Visible installed-product result

Installed source `4cb622a0bffc602051bf72e6e95b6100948f861e`, verified again
from Info.plist. Build73875/install77504 and signed-candidate bindings remain
in `artifacts/build/desktop-startup-guard-20260905/README.md`.

The still-unapproved AnyChat permission sheet was cancelled without granting
rights; the Store returned to Hinzufügen. On that same public page, native
CUA Raised the browser and exercised:

1. Docked sidebar (floating toggle0, width264) -> floating (toggle1).
2. Header hide action -> sidebar absent, page remained visible.
3. Cmd+Shift+S -> sidebar returned in its previous floating mode (toggle1).
4. Floating toggle -> original docked mode (toggle0, width264).

The Store URL, active Store tab and existing Settings tab stayed the same.
Sidebar rows returned; the browser remained usable. Screenshots/AX states are
in the native CUA transcript. No folder/workspace/tree edits, import/recovery,
extension install, feature implementation or new build occurred. This is a
bounded mode/hide/restore PASS, not folder-motion, pixel-seam, full Sidebar,
extension or Sync acceptance.

## Reused test binary and source boundary

Existing `out/AhoiDev/ahoi_sidebar_tree_unittests` SHA256:
`d80e587a21b7220567d3dcf857d696e6a17433e38363c612fb1df1088ab35b88`.
It was last linked in the full c986090 build59738 (recorded `LINK ./...` in
`artifacts/build/desktop-startup-guard-20260905/build.log`), NOT rebuilt by the
product-only4cb build. The selected `sidebar_presentation_state.{h,cc}` and
`sidebar_presentation_state_unittest.cc` have an empty c986090..4cb622a diff.
The changed NavigationSurfaceController/fullscreen assertions are not selected
or claimed as tested here. New main-branch Native A/Common WIP is excluded.

The shared component runtime is the existing4cb output. Its changed
`libchrome_dll.dylib` and the installed copy hash identically:
`abbdb109dc3cc69cb3f646d5b2ce80fae26ec9da7ea4ca2f802ca8afef0822fa`.
No standalone new4cb test build or whole-binary4cb test-source claim is made.

After the visible journey and a fresh all-project CPU gate, list session39741
exited0 and showed exactly the five intended tests. A separate fresh CPU gate
at22:08:26UTC preceded run95926, terminal EXIT0, jobs1/retries0:

`SidebarPresentationStateTest.FloatingRoundTrips:SidebarPresentationStateTest.HiddenRestoresPreviousVisibleMode:SidebarPresentationLayoutTest.*`

`summary.json` contains exactly five executions, all SUCCESS: floating
roundtrip, hidden-mode restoration, docked viewport reservation, floating
overlay and hidden viewport release. The launcher reported about1 second.
The existing duplicate `ANGLESwapCGLLayer` warning is retained in both logs;
no new cause or broader safety conclusion is inferred from it.

SHA256:

- `list.log`: `934f493095785e9d28ce8d1d2df8cba5dfc761948d0cd4a9befc62cece9239eb`
- `run.log`: `5e09a8d7b53f1ef3463fd812c7dd28186abe86f7d1cad605cddbc851701431d8`
- `summary.json`: `dda25e865cc92bf820699f2cd7f94c516d16ecfb94ab0651d232e69ee14c626b`
