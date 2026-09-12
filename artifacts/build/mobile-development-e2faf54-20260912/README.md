# Corrected Mobile Development24, installed but not launched

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
