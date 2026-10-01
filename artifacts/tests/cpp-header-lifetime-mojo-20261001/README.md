# Temporary header Mojo regressions, 1 October 2026

Four added native methods use the actual factory/request proxy and Mojo pipes,
with synthetic terminal loader/interceptor and a gated fake secret store:

- Two requests materialize fresh temporary secrets in both directions, while
  between-request metadata contains references and the tab owner token only.
- Reset during a blocked secret read retires both overrides on the late reply.
- Replacing the helper and saving identical rules cannot revive the earlier
  owner's blocked request.
- A retired helper prevents an existing factory from resolving further secrets.

The existing fixture now saves through its actual tab helper and commits its
merged metadata, making the tests cover real lifetime storage rather than a
shortcut into Preferences. Ordinary persistent fixtures keep their default mode.
No native assertion, sandbox/permission or URL-factory interception is skipped.

Pinned-Clang source syntax passes with exact source/command/log binding. Four
methods are written/syntax-checked, **not executed**; the previous 20/20 proof
belongs to the older unchanged binary, not these source additions. All source
stays below 800 lines. No real secret store/Keychain value is accessed or logged.

Next: one guarded integrated development candidate for backend/editor/Mojo
changes, actual new regressions and required native/visible gates. Installed
9acb43c8/M154 and its open visible acceptance remain unchanged. No build,
installation, simulator, external API or release action occurs in this step.
