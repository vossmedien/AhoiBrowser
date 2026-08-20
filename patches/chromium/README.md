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
