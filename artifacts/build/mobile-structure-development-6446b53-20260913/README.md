# Structure / Home / Private Lock — iPhone Development26

Exact clean source `6446b534b3269befaf36a07fb80bde6e0251e745` compiled as
CloudKitDevelopment0.1(26), generic iOS/arm64, then a separate copy was signed
using the existing Ahoi iOS Development profile and Apple Development identity.
This is a real iPhoneOS product, not a Simulator or My-Mac host. It is NOT
installed or launched and does not prove device UI or CloudKit behavior.

## Actual build and preserved candidate

Root's product-only build88339 finished EXIT0 (`build.exit`, `build.log`,
`build.xcresult`): four product targets, Xcode26.6/17F113, SDKiphoneos26.5,
two Xcode/Swift jobs, no test targets, signing disabled during compilation,
no provisioning-update option. The ordinary signing-mode preflight executed.
The fresh capacity samples were68–75% CPU idle,66% memory headroom, no swapout
movement and about65.7GiB free. The active Docker/Virtualization workload was
left untouched.

The existing clean source and DerivedData at
`/private/tmp/ahoi-mobile-debuglocal25.7q8d3O/` were reused. The `iphoneos` product
directory is separate from DebugLocal25's `iphonesimulator` directory. A byte-
verified APFS copy preserves `AhoiMobile-6446b53-unsigned.app`; the independent
`AhoiMobile-6446b53-development-signed.app` preserves the signed result. Both
are local artifacts; no existing candidate or profile was replaced.

The source includes the committed Structure metadata, Home/Reader/Markdown
actions and private-session lock. The same Source bundle is already preserved
at `../mobile-debuglocal-6446b53-20260913/source.bundle`; its prerequisite remains
fa53e31. `candidate.json` binds that bundle, the clean source, generated project,
actual app Info.plist, both app trees, executable and debug-library bytes.
Tree hashes use the repository `tree-v1` algorithm; `rawFileSha256` fields are
ordinary SHA-256 over the raw bytes, not the tool's `file\0`-prefixed hash.

## Actual signature and Development scope

Signing/strict-deep verification92531 finished EXIT0 (`sign.exit`). The existing
profilebbf658ff-10e4-4768-8cac-de5787ab5e78 remains unexpired until
2027-08-31 and contains the selected public signing certificate. The certificate
actually extracted from the signed app has the expected SHA-1
147982d504ebd2c629843517a8beccdeefc67e6d and SHA-256
32588cf8f454c31e32e62743f10731c9f394bfe8c84098bfbcf980a40281550c.
Nested code and the main app verify; the main entitlements exactly equal the
already accepted `Development22.entitlements.plist`, with Development CloudKit,
development APNs, exact Ahoi container and the two exact Sync/command groups.
The profile's broader allowlist did not become broader signed app claims.
The embedded profile is byte-identical to the existing cache file and the
Info.plist is byte-identical before/after signing. No profile, certificate or
private key was created, exported or replaced. Only ordinary code signing used
the existing signing identity; no Sync payload key was accessed.

All seven actual built runtime values match
`../../e2e/shared-sync-structure-development-scope-20260913.json`, SHA-256
5b3586741c8ab4711bf096ac6960b046732a43489bb4ae9c6018afa64439c9f7,
scope96950f6b-50e0-4e2c-9a94-852dc5099446. That prepared scope is not server-
verified or activated. A matching native schema7 candidate is still required;
the installed Native55ab and Device24/bba baseline are unchanged. DebugLocal25
is only the separate local-UX candidate and is not this entitled Sync partner.

Two initial read-only profile-inspection commands needed parser corrections
(plist dates/data are not JSON-convertible; the Python plist reader needed
buffered bytes). Neither changed a file/profile or weakened validation. The
final CMS read checked expiration and certificate membership successfully.

## Remaining actual gates

Bind the completed Native Structure source to its guarded app-only build and
the same prepared Development scope, then perform the normal, device-targeted
installation and short visible shared-Sync journey when UI/device access is
available. Use the existing first-use claim and synchronizable Keychain path;
do not copy keys or reset the previous bba zone/stores. This build did not start
any app, Simulator, iPhone host, CloudKit request or payload-key bootstrap.
Visible Home/Reader/private-lock and real cross-device/structure acceptance
remain open; no programmatic suite or Production/distribution pass is claimed.
