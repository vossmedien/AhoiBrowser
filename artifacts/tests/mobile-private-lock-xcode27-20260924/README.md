# Mobile private-session lock: focused Xcode 27 regression

- Date: 2026-09-24.
- Repository HEAD: `8662590`, with clean Mobile paths; Mobile product, project and configuration files are byte-identical to visibly tested `45330d2` / DebugLocal build 39 (`git diff --name-only 45330d2..8662590 -- apps/AhoiMobile/Sources apps/AhoiMobile/project.yml apps/AhoiMobile/AhoiMobile.xcodeproj` returned empty). Other user-owned worktree changes were left untouched.
- Toolchain: Xcode 27.0 (`27A266a`), iOS 27.0 Simulator A168 (`24A434`), `DebugLocal`.
- Command: `xcodebuild test -project apps/AhoiMobile/AhoiMobile.xcodeproj -scheme AhoiMobile -configuration DebugLocal -destination 'platform=iOS Simulator,id=A168C9AA-1018-4C41-9D20-10ED6206D4B2' -parallel-testing-enabled NO -maximum-concurrent-test-simulator-destinations 1 -only-testing:AhoiMobileCoreTests/MobilePrivateSessionLockTests` with isolated derived data and result bundle.
- Result: EXIT 0; 3 passed, 0 failed, 0 skipped. Cases cover inactive authentication, cross-scene/background invalidation and replacement-session grant rejection, plus failure protection.
- The test action did not replace the installed Build 39 app (`simctl appinfo` still reported build 39); A168 was returned to Shutdown. The CloudKit Development simulator C645 was not touched.
- This is a focused state/race regression, **not** visible private-lock/return/cancel/background acceptance, snapshot, VoiceOver, keyboard, multi-scene or real-device authentication evidence.

Evidence: `private-lock.xcresult/` and `xcodebuild.log` in this directory.
