# Patch 0071: GRIT check of the rebranded string bundles (no build)

Checkout's own GRIT (`tools/grit`, M153 `153.0.8010.53`) parsed both
bundles with the macOS defines before (`a`, patches 0001–0070) and after
(`b`, plus 0071) and matched every translatable active message's GRIT id
against the German `.xtb`:

| Bundle | Translatable Mac messages | Without German translation (a → b) | Product "Chromium" (a → b) | "AhoiBrowser" (a → b) |
| --- | --- | --- | --- | --- |
| `chromium_strings.grd` (+ `settings_chromium_strings.grdp`) | 629 | 0 → 0 | 448 → 0 | 34 → 482 |
| `components_chromium_strings.grd` | 15 | 0 → 0 | 13 → 0 | 0 → 13 |

"Product Chromium" excludes attribution/license texts and "Chromium OS".
So no German translation is orphaned by the new fingerprints. The patch
applies with `git apply --check --whitespace=error-all` on the current stack
and the patch-stack tests pass (30/30). Not yet built or seen in the app:
build 46 waits for disk space (19 GiB free, floor 32 GiB).

Same session: `command_bar_view.cc` (977853f9, SchedulePaint after rebuild)
passes `-fsyntax-only` with the AhoiDev flags of
`obj/ahoi/browser/command_bar/command_bar_core/command_bar_view.o`.
