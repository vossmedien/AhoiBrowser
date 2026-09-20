# Concrete digest regression — 20 September 2026

Executed after the real visible Mobile38 first-use/provider-status pass, not
instead of E2E. Exact source33bddb4f2d4a462d5d2419720372af72e6f5b45b; Mobile
product sources unchanged fromf21d089. Existing CoreTests test:
`CompanionKeyLifecycleTests/testConcreteKeyStoreDigestReadsRejectRevokedAuthorization`.

The MainActor caller invokes both concrete actor digest methods with revoked
authorization and requires the exact authorizationRevoked error. A silent nil
protocol default fails. Authorization prevents Security/Keychain access.

Actual xcodebuild EXIT0/TEST SUCCEEDED, one test passed, zero failures/skips,
body0.018s. Root independently read the selected test and terminal result.
Full command is the first two lines of `xcodebuild.log`; it used one build job,
parallel testing off and only the named Core test. The existing stale
SharedTabCreationProvenanceTests.swift and SharedTabFrozenContractTests.swift
were explicitly excluded, not reported as passing. Snapshot absolute-path cache
invalidation rebuilt Core/app/UI-test artifacts despite this narrow selection;
only the one Core test executed. No app/UI/Cloud journey or test matrix ran.
Own C645 was reported Shutdown before and after cleanup.

Original log/xcresult were preserved from
`/private/tmp/ahoi-mobile-digest-regression.j1tdPH/` into this canonical directory.
Log SHA256:80d5bed58cf3d0dc78e95741e19f7f71efe81116fd10a08e9a7f821c211829c6.
This is a regression check, not cross-device/push/Production acceptance.
