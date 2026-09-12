# Corrected Mobile Development24, installed but not launched

## Subsequent normal product start — 12 September

Root separately authorized a real product launch with Sync OFF. Device24
started normally at11:28:36UTC as PID5058, with no fixture/harness arguments or
terminate-existing option. The owned process was still present at11:47:02UTC.
Only the precise new acceptance namespace/defaults were read: one blank normal
browser tab, one local device/session pair with matching scoped UUIDs, no
workspaces/pages/history/bookmarks and no Sync/category/search overrides.
The namespace contained only the local browser/session snapshot files, modified
at launch, with no provider/engine/key-journal files. No old private app data
was read and no preferences, keys or CloudKit state were changed.

This is technical launch/local-state proof, **not a visible UI pass**. No installed
device-bound screenshot/AX tool was available without entering the foreign
Mac/Simulator CUA window. Hostlabel/metadata navigation remains NOT_RUN. The
prelaunch directory query failed generically, so it is not claimed as a proven
empty-directory baseline. A corrected CLI environment-argument invocation
failure is also retained in `product-start-receipt-20260912.json`.

## Original build and install evidence

Exact source `e2faf5440015512800f346a03e02de740a9e08b9` is the cleanfa53e31
baseline plus ONLY the two-file `7e19476` AppStorage correction. The original
three UI bindings now use the runtime model's scoped defaults. No e7abcff
Structure or private-lock implementation is included. `source.bundle` preserves
this detached source commit, with the existingfa53e31 commit as its prerequisite.

The ordinary cached product-only iPhoneOS/arm64 build54890 completed EXIT0 with
the same seven bba acceptance values and build number24. The unsigned original
and separate signed copy are retained. Strict/deep signature, exact signed
entitlements, unchanged Info.plist and the existing matching device profile
were verified. Two ordinary AppIntents metadata warnings remain unchanged.

Native install8681 completed EXIT0. Servusla reports `app.ahoibrowser.AhoiBrowser`
0.1(24), replacing23. Complete process readbacks found no Ahoi process before
(360 entries) or after (365 entries). No app start, uninstall, reset, container
copy, key or CloudKit operation occurred. The OS metadata confirms ID/version;
the source association is bound to the exact signed upload, not a direct device
Info.plist read. Original23 app/evidence remains preserved.

Before this two-job incremental build the host had55.24% aggregate CPU idle
across12 cores,47% memory headroom and65,550,240KiB free. No foreign compiler was
observed. The existing own DerivedData was reused; the worktree was clean.

This is the corrected schema6-baseline counterpart for Nativec8d9161. Visible
fresh-store/default-off verification and the real cross-device journey remain
NOT_RUN and require the shared runtime window. Installation does not approve or
prove CloudKit/key activation. Detailed bindings and raw receipts: `candidate.json`.
