# Receiver candidate: installed, visible peer proof blocked by screen lock

Installed source55abcf76498dcc8629f94e5ecebde89a4269ecf3, exact immutable
Development/bba scope SHA851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe.
Build61607, scoped prepare/sign/verify40936 and guarded install28512 EXIT0.
No current product build is running. The exact source lives in the known
isolated build worktree and refs/ahoi-preserved/native-sync-receive-55abcf7-20260913.

- Build receipt d1455677a2e8810ab4782bf029c7b5e13a073b76ab188cca55ea6798963d7cd8.
- CloudKit verification 48bf008a8c43c0b3adce588ccb85dfa409bca7607546a6e6c91ee63c37c381c4.
- Install receipt fb60719d7ea4a1ec8fbe0eeffe3b098c3608fd00ca9351330b1e016c82191d1c.
- Installed executable c62675d22e397265be0e556b33b178528f3f108196e535e41242bb527fefb9b7.

Canonical product commits: d38449b (15-file existing-provider receive bridge),
4b23125 (token-bound original setting read leases). The previous455 compile was
EXIT0 but was never installed. No canonical Structure/Mobile/UX WIP was included
in the installed55ab snapshot; no wire/schema/key/portal change or reset.

## Actual UI/access attempt

The normal CUA app binding launched own MacA PID14715 from the exact installed
path. It then returned cgWindowNotFound. Bundle-ID selection was ambiguous
because preserved rollback/candidate apps share that identifier, so no rollback
was opened. Exact-path AX/rebind and a normal Cmd+N could not target a window.
One normal installed-app reopen, one isolated CUA reset/rebind and the explicit
normal Finder startup path gave the same window error. No blind clicks,
AppleScript/CGEvent alternative or permission change followed.

A read-only one-second process sample showed the main thread in the ordinary
NSApplication event loop (not a receiver deadlock), with no new crashdump.
The decisive read-only environment check returned:

```
IOConsoleLocked = Yes
CGSSessionScreenIsLocked = Yes
```

Root asked the user once to unlock the Mac. No further UI/reopen/Phone/lock-poll
loop is authorized before that event. PID14715 was left running for the ordinary
unlock continuation; no hidden profile or store mutation was used as cleanup.
The read-only MacB check found its allowed path absent. MacB was never launched,
seeded or used as fictional peer evidence. Physical Device24 was not touched.

## Remaining acceptance

Need a genuine peer-created tab arriving in the already open Mac window without
a receiving-side SyncNow, forced focus or eager load. The authorized MacA/MacB
normal-profile test is distinct from the physical Phone gate. Only actual user
unlock plus a safely bound genuine peer can close this requirement.

Root permits narrow existing-target cache/authority/coalescing checks under this
specific E2E access exception. They are regression evidence only, never a real
CloudKit or physical-peer pass. All earlier26 runtime evidence remains preserved
as prior baseline evidence; it does not cover the receiver's new arrival path.
