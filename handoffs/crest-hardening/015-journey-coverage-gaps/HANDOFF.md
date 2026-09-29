# 015 – Coverage gaps of the Workspace level and deletion journey (H6)

Status: integrated d1ba87a (WS-ISO journey); WS-DEL-04/06-09 extensions queued
Owner lane: desktop (visible acceptance)
Reviewed: `b0c5960` (`tools/desktop_e2e/ws-level-deletion-journey.sh`)

The journey covers the level choice (its WS-LVL-01/02) and WS-DEL-01, 02, 03
and 05 on the product default launch. These cases from handoffs 003, 007, 010,
013 and 014 still have no visible or automated journey:

| Case | What is missing | Suggested extension |
| --- | --- | --- |
| WS-DEL-04 | kill between the tree commit and the partition cleanup | `kill -9` the PID right after the confirm press, then relaunch and run the existing binding/partition checks |
| WS-DEL-06 | startup cleanup never loads the partition | after that relaunch, assert that the partition directory does not reappear within 30 s |
| WS-DEL-07 | page opened while the prompt shows | open a second tab in "Kunde" (CDP `Target.createTarget` with the partition context) before confirming; assert that both pages are gone |
| WS-DEL-08 | no re-homing before the unload finished | poll `/json` and the AX tree during the close; no "Kunde" page may appear under the fallback |
| WS-DEL-09 | late page vetoes its own close | late page with a before-unload handler; cancel its prompt; assert it is not left without a Workspace |
| WS-ISO-01..04, 07, 14..16 | the whole `isolated` level | create "Vollständig getrennt", log in, check the main Profile does not see it (cookies and history), close, reopen from the Workspace menu, delete from that window (WS-ISO-16 last-window case) |

No extra build is needed; all of them run on the candidate reserved for the
ADR 0011 and WS-DEL acceptance.

## Lane note (25 September 2026)

Written in a desktop-owner session by mistake; adopted unchanged by the
`crest-hardening` lane after checking it against `b0c5960`.

## Owner intake (desktop, 2026-09-25)

- The isolated level has its own journey `tools/desktop_e2e/ws-isolated-journey.sh`
  (`d1ba87a`, WS-ISO-01/02/14/16/17).
- WS-DEL-04, 06, 07 and 08 extensions are queued for the deletion journey
  and run on the same candidate; WS-DEL-09 is not applicable (see 014).
