# Sparkle binary dependency

AhoiBrowser uses the official Sparkle 2.9.6 binary distribution without a
source rebuild or local patch.
The framework is fetched by the repository-level `scripts/fetch-sparkle.sh`
into `prebuilt/`; generated framework files are intentionally not committed.
The fetcher verifies the upstream archive first, then deterministically retains
only the arm64 slices required by the product and applies development ad-hoc
signatures. The release signer replaces every nested signature leaf-to-root.

- release: `2.9.6`
- source commit: `ac2def288cbff5cfc7df3ffef6abdf45b72bcb0a`
- archive: `Sparkle-2.9.6.tar.xz`
- archive SHA-256: `52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192`
- upstream: <https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6>
- license: MIT plus bundled third-party notices, reproduced in `LICENSE`
- license SHA-256: `389a4e4e9a32f059775b13a06e25a591445ba229d2838d26dd3e7c0c45127cfe`

2.9.6 is the minimum accepted version because it fixes the high-severity
GHSA-3x7w-j75x-ppq5 and GHSA-4v99-qgq9-6pxp advisories affecting 2.9.5 and
earlier. Do not locally patch or rebuild Sparkle as an implicit substitute for
updating this reviewed pin.
