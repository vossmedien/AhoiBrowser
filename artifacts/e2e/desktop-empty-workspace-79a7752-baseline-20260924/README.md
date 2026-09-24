# Desktop empty-Workspace typed navigation — tool baseline on 79a7752

Outcome: **expected RED reproduced** by the automated PID-scoped journey
`tools/desktop_e2e/empty-workspace-navigation.sh` on the signed pre-`0054`
clone `/private/tmp/ahoi-cookie-fix.afGwcv/AhoiBrowser.app` (source `79a7752`;
main binary SHA-256 in `candidate-main-binary.sha256`, identical to the
recorded `dd93cdb5…` candidate). Disposable profile, installed
`/Applications/AhoiBrowser.app` untouched.

Journey: two new Workspaces via the real Workspace context menu and dialog;
⌘T in "Leer-Test" opened `…/?empty-probe=1` as a new tab (correct). From the
second, tabless "Leer-Zwei", ⌘L opened the command bar pre-filled with the
hidden Leer-Test tab's URL; typing `…/?empty-probe=2` + Return navigated that
**hidden** tab (`hiddenTabsNavigated`) and created no tab in Leer-Zwei
(`verdict.json`, `tabs.txt` from Chromium DevTools `/json`). This matches the
earlier manual negative and is the boundary ordered patch `0054` must fix.

Purpose: proves the harness detects the defect, so a later PASS on the `0054`
candidate is meaningful. This is not acceptance of anything. No screenshots:
Screen Recording is an owner-gated permission; UI state is the AX tree.
