# WS-ISO-05 cross-level move on installed e86c929f — 5 October 2026

Installed `/Applications/AhoiBrowser.app` on MacbookPro2026.local, source
`e86c929f6bfbb0cbe1dac2fa4c101a31250c215a` (Chromium 154.0.8037.93), product
default launch, disposable user data directory, unlocked console, own
`.work/agent-queue/e2e.lock`, no foreign GUI test active.

| Run | Driver | Result |
| --- | --- | --- |
| `first-run-cmd-y/` | frozen `0e38e26e` (sha256 `bb4a3f59…`) | 22/25, exit 1 |
| `rerun-5ac391c7/` | `5ac391c7` (journey sha256 `f8dbf2ee…`) | **25/25 PASS, exit 0** |

First-run failure (retained): `undoRestoresSource`, `undoBringsLoginBack`,
`undoLeavesTargetEmpty`. Cause is the harness, not the product: the target has
only ever had the German layout, so HIToolbox stores no
`AppleCurrentKeyboardLayoutInputSourceID`; the driver logged
"undo: key 6 on unknown layout" and sent Cmd+Y. `5ac391c7` reads the layout
from the selected/enabled input sources (`tools/desktop_e2e/keyboard_layout.sh`);
the rerun logged "undo: key 16 on com.apple.keylayout.German" and all three
undo assertions passed. No assertion changed. The first rerun attempt refused
to drive (idle 27 s after the own run's synthetic input) and waited for
310 s of HID idle before starting.

Covered: confirmation and its sign-in notice, Cancel changes nothing, tab
moves into the separated Profile and reopens by URL without the login, undo
restores the source with its login, split moves whole, folder move, command
bar move with sign-in hint, moved folder persists across relaunch.
Not covered: drag-and-drop between two Profiles' windows (HID drag, manual CU).
