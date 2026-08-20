# Chromium patch ledger

`series` is the authoritative application order. Every patch must have a ledger
entry below before it is accepted.

Required fields: patch filename, owner, upstream Chromium version/commit,
affected paths, rationale, rejected alternatives, tests, security/privacy
impact, expected rebase risk, and removal/upstream plan.

## `0001-ahoi-vertical-tabs-default.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`.
- **Affected paths:** `chrome/browser/ui/tabs/features.cc` and
  `chrome/browser/ui/tabs/tab_strip_prefs.cc`.
- **Rationale:** use Chromium M151's native Views vertical-tab implementation
  by default and make fresh Ahoi profiles start in vertical mode.
- **Rejected alternatives:** a parallel sidebar/tab implementation, bypassing
  `VerticalTabStripStateController`, or removing the horizontal mode.
- **Tests:** `tests/repository/test_vertical_tabs_patch.py` constrains the patch
  to the two intended default changes; the patch must also pass `git apply
  --check --whitespace=error-all` against the exact pinned checkout.
- **Security/privacy impact:** none. The patch changes browser chrome defaults
  only; profiles, storage, renderer isolation, networking, and permissions stay
  owned by Chromium.
- **Expected rebase risk:** low but source-sensitive; the two upstream default
  declarations may move or be removed on a Chromium roll.
- **Removal/upstream plan:** re-evaluate on every roll. Remove the feature
  default override when Chromium's launch feature is enabled by default; retain
  an Ahoi-owned fresh-profile default only while vertical mode remains the
  product default.

## `0002-macos-26-posix-spawn-chdir.patch`

- **Owner:** AhoiBrowser project.
- **Upstream baseline:** Chromium `151.0.7922.170` at
  `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`.
- **Affected path:** `base/process/launch_mac.cc`.
- **Rationale:** AhoiBrowser v1 targets macOS 26. Chromium M151 retains a
  runtime fallback to `posix_spawn_file_actions_addchdir_np`, but the macOS
  26.5 SDK marks that symbol deprecated and Chromium promotes the warning to an
  error even though a macOS-26 deployment can never execute the fallback. A
  compile-time deployment-target guard uses the standardized replacement while
  retaining Chromium's runtime fallback for builds that still target older
  macOS versions.
- **Rejected alternatives:** globally suppressing deprecation diagnostics,
  weakening `-Werror`, lowering the deployment target, or deleting support for
  older deployment targets from the shared upstream source.
- **Tests:** `tests/repository/test_macos_26_compat_patch.py` constrains the
  patch to the one method and requires the legacy fallback to remain in the
  lower-deployment-target branch. The patch must pass `git apply --check
  --whitespace=error-all` against the exact pin, and the failed
  `obj/base/base/launch_mac.o` edge is rebuilt before the full `chrome` target
  resumes.
- **Security/privacy impact:** none expected. Process launch semantics are
  unchanged on every supported runtime; only compile-time symbol selection is
  made explicit for the macOS-26-only product build.
- **Expected rebase risk:** low. Remove or refresh the patch if Chromium
  upstream adopts an equivalent deployment-target guard or raises its minimum
  macOS version.
- **Removal/upstream plan:** check the upstream implementation on every roll
  and drop this compatibility patch as soon as the pinned Chromium revision
  builds cleanly with the required macOS 26 SDK without it.
