# Mac Sync readiness — installed candidate readback

Date: 2026-09-05. Installed Chromium candidate:
`3d413efb5b6f196403e92f51631c346c9c55b2e5`, Chromium `152.0.7977.65`.
Receipt: `artifacts/install/ahoi-dev-3d413ef-20260905T074543Z.json`.

| Boundary | Current evidence |
| --- | --- |
| Build / install | Guarded build and atomic install both exited 0; post-install verification true |
| Code signing | Apple Development: Christian Voss (2265UJB5KF); actual TeamIdentifier `248AJ5BN47` |
| Runtime CloudKit configuration | No `AHOI_CLOUDKIT_CONTAINER_ID` or `AHOI_SYNC_KEYCHAIN_ACCESS_GROUP` in installed Info.plist |
| CloudKit permissions / profile | No CloudKit entitlement readback; no `Contents/embedded.provisionprofile` |
| Provider construction | `FromMainBundle()` returns nullopt because the container is absent; backend does not construct the provider |
| Shared payload-key bootstrap | Not verified; missing group/config is a separate boundary, not the predicate that returns nullopt |
| Native Mac CloudKit candidate | No separately prepared and verified candidate in this Desktop wave |
| C++ sync unit executable | GN target exists, but `out/AhoiDev/ahoi_sync_unittests` is absent |

The development signature is not CloudKit transport or Production evidence.
Provider-free local persistence remains usable; this report did not enable Sync,
query a payload secret, change entitlements or mutate Apple/CloudKit state.

Existing implementation preparation is documented in `docs/SYNC.md` and
`config/macos-entitlements.json`: distinct `cloudkit-development` and
`cloudkit-production` profiles, public container/group identifiers and the
`prepare-macos-cloudkit` / `verify-macos-cloudkit` release-tool commands.
Their existence is not a prepared/signed runtime candidate. The old Apple-portal
snapshot in SYNC.md was not refreshed here and must not be treated as current
proof that an Ahoi container/profile does not exist.

Next coordinated package must include `ahoi_sync_unittests` and a verified Mac
CloudKit signing/configuration path with a concrete compatible Mac provisioning
profile, candidate-bound receipts, payload-key bootstrap and Mac–Mobile roundtrip.
No extra build was started for this coordination request. Existing iPhone-hosted
two-store/bridge Development or simulated tests do not prove Chromium execution,
cross-device Keychain bootstrap or Production transport. Use Mobile's checkpoint
for its exact run/device/server evidence; do not relabel historical Workspace
Library tests as Chromium bookmark or shared-normal-tab acceptance.

The isolated desktop UI slot remains with the bookmark owner. A Mobile test host
running on My Mac with the same bundle identifier requires an explicit runtime
handoff before launch; it must not collide with the installed Chromium candidate.
