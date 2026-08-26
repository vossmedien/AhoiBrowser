# Third-party and release-brand review

Every public release candidate requires an independent product/legal review of
the exact shipped component set and branding. The automation can prove what is
in a candidate; it cannot grant distribution or trademark rights.

The machine-readable contract is `config/release-review-policy.json`. It ships
with `releasePassEnabled=false` and no trusted reviewer key IDs. Those values
must stay closed until the named external gate has actually been completed.

## Candidate evidence

Reviewers receive one immutable release directory containing:

- the exact component inventory and SPDX-2.3 SBOM;
- Third-Party Notices, the component license archive and corresponding-source
  offer generated from that inventory;
- the pinned Chromium and Sparkle revisions and archive hashes;
- ZIP, DMG, separate debug-symbol archive and canonical `SHA256SUMS`;
- the AhoiBrowser source commit, build number and signed evidence manifest;
- known deviations and the current external-gate report.

The inventory must describe every shipped component. A missing component,
`NOASSERTION`, unresolved supplier/source location, empty license evidence or a
receipt from another candidate is a blocking failure, not a review exception.

## Required review decisions

`third-party-license-and-source` covers license compatibility, attribution,
notices, source availability, Sparkle and Chromium obligations, plus any
separately distributed extension, filter list, codec or CDM. Proprietary codec,
Widevine, uBlock Origin and Apple/App Store questions retain their own external
gates; omitting a gated component from a build does not mark its review passed.

`trademark-and-branding` covers the AhoiBrowser name/logo policy, removal of
Chrome/Google product branding, Chromium attribution, public-fork rebranding
and every intended distribution channel. The normative project rules are in
`docs/TRADEMARKS.md`.

## Fail-closed handoff

Approval must identify the exact candidate and preserve reviewer identity,
review time and stable evidence references outside generated source artifacts.
Engineering may then pin the independently controlled reviewer trust material
through a reviewed change. It must not invent an approval, copy a prior
candidate's decision, enable `releasePassEnabled`, or add a trusted reviewer key
merely because repository tests pass.

Until that handoff exists, the canonical state remains
`blocked-legal-review`, public distribution is prohibited, and Definition of
Done items 21 and 22 remain open even when local artifact generation succeeds.
