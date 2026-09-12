# Isolated Mobile Development23

Source `fa53e316b3a2cfda81dc2f99fdd4a9b2e581b8c6`, CloudKitDevelopment0.1(23),
iPhoneOS/arm64 built successfully (session67411 EXIT0). The clean detached
snapshot and ordinary product-only build are bound in `candidate.json`; no
test target was built or run. The original unsigned app is preserved, and the
separate signed copy passes strict deep verification with the exact existing
Development entitlements/profile/device/certificate. All seven built runtime
values match the unchanged shared scope, and signing preserves Info.plist.

This correction is necessary because Build22's fresh cloud zone/key account
did not also isolate the fixed local SyncFormat3 directory and UserDefaults.
Build23 scopes snapshots, outbox/provider state, sessions/external-open receipts,
defaults/device identity, downloads/recovery and the normal persistent WebKit
data store to the same configured UUID. A partial or mismatching acceptance
tuple fails before product state is opened. Website-data clearing targets the
selected normal store. Existing product stores, preferences, cookies and keys
are not copied, reset or deleted. Ordinary product configuration keeps its
existing persistence. Build22 remains archived and unchanged.

Capacity before this two-job build:12 cores,69.49% aggregate CPU idle,40%
memory headroom, unchanged swapout count,75,809,764KiB free. Desktop confirmed
no Chromium compile was running. The two normal AppIntents metadata warnings
remain in the build log; no warning was suppressed.

No installation, app start or key/cloud access has occurred. The previous
device diagnostic proves `kAMDMobileImageMounterDeviceLocked`, and the user was
asked to unlock Servusla. No unchanged DDI retry was made. After device unlock,
coordinate install/start with the matching Mac candidate, verify the fresh
scoped local baseline before opt-in and exercise the visible real journey.
The three added scope tests are not run; they follow that visible journey (or
an explicit device-access exception). No old UI matrix is repeated here.
