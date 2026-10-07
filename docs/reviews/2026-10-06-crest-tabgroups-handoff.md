# Crest-Paket B: native Tabgruppen in Open Tabs

## Auftrag und eigener Fortschritt

- Delegation `3e38308c`, Session `390F31CB-E48E-445E-AFD4-1BC0FD3867AB`, eigener Thread/Goal `01a110d1-573a-7963-96c7-5defc6533dbd`.
- Worktree `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-crest-reddit-recherche-9a68d1f3-codex-3e38308c`, Branch `cockpit/codex-3e38308c`, Basis `c75bc93cd5c576b65c8097b162ae048122e9e0c9`.
- `get_goal` lieferte zu Beginn `goal: null`; genau ein eigenes Goal mit dem beauftragten Source-Paket angelegt. Keine fremde Zielidentität verwendet, keine Subdelegation.
- Parent: Session `05EFDE54-146B-47AD-89CE-A56EF1781938`, Thread `01a110a1-bc0a-7ab0-a66e-3ec779a1b88c`. Desktop-Owner: Claude `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread `049a3c1d-5edf-4d2e-a173-c89f7f9e0663`; deren Checkout und Kandidaten bleiben unberührt.

| Schritt | Stand | Ergebnis |
| --- | --- | --- |
| Goal, Ownership, Architektur und reale Caller prüfen | erledigt | Eigenes Goal/Worktree belegt; SessionBridge, Sidebar, TreeStore, Split- und Restore-Aufrufe verfolgt |
| Native Adapter und Ordneransicht erstellen | erledigt | Fünf neue C++-Dateien, keine Änderungen an gemeinsam besessenen Produktdateien |
| Exakten Wiring-Patch und kleine Regressionfälle erstellen | erledigt | Acht bestehende Dateien ausschließlich im beigefügten Unified-Diff; sechs native Regressionfälle als Source |
| Eigene Diff-/Callerprüfung und Source-Paket committen | erledigt | Source-Paket final; DCO-/Lane-Übergabecommit auf dem eigenen Branch, SHA wird durch `cockpit_report` gemeldet |
| Gemeinsamen Kandidaten bauen und sichtbar abnehmen | offen, Parent/Claude | Kein Build, Test-Binary, Inhouse-Zugriff, Computer Use oder Installieren in dieser Session |

Referenz: [Crest-Abgleich](2026-10-06-crest-adoption.md), [Reddit-Feedback](2026-10-06-crest-reddit-feedback.md). Aktuelle veröffentlichte Referenz ist Crest Development 0.7.11/1184; keine eigene Crest-Runtime-Abnahme. Architektur: [Chromium-Reuse](../CHROMIUM_REUSE_AND_MOBILE_SYNC.md), [Split View](../SPLIT_VIEW.md), [Website-Sitzungen](../WORKSPACE_SESSIONS.md).

## Eindeutige Abbildung

Eine native Chromium-Gruppe entspricht genau einem **flachen Projektionsordner unter Open Tabs** des tatsächlichen normalen Browserfensters und seiner Workspace-/Website-Sitzung. Native Gruppen-ID und Tab-Handles sind ausschließlich lokale Laufzeitreferenzen. Der Adapter liest das native Modell; er hält keinen zweiten Tab-/Sync-Store und schreibt keine Gruppenordner in `TabTreeStore`.

Mitglieder bleiben dieselben `TabInterface`/`WebContents` mit denselben logischen Tree-UUIDs. Saved-Seiten behalten ihren beliebig verschachtelten Saved-Elternordner, Saved Home und Saved-/Temporary-Status. Während der Gruppenprojektion unterdrückt die vorhandene `SetRuntimeCompositeSuppressedNodes`-Darstellung ihre separate Saved-Zeile; Ungroup stellt diese wieder her. Auch eingeklappte Gruppen bleiben eindeutig, ohne einen zweiten Tree-Insert. Ein Open-Gruppenordner kann deshalb nicht in einen Saved-Ordner verschachtelt werden; Saved-Ordner werden niemals durch Namen, Farbe, URL oder Position zu nativen Gruppen umgedeutet.

Gruppen über verschiedene tatsächliche Workspace-UUIDs oder lokale Website-Session-Kontexte werden **nach** abgeschlossenem Restore in getrennte native Gruppen mit gleicher Darstellung aufgeteilt. Die erste native Tabposition behält die ursprüngliche Gruppen-ID. Alle anderen Scopes bekommen native Gruppen; Tabs, Konten und Partitions bleiben unverändert. Der Sidebareditor akzeptiert nur Mitglieder im gleichen Fenster/Profile/Workspace/Website-Kontext. Ein fremdes Fenster wird nicht anhand einer zufällig gleichen Gruppen-ID angenommen. Der Scope vergleicht die tatsächliche `StoragePartitionConfig`, einschließlich nativer Extension-Partitions; fremde Domains werden niemals als Default-Website-Sitzung umgedeutet.

Fehlende/native-fremde BrowserContext-Zuordnungen, noch ungebundene Restore-Tabs oder ein bereits widersprüchlicher Split über mehrere Scopes werden nicht der aktiven Sitzung zugeschlagen. Solange der Besitzerzustand fehlt, liefert der Adapter keinen bearbeitbaren Gruppenordner und verändert die Gruppe nicht. Die normalen bestehenden Tabzeilen bleiben zuständig. Nach Bindung, nativer Änderung oder Restore-Abschluss wird ereignisgesteuert erneut abgeglichen. Ein dauerhaft scopeübergreifender Split wäre ein konkreter Konflikt mit der bestehenden Split-Sitzungsinvariante: Parent muss ihn am Kandidaten diagnostizieren; der Adapter löst ihn weder auf noch lädt er Seiten neu.

## Tatsächlicher Browserweg und Wiring

Parent-Aktualisierung vom 6. Oktober: Der Präsentations-Hunk wurde gegen
`ca1b641a` angepasst; der neuere `OnRuntimeMultiSelect`-Callback aus `8681500b`
bleibt erhalten. Alle sechs Crest-Handoffs ließen sich in der vorgesehenen
Reihenfolge gemeinsam auf eine temporäre Kopie dieses Stands anwenden;
Overlay-Whitespace, Sourcegrenzen und innere Patchsyntax PASS. Die
Präsentationsdatei hat danach 749 Zeilen. Keine gemeinsame Produktdatei
geändert; Compile, echte Gruppen-/Neustart-Reisen und native Review offen.

Nach dem tatsächlichen Compilerabbruch wurde der Hunk erneut an den
[Buildfix](2026-10-06-crest-build-fix-handoff.md) angepasst. Der gültige Host
wird im Bool-Callback ausdrücklich geprüft; ein abgelaufener Host liefert
`false`. Aktuelle Reihenfolge: Buildfix → Dichte → Tabgruppen → übrige vier
Handoffs. Gemeinsam gegen `75398749` angewendet: PASS; Präsentation jetzt
754 Zeilen. Keine Compilation oder Runtime-Abnahme.

[Wiring-Patch](2026-10-06-crest-tabgroups-handoff.patch), aktueller Präsentations-Hunk gegen `75398749` nach dem Buildfix, anwendbar ab Repo-Root nach den Source-Commits und dem Dichte-Handoff:

```sh
git apply --check docs/reviews/2026-10-06-crest-tabgroups-handoff.patch
git apply docs/reviews/2026-10-06-crest-tabgroups-handoff.patch
```

Anwendung und gemeinsame Edits gehören ausschließlich Parent/Claude nach Ownership-Abstimmung. Diese Session hat keinen gemeinsamen Dateihandback erhalten und den Patch daher **nicht** angewendet.

| Integrationsstelle | Wirkung |
| --- | --- |
| `session_bridge.h/.cc` | Profil-Bridge besitzt Adapter je `TabStripModel`; Getter für Sidebar; Adapter vor Bridge-Shutdown freigeben |
| `session_bridge_observers.cc` | `TrackBrowser` hängt Adapter nach den bestehenden Runtime-Bindings ein, einschließlich Fenster ohne Sidebar; `UntrackBrowser` löst ihn vor Tab-/Fensterentfernung ab |
| `browser_sidebar_host_presentation.cc` | Vorhandene Open-/Split-Row-Factory bleibt aktiv; native Mitglieder bekommen einen Ordnercontainer; Saved-Mitglieder und ganze Splits werden nur einmal präsentiert; bestehende Suchprojektion öffnet Ordner vorübergehend |
| `browser_sidebar_host_runtime_actions.cc` | Drop in freie Open-Fläche entfernt Gruppenmitgliedschaft, ohne Saved-Seiten zu entspeichern; native Gruppen-Reorders bewahren ebenfalls den Speicherstatus |
| `extensions/BUILD.gn` | Zwei schlanke Source-Sets für Adapter und Views; vorhandene Chromium-Tabgruppen-/Views-Abhängigkeiten |
| `session/BUILD.gn` | Adapter in tatsächlichem Session-Target; sechs Regressionfälle im vorhandenen Session-Testtarget, ohne neue Test-Binary |
| `ui/sidebar/BUILD.gn` | Ordneransicht in tatsächlichem Sidebar-Target |

Die Grenze `session_bridge_api` verhindert einen GN-Zyklus: der Adapter hängt von der vorhandenen API ab, das Session-Implementierungstarget vom Adapter. Keine neue Abhängigkeit außerhalb des bereits verwendeten Chromium-/Ahoi-Stacks. Die 800-Zeilen-Grenze bleibt auch nach Patch-Anwendung eingehalten; die bereits 800-zeilige Sidebar-Host-Headerdatei benötigt keinen Edit.

Extension-Aufrufe `chrome.tabs.group/ungroup` und `chrome.tabGroups.update` ändern das reale `TabStripModel`. `TabGroupedStateChanged` und `OnTabGroupChanged` lösen nur einen zusammengefassten, geposteten Abgleich aus. Die tatsächlichen Mutationen laufen außerhalb von Chromium-Observer-/Reentrancy-Stacks. Vom Adapter selbst erzeugte Events werden innerhalb desselben Abgleichs berücksichtigt; allgemeine Bridge-Callbacks werden nicht zurückgesendet und können daher zwischen zwei Fenstern keine Observer-Schleife bilden. Die vorhandene Runtime-Presentation-Subscription aktualisiert die Sidebar.

## Wirksame UI-Aktionen und Lifecycle

- Ordnerkopf anklicken oder per Tastatur auslösen: Collapse/Expand über `ChangeTabGroupVisuals`; die native Darstellung ist maßgeblich. Suche zeigt Mitglieder vorübergehend, ohne das gespeicherte Collapse-Bit zu ändern.
- **Weitere Optionen → Umbenennen**: lokales Textfeld; Enter schreibt den nativen Namen, Escape verwirft. Passive Favicon-/Titelupdates zerstören keinen laufenden Edit. Gruppenentfernung oder Workspace-Wechsel beendet den Edit durch Reprojektion.
- **Weitere Optionen → Farbe**: Chromiums vollständige Palette und lokalisierte Farbnamen; nativer Farbwert und thematische Folder-Iconfarbe werden verwendet.
- Live-Tab oder Saved-Live-Zeile auf den Open-Gruppenordner ziehen (nativer MOVE-Cursor; vorhandene Row-Drophighlights werden abgeräumt): `AddToExistingGroup`, gleiche reale Sitzung erforderlich. Ein geschlossener Saved-Link wird dadurch nicht neu geöffnet.
- Mitglied in freie Open-Fläche ziehen: `RemoveFromGroup`; Saved-Zeile bleibt Saved. Bei einem Split bewegt die Gruppenaktion stets dessen vollständige Einheit, damit Chromiums API ihn nicht durch eine Teilmenge auflöst.
- **Weitere Optionen → Ungroup**: Ordner entfällt, Mitglieder bleiben geöffnet; keine Tree-Löschung und kein Save-/Unsave. Ein leerer nativer Ordner existiert nach Entfernung des letzten Mitglieds nicht weiter.
- Schließen einer vorhandenen Tabzeile bleibt `CloseRuntimeTab → TabInterface::Close`. „Alle temporären Tabs schließen“ bleibt die vorhandene `GroupPageClose::Ask`-Frage mit Veto. Gruppenaktionen schließen keine Seiten und fragen daher kein BeforeUnload ab. Bei einem Veto bleibt der native Zustand bestehen; der Adapter tombstonet nichts vorab.
- Native Detach/Attach, TabReplace/Discard, Split- und Workspace-Änderungen werden nach der bestehenden Bridge-Reconciliation gelesen. Kein Aufruf öffnet, klont oder lädt `WebContents` neu. Veraltete UI-Referenzen werden gegen reales Fenster/Profile/Modell erneut geprüft.
- Neustart: native `SessionService` persistiert Gruppen und Visuals, Ahois bestehende `TabSessionMetadata` persistiert Workspace und Tree-UUID. `SessionRestore::RegisterOnSessionRestoredCallback` und Bridge-Ready/Binding-Callbacks gleichen erst nach Restore ab. Es gibt keinen separaten Gruppen-Persistenzcodec, keine CloudKit-Handles und keine Aktivierung von Chrome Sync. Start ohne Wiederherstellung erzeugt keine Phantomordner.

## Prüfung und offene Abnahme

Der API-Abgleich erfolgte ausschließlich anhand offizieller Chromium-Quelldateien für die tatsächliche Basis-Konfiguration M155 `155.0.8059.26`, Commit `16c3e55476d3564bea713314b2fff638749ce3e6` ([TabStripModel](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_strip_model.h), [Observer](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_strip_model_observer.h)). README/Split-Dokumentation enthält noch historische M154-Angaben. Kein Zugriff auf das gemeinsame Chromium-Checkout und keine Änderung der Pin-Konfiguration.

Belegte Prüfungen: eigener Source-/Callercheck einschließlich nativer Session-Metadaten-Serialisierung (Name/Farbe/Collapse) und Restore-Visuals; `git apply --check` auf unverändertem Basis-Worktree; nur erlaubte neue Dateien; keine bestehenden Produktdateien geändert. `git diff --cached --check` für Produkt-/Markdown-Dateien erfolgreich (Unified-Diff-Kontext separat behandelt); angefügte Patchzeilen separat auf Whitespace geprüft. Pfadcheck bestätigt genau sieben erlaubte neue Dateien; alle Ahoi-Includes und Handoff-Links auflösbar. Native und materialisierte Ahoi-Quellen halten das 800-Zeilen-Limit ein. `python3 tools/check_lane_boundaries.py --all --worktree --since c75bc93cd5c576b65c8097b162ae048122e9e0c9`: 0 Fehler, 0 Hinweise. Keine Compilation oder C++-Testausführung; die sechs Fälle bleiben `NOT_RUN`.

Sechs Source-Regressions in `tab_group_sidebar_adapter_unittest.cc`, **NOT_RUN**:

1. Native Name/Farbe/Collapse → Readback → Sidebaränderung → Ungroup; kompletter Tree-Snapshot, Saved-Parent, UUID, Temporary-UUID und WebContents bleiben gleich; veraltetes Ungroup schlägt ohne Wiederanlage fehl.
2. Ein Split-Pane hinein/heraus bewegt die vollständige Split-Einheit, Split-ID bleibt erhalten.
3. Extension-Gruppe über zwei Workspaces wird getrennt; die Sidebar lehnt erneutes Vermischen ab.
4. `AddToGroupForRestore` plus `RestoreTabSessionMetadata` akzeptiert neu vergebene lokale Gruppen-ID bei gleicher Tree-Identität und gleichen Visuals. Das ist ein Restore-Seam-Test, kein Prozessneustartbeleg.
5. Tatsächliche Fixed-StoragePartition innerhalb desselben Workspaces wird nicht mit Default-Sitzung vermischt; WebContents und Website-Binding bleiben erhalten.
6. Detach/Attach in ein zweites reales Testfenster erhält UUID/WebContents, lehnt stale Quellfensteraktionen ab und bleibt nach erneutem Leerlauf ohne weitere Observer-Invalidierungen.

Parent baut zuerst den gemeinsamen Browserkandidaten über den vorhandenen Guard-Pfad und prüft sichtbar: Extension erzeugt/benennt/färbt Gruppen; Sidebar benennt/färbt/klappt zurück; Mitglied hinein/heraus und Ungroup einschließlich Saved-Seite in verschachteltem Ordner; vollständige Zwei-/Drei-/Vier-Pane-Gruppenaktionen; Gruppentransfer zwischen Fenstern; Workspace-Wechsel und Website-Sitzungsgrenzen; BeforeUnload-Schließen abbrechen; letztes Mitglied schließen; regulärer Quit/Restart mit Wiederherstellung von Namen/Farbe/Collapse/Mitgliedern und Tree-Identitäten. Dieselbe Extension muss die durch Sidebaraktionen geänderten nativen Gruppen zurücklesen. Kein Gruppen-Handle darf in Sync-Export auftauchen.

Danach genügen auf demselben Kandidaten `ahoi_session_unittests --gtest_filter='TabGroupSidebarAdapterTest.*'` sowie die betroffenen vorhandenen Session-Binding-/Split-/BeforeUnload-Regressionen bei einem beobachteten Lifecycle-Fehler. Compilation, sichtbare E2E, echter Neustart, Extension-API-Journey, Defaultbranch/Push und Auslieferung bleiben **offen beim Parent/Claude**. Dieses Source-Paket behauptet keine Browserfunktionsabnahme.
