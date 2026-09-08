# Build18 visible search-setting journey

Candidate894c9a2ac3f21711a3f9ed3eba702d16413bdf9c, DebugLocal0.1(18), iPhone17Pro /
iOS26.5. Product build48865 completed EXIT0 before this run. The exact source,
generated project, plist, signature and app tree were verified against
../../build/mobile-browser-settings-29c42db-20260908/candidate.json before
installation AND against the actually installed simulator bundle afterward.
App tree: bdeb580e666a4462a5cd541e45d99ab7c41402510f264b1d6eab93243c70a111.

Fresh own device: FE1B566C-F706-47E0-9D21-D96139C03B18,
`Ahoi Search Build18 20260908`. Initial boot93077 and install69291 ended EXIT0.
The NEW12:01UTC coordinated window had explicit Desktop, FillIt, BetterConvo
and MindBodyCompass handoffs. It was not a reopening of07:08/Build17. Start
was reported in01a080e7-9531-7b82-bde6-6dc974fa8520.

Before boot,12 cores had46–51% aggregate idle/46% memory headroom and stable
swapout counters. The two-job Chromium build was identified by its out/AhoiDev
CWD; the separate Swift compiler belonged to ConversationCopilot/FluidAudio.
No foreign process, simulator, profile or UI was modified by this task.

## Bounded visible result: PASS

1. Launch the installed app via its actual home-screen icon. More -> native
   actions sheet -> expand -> Settings. The picker initially displayed
   DuckDuckGo. Global Sync was off and browser-setting Sync was disabled with
   an explanation because CloudKit was not configured. No Sync switch was enabled.
2. Open the native picker and choose Bing. It visibly changed to Bing and the
   real default-reset button appeared.
3. Open the address sheet. CUA's initial typeText left a partial input; the
   exposed settable text field was then set to the full synthetic query
   `ahoi browser build18`, and AX confirmed the full text plus Bing. Click the
   real Search result. The actual Bing search page rendered, and the address
   value was https://www.bing.com/search?q=ahoi%20browser%20build18.
   Screenshot: bing-search.png.
4. Use Simulator Home, observe the actual home screen, then shut down/boot ONLY
   this simulator (restart58092 EXIT0). Launch the same app via native Spotlight.
   Its restored Bing result was visible, with one tab. Reopen Settings: Bing
   remained selected, with the reset button. Screenshot: bing-after-restart.png.
5. Click `Standard dieses Geräts verwenden`. The picker changed to DuckDuckGo
   and the reset button disappeared. Screenshot: default-reset.png. After
   closing Settings, the current webpage remained unchanged. A fresh synthetic
   address-sheet query displayed DuckDuckGo as its search provider; it was then
   cancelled without another navigation.

The post-restart navigation interval has the explicit limit below. This PASS
covers the setting's actual selection/search/restart/default-reset behavior,
not an uninterrupted complete browser/navigation journey.

## Focused readback AFTER the visible journey

settings-readback.json retains ONLY the relevant preference record from the
isolated app's real persisted domain, not a whole profile or unrelated history.

- Exactly one setting record, ID1B415D30-483C-42FD-852C-886B839840C7, current3.
- The complete record, value `"bing"` and all clocks are identical before/after
  restart. Passive startup did not author a new value.
- Reset retains the same ID and setting_id/tombstone field clocks, changes the
  value to JSON null, and advances the value_json clock. The native AppStorage
  user key `AhoiMobile.Browser.SearchEngine` is absent after reset.
- Structured JSON comparisons and the targeted plist-key query succeeded.
  These are real candidate-store checks, not fixture transport or XCTest results.
  No test binary or suite was built/run for this journey.

## Original tool failures and attribution limit

CUA's Settings-open call took113s. The picker-open call then returned
`Sky Computer Use request timed out` after120s; a screenshot request timed out
and reset the kernel. One exact-app rebind subsequently showed the expected
Google/Bing menu. No blind repeat click was sent. Remaining actions used fresh
AX indices and the exact own window title. This is not latency acceptance.

After the restored Bing page was already visible, a More-button request
returned `The user changed ... Simulator.app. Re-query the latest state`.
The fresh own-window state then showed https://www.vossmedien.de/startseite.html
instead. The source of that navigation is NOT established: it may be external
input or a targeting effect. It is not excused as harmless and is not assigned
to product code without evidence. The navigation was left untouched; no reset,
history clear or model manipulation was used to conceal it. The subsequent
picker still showed Bing, and resetting it did not navigate the current page.
No uninterrupted focus/navigation or interaction-timing pass is claimed.

All three screenshots were saved through the Simulator Save Screen UI while
the named own window was observed, then moved from their exact temporary Desktop
filenames into this directory. The final files were visually reread. SHA256:

- bing-search.png:3562559bb0e32ef7b0617acd4748c7c0cf0d771a811ad99cb8a352ff8da353ba
- bing-after-restart.png:257aaad198d67eff3628c7012540ef9e8605be1e7b07f8f848d585cc053f302c
- default-reset.png:fa83880d46690f5d8b9da6823fdcb6a76911dc08d126a90fa4b573b38dc2f0ca

## Cleanup and handback

After the final normal background transition, ONLY the own device was shut down
and its Shutdown state independently read back. No device or profile was erased;
the isolated store remains available for diagnosis. No more CUA action followed.
Explicit UI handback to coordinator:01a080fb-0b7e-7161-9c33-c8fb0cc18ce7;
Desktop:01a080fb-0be1-75e3-8087-71a5e428bd38. The coordinator can release the other
Simulator projects; the task does not retain their UI ownership.

This is NOT Mac/iOS CloudKit convergence, C++ settings application, extension
installation/storage restoration, new-icon acceptance, the later Home-selector
correction or host-width polish. Those source changes are not in Build18.
