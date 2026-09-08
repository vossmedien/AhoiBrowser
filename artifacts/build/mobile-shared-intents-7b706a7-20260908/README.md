# Mobile deferred-intent candidate

- Exact clean source: `7b706a73f98802fe31b206731b7e88c1da6b6c59`.
- DebugLocal 0.1 (17), generic arm64 iOS Simulator, Xcode 26.6/17F113.
- Product-only build Session `79364`: terminal **EXIT 0 / BUILD SUCCEEDED**.
  Full command/output in `build.log`, raw result in `build.xcresult`.
- Existing isolated source/DerivedData reused; build and Swift concurrency two.
  Confirming resource sample: 73.57% CPU idle, 45% pressure-reported available
  memory, no new swap-outs, no competing compiler identified.
- Embedded source/Build17 read back; deep/strict codesign verification succeeded.
  Existing candidate tool binds app, clean source and generated Xcode project in
  `candidate.json`. App-tree artifact hash:
  `d84c635869568ef1a90b809895d3571b148a75d814dcc41bd3842346a24dab87`.
- Built but not installed/launched or visibly accepted. No tests, Simulator/
  My-Mac host, Desktop app, profile, CloudKit, portal or key action.
- Prior Build16 `bba0b86` was APFS-cloned before cache reuse to
  `../mobile-unified3-4e64c5f-20260908/AhoiMobile-bba0b86.app` and independently
  verified against its existing exact candidate receipt. No old evidence was
  relabelled as Build17 success.

Changed behavior: pending normal-tab navigation/move/rename survives deferred
identity; domain receipt and mutation commit together to prevent crash replay;
receipt pruning follows successful browser-session persistence. Dormant metadata
edits do not instantiate a Presence/WebKit page. These behaviors still require
the representative visible journey and then minimal focused boundary checks.
