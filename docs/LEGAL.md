# Legal and distribution gates

AhoiBrowser-authored code is intended for GPL-3.0-or-later distribution.
Chromium and bundled third-party components retain their notices and licenses.
Release automation must produce a complete source/patch offer and third-party
notice set corresponding to each binary.

Legal review is required before public binary distribution for:

- GPLv3 section 7 and compatibility of every bundled updater/framework/library
- Apple Developer Program, Developer ID, notarization, CloudKit, and iOS/iPadOS
  App Store terms, including the GPL/App Store distribution relationship
- AhoiBrowser naming, iconography, Chromium/Chrome trademarks, and rebranding
- Chrome Web Store access, CRX/update service use, Google API keys, and policies
- Widevine licensing/certification and licensed streaming-service conditions
- H.264/AAC and other proprietary codec patent/distribution obligations
- distribution/update rights for a selectively supported uBlock Origin Classic
  package and its filter-list ecosystem
- Sparkle 2.9.6 MIT license and bundled third-party notices, pinned by exact
  release commit and archive SHA-256 in `config/third-party-pins.json`

External feature gates in `config/feature-gates.json` remain off until their
recorded technical and legal requirements pass. The narrowly scoped uBlock
Origin Classic path is the explicit exception: it is compiled into every
supported Ahoi desktop profile so the user-selected, statically browser-pinned
Official GitHub release bootstrap can work in the product. This technical
availability does not grant public redistribution rights or provision later
signed catalog updates. A successful local or installed runtime test is not
permission to publish it; `ubo-redistribution` remains fail-closed until its
independent legal review passes.

The explicit name/logo and public-fork rules live in `docs/TRADEMARKS.md`.
Candidate-specific third-party and brand review follows
`docs/THIRD_PARTY_REVIEW.md` and `config/release-review-policy.json`. That
policy intentionally contains no approval or trusted reviewer key today;
generated receipts and passing tests are not legal clearance.
