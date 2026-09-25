# 008 – Level `website-sessions`: permissions and `chrome.cookies` stay shared

Status: ready
Owner lane: desktop (Master text, `docs/WORKSPACE_SESSIONS.md`, package 1b)
Base: user decision of 25 September 2026, recorded in ADR 0011
("Decided scope of level `website-sessions`")

## Decision

Level `Eigene Website-Sitzungen` separates cookies, site storage, workers and
the network/auth context only. Site permission decisions and the extension
`chrome.cookies` API stay profile-wide and are documented as shared. Full
separation comes from level `Vollständig getrennt` (own Chromium profile).
Per-partition permission and cookie-API patches are not scheduled ("Option A",
kept open and needing a new user decision).

## Text changes

Both files hold uncommitted desktop work, so these are replacements to apply
by hand.

1. `outputs/AhoiBrowser-Master-Zielprompt.md`, section „Verbindliche
   Workspace-Sitzungsentscheidung vom 5. September 2026“:

   Replace

   > Site-Berechtigungen sollen im lokalen Sitzungskontext gelten, ohne
   > automatische Übernahme einer Freigabe aus einem anderen Kontext.

   with

   > Site-Berechtigungen und die Cookie-Schnittstelle für Erweiterungen bleiben
   > in dieser Stufe profilweit gemeinsam und werden in der Oberfläche so
   > benannt; vollständige Trennung liefert die Stufe `Vollständig getrennt`
   > nach ADR 0011. Eigene Chromium-Eingriffe für getrennte Berechtigungen je
   > Partition sind eine offen gehaltene spätere Option und nicht beauftragt.

2. Same file, section on Workspaces (currently around line 896): replace
   „Site-Berechtigungsfreigaben gelten lokal im zugehörigen Sitzungskontext.“
   with „Site-Berechtigungsfreigaben gelten bei `Eigene Website-Sitzungen`
   profilweit und bei `Vollständig getrennt` je Profil (ADR 0011).“

3. `docs/WORKSPACE_SESSIONS.md`:
   - Table row „Site permission decisions“: scope becomes „Profile-wide in
     this level (shared with other Workspaces, disclosed in UI); separate only
     in level `isolated` (ADR 0011)“. The sync column stays „Never“.
   - Add „Extension `chrome.cookies` API | Profile-wide default partition in
     this level; not scoped per Workspace | Never“.
   - In the final paragraph, replace the gate sentence „The development flag
     must remain off by default until these surfaces preserve both
     per-context isolation and global extension installation/authority.“
     with: „The development flag may be enabled once handoff 003 (deletion)
     is accepted and the UI discloses that permissions and extensions are
     shared. Per-partition permission and `chrome.cookies` scoping is a later
     option (ADR 0011).“

## Acceptance change

PERM-LIVE-02 in `docs/reviews/crest-hardening-2026-09-25-crest-chromium-reference.md`
then has a defined result: revoking a permission in one `website-sessions`
Workspace also revokes it for the same origin in the others, and the UI told
the user this beforehand.
