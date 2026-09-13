# Native profile-pair journey — installed55ab, 13 September2026

**PARTIAL, not a shared-tab or physical-device acceptance.** Both clients were
the actual installed/signature-bound Native55ab browser, with independent normal
MacA/MacB profiles and device UUIDs on ONE physical Mac. The existing Development
bba scope was retained; no fixture, key copy, SQL/pref injection or reset was used.

After the Mac reported unlocked, CUA resumed the original MacA session and
visibly showed `Synchronisiert und bereit`. Its process14715 was bound to the
MacA directory through a fresh open-file read; the ordinary startup choice was
continued without saving a new preference. MacB was created by the browser's
normal `--user-data-dir` path under the same strictly constrained acceptance
scope. Its initial Sync state was visibly OFF. Because CUA exposed only one app
identity for both processes, MacA was ordinarily quit through its own app menu
and verified absent before MacB was addressed.

MacB's normal Sync checkbox was enabled through the actual UI. The first server
send saved5 of6 records and retained one CKError14 conflict. One deliberate
normal Sync Now follow-up used the received server metadata, then saved/ACKed
all6 and the coalesced following records. CUA showed Ready plus the two existing
remote logical rows. No category or remote-control grant was made.

MacA was then normally restarted with the public IANA URL recorded in
`result.json`, while MacB stayed open. Its initial send retained three CK14
conflicts; the automatic follow-up later saved/ACKed5 records, then2. Both
processes remained alive. The IANA navigation ultimately appears with its real
title and exact URL in MacA's local NativeTree. Its History record is present
in BOTH Common stores, proving an actual cross-profile data transfer through
the normal provider. The corresponding TreeNode/Presence did not appear in
either Common store, and the new logical tab was not visibly accepted.

The subsequent bounded readback corrected the initial capture-failure
hypothesis: A knows B's older capability with an empty feature list, while B
holds its newer `shared-normal-tabs-v3` capability. The exact versions/ID are
in `result.json`. That stale capability legitimately prevents A's shared-tab
writer from opening. Root independently read the frozen55ab upload loop and
saved-record handler: `records[key]` and `mutations[key]` retain only the last
batch mutation for an entity, while the expected/ACK set contains only that ID.
Older Outbox rows can therefore remain and be sent afterward. The codec uses
the newer known server ChangeTag with the older domain payload. The worker's
bounded readback confirms B received its new capability at06:35:58UTC and the
old/empty one later at06:54:38; A received the old one at06:50:32. This matches
the concrete backward-write path. The current live server state was not queried.
The byte-match and exact mutation/version ACK protections themselves were not
shown to misattribute a newer record. The fix must retain those protections,
fully account for covered original mutation IDs and prevent stale payloads
from overwriting newer domain versions. It is NOT permission to weaken the
capability gate or assert that Native capture itself is defective.

During a further B navigation attempt, CUA's window/element access failed and
a fresh OS read showed the Mac locked again. The requested B URL was never
confirmed as loaded. Both freshly verified own test PIDs49166/90916 were
controlledly terminated; their real execution handles89088/46806 both ended
EXIT0 and the main processes were then absent. This cleanup is not a claimed
normal UI-Quit/restart test. All profile, key and Sync data remain.

Only after shutdown could the exclusive SQLite stores be read consistently;
the earlier live read returned `database is locked` and yielded no counts.
Final A/B Outboxes are0/0, retry attempts0/0, ACK-record counts20/10. Those
counts and a Ready label are not proof of tab completeness. Raw runtime logs
remain local and are SHA-bound in `result.json`; the live CUA AX/screenshot
observations are retained in the conversation. Unrelated GCM and ANGLE warnings
were not relabelled as CloudKit failures or silently suppressed.

The user subsequently directed the next phase to an own Simulator, without the
physical iPhone. No further phone/unlock request is required for that plan;
simulator-local, genuine CloudKit and physical-device evidence remain separate.
