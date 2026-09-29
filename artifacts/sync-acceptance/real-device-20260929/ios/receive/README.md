# iPhone receive direction (Mac to iPhone), 29 September 2026

Same iPhone 16 Pro Max "Servusla" (UDID 00008140-0014294E0A90801C,
unlocked: `passcodeRequired: false`), same isolated scope
`23855a90-ee61-499e-abed-bfdc52a881d7`, same installed candidate as `../`:
the app was rebuilt with `AHOI_SOURCE_COMMIT=d065a744…` and its binary and
Info.plist hashes stayed `a96f9bf1…` / `5791dc72…` (`../candidate-d065a744.sha256`),
so only the UI-test runner changed. No reinstall of another build, no data
reset, no key/record/zone deletion, no Simulator, no Mac GUI.

Test: `MobileRealDeviceCloudKitSyncUITests/testRealDeviceReceivesRecognizableRemoteTab`
(source b2f2c11e), `TEST_RUNNER_AHOI_REAL_DEVICE_CLOUDKIT_SYNC=1`,
`TEST_RUNNER_AHOI_REAL_DEVICE_SYNC_EXPECTED_REMOTE_URL=https://example.com/?ahoi-sync-mac-20260929T112130Z`,
300 s receive window. Each pass: Settings "Jetzt synchronisieren" until
Bereit / Verschlüsselung bereit / Synchronisiert, Devices section, library
"Geräte-Tabs", then library search for `ahoi-sync-mac-20260929T112130Z`.
Pass condition: a `remoteTab` search result carrying the exact URL.

## Result: FAIL for the open tab, PASS for Mac history

Run 6 (13:29–13:36 CEST, 3 sync passes, all "Synchronisiert"):

- Devices (Settings): "iPhone · Dieses Gerät · Online" and
  **"Mac.fritz.box · Online"** (last active about 13:23:33).
- Geräte-Tabs (library): only **"Example Domain · iPhone · Inbox"**. No Mac
  tab in any pass.
- Search `ahoi-sync-mac-20260929T112130Z`: exactly one result,
  `browser.library.search-result.history.f17bab9f-…`, text
  **"https://example.com/?ahoi-sync-mac-20260929T112130Z"**. The result is a
  **history visit** from the Mac, not a device tab.

Run 5 (13:25) showed the same state and then failed only because its harness
could not dismiss the search. It is not kept.

A read-only `devicectl copy from` of the scoped store (`run6-device-store-readout.txt`)
matches the UI: the phone accepted Mac records `device`, 2 × `deviceSession`,
`deviceCapability`, `appearance`, `extensionInventory` and **one
`historyVisit` (the marker URL, transition `auto_toplevel`)**. It holds **no
`deviceTab` record from the Mac**. Its only `deviceTab` is the iPhone's own.

Conclusion: Mac to iPhone CloudKit transport and decryption work, since the
Mac's encrypted history visit for the marker URL arrived and decoded. The
Mac's open tab never arrived as a `deviceTab` record in this zone. The
13:21:32 upload `saved=1` was most likely that history visit. The open issue
is on the Mac publish side: the tab presence was either not uploaded or went
to another zone. The phone's receive path is not the cause.
