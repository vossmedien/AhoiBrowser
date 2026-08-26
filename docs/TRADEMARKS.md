# AhoiBrowser trademark and rebranding policy

This document defines the project's intended naming and branding rules. It is
not a statement that any mark is registered, and it is not a substitute for
the independent legal approval required by
`config/external-gates.json#trademark-and-distribution-policy`.

## Project marks

The names **AhoiBrowser** and **Ahoi**, the official AhoiBrowser logo and icon,
and the visual artwork shipped as official product branding identify builds
published by the AhoiBrowser project. Copyright licenses for the source code do
not grant a right to present a modified build as an official AhoiBrowser build.

The words **Chromium**, **Chrome**, **Google**, and their logos are not
AhoiBrowser marks. AhoiBrowser may accurately describe its Chromium ancestry,
but must not use Chrome branding, Google trademarks as product identity, or
language that implies endorsement by Google or the Chromium project.

## Allowed references

Without implying endorsement, documentation, reviews, bug reports and
compatibility statements may use the AhoiBrowser name to identify the project
or the unmodified software being discussed. Attribution such as “based on
Chromium” must remain factual and visually subordinate to AhoiBrowser's own
identity.

Unmodified source mirrors may retain names needed for attribution and history.
An exact, unmodified official binary may retain its embedded branding only when
redistribution is allowed by all applicable licenses and distribution terms;
it must not be presented as coming from a different official channel.

## Forks and modified distributions

Every public fork or modified binary distribution must rebrand before
publication. At minimum it must replace:

- the product name, application icon and user-facing logos;
- the macOS bundle identifier and any iOS/iPadOS companion identifiers;
- update-feed URLs, signing identities, release keys and official-channel
  language;
- support, privacy, security and crash-reporting contact points;
- any text or metadata that could imply the build is an official AhoiBrowser
  release.

Forks may state factually that they are derived from AhoiBrowser and must retain
copyright, license, source-offer and third-party notice obligations. Rebranding
does not waive GPL or third-party license duties.

## Official release boundary

Only artifacts that pass the repository's complete release chain may be
described as official AhoiBrowser releases. Development-signed, locally built,
unnotarized, modified or incomplete candidates must not use “official release”
language. Nightly, beta and stable channel claims also require their configured
signed update feeds and release evidence.

Requests for an exception or for permission to use official project artwork
remain a product-owner and legal-review decision. No source file, local build,
test result or automated receipt grants that permission.
