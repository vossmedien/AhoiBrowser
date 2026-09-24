# Mobile Reader article selection — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or skips**
on exact Xcode 27.0 DebugLocal build50. A loaded synthetic WebKit document
contained an eligible `<article>` nested in `<main>`, plus unrelated main-page
text. The browser opened its native Reader, displayed the article text without
the unrelated fixture heading/find marker, and returned to the original page.
The screenshot in `screenshots/` shows the bounded Reader result.

The first exact Build49 journey on source `0443cc4` passed its weaker checks,
but its screenshot showed that the Reader chose the enclosing `<main>` and
included unrelated page text. Source `3abe4c4375da51f6f113c2b443ee280fffebf40d`
prioritizes a qualifying `<article>`/`[role=article]` over a qualifying
`<main>` and strengthens the same UI test to reject those extra texts. The
Build50 `build-for-testing` succeeded; `test-without-building` ran only
`testReaderExtractsVisibleArticleAndReturnsToSamePage` and passed 1/1. The
accepted screenshot was visually reviewed. The original Build49 log, result,
diagnostic app and screenshot remain under `/private/tmp/ahoi-mobile-reader.lXzxu7/`;
they are not acceptance evidence.

Build50 is clean detached source `3abe4c4`, `DebugLocal` 0.1 (50), simulator
arm64, ad-hoc signed. The built, archived and post-test installed bundles
were byte-for-byte equal (`diff -qr` exit0); built and installed Info.plist
both report source `3abe4c4` and build50. Deep signature verification passed.
Hashes are in [candidate.json](../../build/mobile-reader-3abe4c4-20260924/candidate.json).
The built app, build/test logs and result bundle are retained locally; the
safe screenshot and attachment manifest are tracked here. C645 and its
CloudKit Development candidate were untouched.

Limits: the page is a debug-only synthetic WebKit fixture, not a public
website. This test does not establish form-value or scroll-position retention,
Markdown clipboard output, unsuitable-page feedback, real-device behavior,
Desktop Reader behavior or full DoD27 acceptance. It is not a Sync result.
