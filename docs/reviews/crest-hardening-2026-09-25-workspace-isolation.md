# Optional vollständig getrennte Workspaces: Analyse und Empfehlung

Stand: 25. September 2026, Lane `crest-hardening`. Anlass: Nutzerwunsch,
Workspaces optional so vollständig wie Profile voneinander zu trennen.
Entscheidung: [ADR 0011](../decisions/0011-optional-isolated-workspace-profiles.md).

## Ausgangslage

| Stufe | Status | Getrennt | Gemeinsam |
| --- | --- | --- | --- |
| Gemeinsam (ADR 0002) | Produktstandard, installiert | nichts | Cookies, Logins, Site Storage, Verlauf, Passwörter, Erweiterungen, Berechtigungen |
| Eigene Website-Sitzungen (Paket 1b, `WORKSPACE_SESSIONS.md`) | Entwicklungsflag `AhoiWorkspaceWebsiteSessions`, standardmäßig aus | Cookies, Site Storage, Worker, Netzwerk-/Auth-Kontext über feste `StoragePartition` | Verlauf, Passwörter, Erweiterungen und deren Storage, Einstellungen; Berechtigungen und `chrome.cookies` derzeit noch profilweit |

Die Partition-Lösung trennt **Konten**, nicht **Personen oder Kontexte**.
Offene Grenzen laut `WORKSPACE_SESSIONS.md`: Chromiums Berechtigungsdienst
(`HostContentSettingsMap`) und `chrome.cookies` arbeiten pro Profil, nicht pro
Partition; beides braucht eigene Chromium-Eingriffe. Außerdem verschiebt
`SessionBridge::DeleteWorkspace` (`session/session_bridge_workspace.cc:231-243`)
offene Tabs samt ihrer isolierten Partition in den Fallback-Workspace, und
es gibt keine Aufräumlogik für Partition und Bindung.

## Was „wie Profile“ zusätzlich bedeutet

Ein Chromium-`Profile` trennt nativ: Cookies und Site Storage, Verlauf und
Omnibox-Vorschläge, Passwörter und Autofill, installierte Erweiterungen samt
Storage und Rechten, Website-Berechtigungen, Einstellungen/Suchmaschine,
Lesezeichen, Download-Verlauf, HTTP-Auth-Zugangsdaten und Zertifikatsentscheidungen.
Diese Trennung pflegt Upstream; Ahoi müsste sie nicht selbst nachbauen.

Wichtige Befunde im Ahoi-Code:

- Alle Ahoi-Dienste sind `ProfileKeyedServiceFactory`-Dienste:
  `SessionBridge`, `WorkspaceService`, `CommandService`, `ProfileSyncService`,
  `HttpAuthCredentialService`, `UboService`, `ArcImportService`,
  `ResourcePolicyService`. Ein zweites Profil erhält damit automatisch einen
  eigenen Baum, eigene Workspaces, eigenes Sync-Journal und eigene Credentials.
- Chromium bindet jedes Browserfenster an genau ein Profil, und ein
  `WebContents` kann das Profil nie wechseln. Ein Workspace eines anderen
  Profils kann daher nicht im selben `Browser`-Fenster wie die übrigen leben,
  und Tabs lassen sich nicht zwischen den Stufen verschieben, ohne neu zu laden.
- CloudKit verwendet heute einen Zonennamen aus dem Bundle
  (`cloudkit_sync_configuration_mac.mm:44`). Mehrere Profile desselben Macs
  würden ohne Profil-Namensraum in dieselbe Zone schreiben.

## Optionen

| Option | Bedienung | Aufwand | Bewertung |
| --- | --- | --- | --- |
| A. Partition ausbauen (Verlauf, Passwörter, Erweiterungen pro Partition) | Workspace bleibt im selben Fenster | sehr hoch, viele Upstream-Eingriffe in keyed services | Nicht empfehlen: baut Profile in einem Profil nach, widerspricht kleiner Fork-Fläche |
| B. Native Chromium-Profile, jedes mit eigenen Fenstern | wie Chrome-Profile: eigenes Fenster, eigener Workspace-Umschalter | mittel | Solide Grundlage, aber kein „Workspace-Schalter“-Gefühl |
| C. **Profilgestützter Workspace mit gemeinsamem Umschalter** (Arc-Modell) | Workspace-Option „Vollständig getrennt“; Wechsel ersetzt das Fenster deckungsgleich durch das Fenster des anderen Profils | hoch, aber mit nativer Isolation | **Empfehlung**; B ist ihre erste Ausbaustufe |

