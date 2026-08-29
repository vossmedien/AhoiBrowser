# Sidebar search in-place acceptance

Date: 2026-08-29

Visible candidate: `/Applications/AhoiBrowser.app`

Visible candidate source: `2b0de426844a2b34c5134f44523e548f584181d6`

Chromium: `152.0.7977.65` (`fc4d67f1788019a27e32511137ceccbd2fafdaaa`)

Architecture: `arm64`

The visible user journey was executed against the installed application before
the focused programmatic regression gate. The application was targeted by its
full path because rollback bundles share the same bundle identifier.

## Visible Computer Use journey

| Step | User-visible assertion | Evidence |
| --- | --- | --- |
| Open sidebar search | Search icon, placeholder, text caret, and close button sit inside one 40-DIP rounded shell with clear internal spacing. The existing hierarchy remains visible before input. | [01-search-open-padding.png](01-search-open-padding.png) |
| Search for `MDN` | Matching saved/open rows remain in the primary sidebar position; matching recently closed items remain in the supplemental section below. | [02-mdn-inline-filter.png](02-mdn-inline-filter.png) |
| Search for `Neue Gruppe` | The matching folder and its child retain their hierarchy. The folder is a navigation result and does not expose a misleading disclosure control while filtering. | [03-folder-hierarchy-result.png](03-folder-hierarchy-result.png) |
| Keyboard selection | Arrow-key selection highlights the matching primary row and remains stable after the asynchronous result refresh. | [04-keyboard-selection-persists.png](04-keyboard-selection-persists.png) |
| Activate folder result | Return closes search and restores the normal hierarchy with the activated folder selected. | [05-folder-activated-restored-tree.png](05-folder-activated-restored-tree.png) |
| No match | A local `Keine Ergebnisse gefunden` state appears immediately below the input instead of replacing the sidebar with the old detached result list. | [07-no-results-inline.png](07-no-results-inline.png) |
| First Escape | The query clears, the complete hierarchy returns, and search remains open. | [08-first-escape-clears-restores.png](08-first-escape-clears-restores.png) |
| Second Escape | Search closes and the previously selected folder remains selected in the restored tree. No crash occurred. | [09-second-escape-closes-restores-selection.png](09-second-escape-closes-restores-selection.png) |

The missing step number 06 is intentional. Coordinate-based native dragging was
not available for this full-path app target in the Computer Use driver, so this
wave does not claim a new visible drag assertion. Search-mode drag and mutation
suppression are covered by the source contract and the focused regression gate;
earlier visible drag evidence remains a separate acceptance record.

## Programmatic regression gate

Executed only after the visible journey:

```text
./ahoi_sidebar_search_unittests \
  --gtest_filter='SidebarDiscoveryViewTest.*:SidebarTreeControllerTest.*:SidebarTreeViewTest.*:SidebarTreeViewRangeTest.*' \
  --test-launcher-bot-mode
```

Result: `SUCCESS: all tests passed.` — 58/58 tests, 0 failures, 0 crashes,
about four seconds. The focused runner uses Chromium's `ViewsTestSuite` and a
dedicated repacked resource file containing the required Chrome and Components
strings; it does not depend on the broad `chrome/test` unit-test graph.

The gate covers input/Escape/keyboard delegation, stable selection, external
store invalidation, in-place hierarchy projection, cross-folder split context,
saved/runtime drag validation, drop-zone hysteresis, split-pane targeting,
virtualization, accessibility, and rename/activation behavior.

## Review conclusion

Independent compile-contract and UX/state reviews reported no remaining static
P0 or P1 finding in the integrated sidebar search change. Temporary Chromium
PDF test workarounds used while diagnosing the broad test graph were completely
removed before this gate.
