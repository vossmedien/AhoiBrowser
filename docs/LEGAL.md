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
- Sparkle 2 or replacement updater license and attribution

Feature gates in `config/feature-gates.json` remain off until their recorded
technical and legal requirements pass. A successful local experiment is not
permission to publish it.
