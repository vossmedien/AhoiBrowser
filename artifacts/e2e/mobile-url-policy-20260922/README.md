# Mobile URL navigation, 22 September 2026

## Observed failure

On the installed `CloudKitDevelopment` build 38 (`f21d089`, iOS 27, owned C645),
typing `https://winfuture.de/` through Ahoi's address field loaded the page but
raised the native alert “AhoiBrowser öffnet nur sichere Weblinks und freigegebene
App-Links.” The alert recurred when revisiting the page. The initial visible
failure is retained in `winfuture-baseline-build38.png` (SHA-256
`475e70d81f97b17dc76b3eebcbdeb1f703346d6546c0cd65c4d9c2ff7eb61696`).
WinFuture's mobile HTML contains an `about:blank` iframe. That and the policy's
previous unconditional blocked-navigation callback suggest an embedded document
trigger, but the exact blocked action URL was not captured from WebKit.

## Corrected candidate and visible result

- Product source `45330d2de8ff75e0d50bbbc88a4a3de9aaea8aa5`, `DebugLocal`
  build 39, Xcode 27 / iOS 27. Product-only `xcodebuild build` exited 0.
- Exact candidate receipt: `artifacts/build/mobile-url-policy-45330d2-20260922/candidate.json`
  (SHA-256 `11d7181fbb875bb915f513777cd4e4277179bc3a855cb0e309e44097c3d03af1`).
  Installed app tree, executable and Info.plist each matched that receipt on
  dedicated simulator `A168C9AA-1018-4C41-9D20-10ED6206D4B2`.
- A normal address-field navigation to the same WinFuture URL displayed its
  content and site cookie sheet without the native Ahoi URL alert. The page
  finished loading; a further visible check after five seconds still showed no
  Ahoi alert. `winfuture-fixed-build39.png` SHA-256:
  `9b860702d3237d73ec674f4aa7a1e0dfda2552055828b60c64f944e1fc6675d6`.
- Creating a normal tab with no assigned workspace showed the bottom label
  “Tabs” instead of “Ohne Workspace”. `unassigned-tabs-build39.png` SHA-256:
  `d498be0f4a503d4df34867d0966c3d686a1b2f2e913bb7941d6edb55fd27e9ae`.
- The normal address field still rejected `javascript:alert(1)` with
  “Gib eine gültige Adresse oder einen Suchbegriff ein.” No script ran.

## Focused follow-up and limits

The first selected XCTest invocation exited 65 before executing the new test:
two pre-existing SharedTab provenance tests refer to removed Format-2 APIs.
The corrected invocation excluded only those two source files using the same
documented Xcode build setting as the earlier focused Mobile run. The new
`MobileExternalSchemePolicyTests.testWebKitDocumentNavigationDoesNotTurnIntoExternalURLPermission`
then executed once, passed with zero failures/skips and exited 0. Logs and
XCResults are under `artifacts/tests/mobile-url-policy-20260922/`; the first
failure remains retained. The corrected test log SHA-256 is
`cb10d7d7013a99528b26530c656749c5efc119d8377b986b82361850a439f971`.

The tested fix allows WebKit-owned internal document URLs in an existing page
while the address/external-open boundary still accepts only validated web URLs.
Only user-activated blocked main-frame links present the native alert. This is
the observed WinFuture journey and its focused security boundary, not a broad
web compatibility matrix or a CloudKit/Sync acceptance. DebugLocal is
provider-free. An attempted separate plain-HTTP loopback fixture did not reach
its server and ended at `about:blank`; it was not used as proof of this fix.
