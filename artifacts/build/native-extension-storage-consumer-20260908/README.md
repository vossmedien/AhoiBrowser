# Prepared Native write-request hook

This is a NOT-APPLIED two-file delta for Desktop's existing Patch0036 ownership,
requested in01a08182-b3b0-7a41-b45e-ba352ccca250. It applies AFTER851e3bb/0036;
it is not a new active patch-series entry or a build/install instruction.

`native-write-request-hook.proposed.patch` SHA256:
9f8ef3ca5fbb23970e41b9feec0afd1697a85d5204568f59d8171c2e30dcfecf.
Scope: extensions/browser/api/storage/storage_frontend.h and .cc only,
48 insertions /1 deletion. It provides ObserveSyncSettingsWriteRequests with a
scoped ID-only callback. Native sync-area Set/Remove/Clear notify before queueing;
empty Set/Remove and non-sync paths do not notify. Remove copies caller-owned
keys before callbacks. Shared callback-list lifetime plus WeakFrontend readback
prevents use of a released profile. No values, successful-write claim, upload
permission, global suppression or new wire/schema are introduced.

Why: completion-only Native observation may revoke an already queued remote
write too late. Common captures a revocable extension epoch before its read/UI
fence; a later Native request immediately invalidates it, including A->X->A.
Actual local authoring still depends on the existing successful write changes.

Preparation used a disposable TWO-FILE sandbox from the exact pinned originals
(header909f9e5d..., source5c70ae28...) plus the existing0036. Pinned Chromium
formatting, whitespace and git apply --cached --check succeeded there. The diff
preserves unrelated formatting. No product compile, test, runtime, profile,
key/Portal or native checkout/out action occurred. Canonical Patch0036 is untouched.
Common consumer WIP remains unbuilt until Desktop commits the matching Native hook.
