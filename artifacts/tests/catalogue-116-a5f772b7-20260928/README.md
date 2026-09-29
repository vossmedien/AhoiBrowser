# Crest 116: native compile of the generated field catalogues

Bound to `a5f772b7`; assets unchanged since 27 September (SHA-256:
`sync_field_catalogue.h` 33617774…, `SyncFieldCatalogue.swift` 1acdeb0c…,
`field_catalogue.json` 63d0982b…; contract ddebfcfc…).

- **C++**: pinned Chromium Clang 24.0.0git (`20e97c4b`), `-std=c++20
  -Wall -Wextra -Werror -fsyntax-only` with the macOS SDK. [check_catalogue.cc](check_catalogue.cc)
  holds 147 `static_assert`s generated from `field_catalogue.json`
  (entity count, id, data class, wire version, field count, each field
  name). **Exit 0.**
- **Swift**: `swiftc -swift-version 6 -O`, the catalogue plus
  [check_catalogue.swift](check_catalogue.swift), which compares all 15
  entries with the JSON at runtime. **Build exit 0; run prints "swift
  catalogue matches 15 entries", exit 0.**
- **Mutation**: renaming each `tombstone` expectation fails 15 C++
  assertions; changing one JSON field makes the Swift check trap (exit 133).

This proves that both tables compile under the pinned toolchains and agree
with the JSON; it does not wire them into a product target or prove merge
behavior (the handoff's H1 scope note stands). Ran while build 45 journeys
waited for idle; build.lock was free, no browser/simulator was started.