## Empfehlung

Drei wählbare Stufen je Workspace, gewählt beim Anlegen:

1. **Gemeinsam** (Standard, ADR 0002).
2. **Eigene Website-Sitzungen** (Paket 1b): getrennte Logins bei gemeinsamem
   Passwortmanager, Verlauf und Erweiterungen. Weiterführen; das ist der
   häufige Fall „Arbeits- und Privatkonto derselben Website“.
3. **Vollständig getrennt**: eigener Chromium-Profilkontext je Workspace,
   technisch Option C.

Stufe 3 als Profil, nicht als erweiterte Partition. Umsetzung in drei Schritten:

- **Schritt 1 – Isolierte Welt:** Anlegen erzeugt ein neues Chromium-Profil
  ohne Google-Anmeldung und Profilauswahl; sein Fenster zeigt die volle
  Ahoi-Oberfläche mit genau diesem Workspace. Löschen löscht das Profil über
  Chromiums Profil-Löschpfad nach Bestätigung, crash-sicher bei Neustart
  fortgesetzt. Erweiterungen, Passwörter, Berechtigungen und Verlauf sind
  getrennt. Die Stufe steht im Workspace-Menü und in der Workspace-Liste
  sichtbar.
- **Schritt 2 – Gemeinsamer Umschalter:** Ein prozessweites
  Workspace-Verzeichnis fasst die Workspaces aller geladenen Ahoi-Profile in
  einer Reihenfolge zusammen. Der Wechsel zu einem Workspace eines anderen
  Profils zeigt dessen Fenster mit identischer Größe und Position und
  verbirgt das bisherige. Die Wischgeste über eine Profilgrenze wird zu
  einem Fensterwechsel ohne Web-Reflow. Link-Routing, Quick Window,
  Befehlsleiste („In Workspace öffnen“), Arc-Import (Arc-Profil wird zum
  getrennten Workspace) und der portable Export wählen das Zielprofil
  ausdrücklich.
- **Schritt 3 – Sync und Mobile:** Sync bleibt pro Profil einzeln
  einschaltbar. Jedes Profil erhält einen eigenen CloudKit-Namensraum (Zone
  oder Präfix) und bleibt in derselben Kontogrenze. Mobile bildet einen
  getrennten Workspace mit eigenem `WKWebsiteDataStore(forIdentifier:)` ab.
  Website-Daten werden nie synchronisiert.

Bewusste Grenzen:

- Tabs, Splits und Drag-and-drop über Stufen hinweg übertragen nie
  Sitzungen. Ein Verschieben öffnet die URL im Ziel neu und sagt das an.
- Ein Split kann keine Profile mischen.
- Eine nachträgliche Umwandlung in Stufe 3 ist ein ausdrücklicher Umzug.
  Er nutzt den portablen Workspace-Export und -Import: Die Struktur zieht
  um, Logins nicht.
- Jedes geladene Profil kostet eigenen Speicher für seine Dienste.
  Profile werden erst beim Öffnen geladen und ohne offenes Fenster
  entladen; das gehört in die Performance-Messung (H3).
- Weiterhin keine Sicherheitsgrenze gegen Schadsoftware mit Nutzerrechten,
  aber eine echte Datentrennung innerhalb von Chromium.

## Folgen für die laufende Arbeit

- Paket 1b bleibt gültig. Für die offenen Grenzen der Stufe 2 (Berechtigungen,
  `chrome.cookies`) entscheidet der Nutzer, ob sie weiterhin eigene
  Chromium-Eingriffe erhalten oder als bewusst geteilt dokumentiert werden,
  weil Stufe 3 die vollständige Trennung liefert.
- Der Lösch-Befund (Tabs mit fremder Partition im Fallback) muss in Paket 1b
  vor Aktivierung behoben werden: Tabs eines gelöschten isolierten
  Workspaces werden mit Before-Unload geschlossen, Bindung und Partitionsdaten
  werden entfernt.
- ADR 0002 gilt weiter für die Standardstufe; ADR 0011 ergänzt die
  optionalen Stufen.
