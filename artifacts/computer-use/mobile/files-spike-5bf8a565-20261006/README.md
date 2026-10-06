# Files spike: launch verified, interaction unavailable

Reused exact DebugLocal5bf8a565 on Inhouse deviceA058D204 (iOS27).
`readback.json` binds the installed app tree/executable/plist to the existing
candidate receipt and binds `mobile-current.png` by SHA256. `launch.log` and
the screenshot prove the native browser launched/rendered with the spike flag.

Files import was not exercised. Xcode27 uses Device Hub, verified against
[Apple's documentation](https://developer.apple.com/documentation/xcode/device-hub);
the first Simulator.app path was obsolete. Computer Use could not establish a
usable remote window (`-10005: noWindowsAvailable`). Target GUI accessibility
was unavailable; the original `hub-select/gui.log` says `not AX trusted` and
`could not create image from display`. A successful AX call without the expected
visible device change was not counted as evidence.

Own app terminated and own device Shutdown verified; own E2E lock absent.
Foreign Device Hub/devices and saved migration data remain untouched. No Files
fixture was staged, no import was performed and no permissions were changed.
ADR0012 Step1's Files/action/permission requirements and Step2 decision stay open.
Canonical `boot.log` omits only trailing blank lines; the raw target log is retained.
