# Native document Mojo progress and test gate RED

Frozen b5d5e783 builds/signs/provenance-verifies. The 20-case native Mojo run
(one job/no retries) reports 14 SUCCESS, one CRASH and five NOTRUN. Real cache,
header, redirect, extension, inactive context and cross-origin boundaries execute;
the secret case hits ThreadRestrictions when the fake store deliberately waits.
MayBlock does not grant use of base synchronization primitives. The next source
adds ScopedAllowBaseSyncPrimitivesForTesting only around that synthetic wait.
No production task trait or restriction is weakened. Skipped batch cases are
not accepted and native core/editor/browser acceptance remains pending.
