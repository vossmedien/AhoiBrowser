# Schreibhoheit zwischen Tab-Baum und Chromium-Session (H2)

Stand: 25. September 2026, Lane `crest-hardening`, Quellstand `b141fce`. Die
Analyse beruht ausschließlich auf Quellcode; nichts wurde gebaut oder
ausgeführt. Befunde 3, 4 und 5 unten sind im Code gegengeprüft. Die übrigen
sind aus Aufrufpfaden abgeleitet und als *abgeleitet* markiert.

## Regel

1. Eine Ahoi-Operation entscheidet den semantischen Übergang, zum Beispiel
   Workspace-Wechsel, Verschieben, Archivieren, Löschen oder Split, und
   committet ihn genau einmal.
2. Chromium führt aus (`TabStripModel`, `WebContents`) und meldet den Abschluss.
3. Beobachter einer selbst ausgelösten Änderung schreiben keinen Zustand
   erneut. Sie erkennen sie an einem Operationsschutz, der bis zum
   asynchronen Ende der Chromium-Ausführung reicht, nicht nur bis zum
   Rücksprung des Aufrufs.
4. Nur von außen ausgelöste Änderungen (Nutzer in nativer UI, Erweiterung,
   Wiederherstellung) werden als neue Tatsache übernommen.
5. Verspätete oder wiederholte Abschlüsse sind über eine prozesslokale
   Operations-ID idempotent.
6. Autoritäts-Epochen verfallen nur bei Änderungen, die die Vorbedingung
   tatsächlich betreffen, nicht bei Auswahl-, Titel- oder Ressourcenmeldungen.

## Übergänge

> Nachtrag 28. September 2026: Die folgende Tabelle fasst Übergänge zusammen
> und ist auf `b141fce` bezogen. Die aktuelle Zuordnung je Übergang mit
> Datei/Zeile an `de04e0aa` und der Status von S2–S8 stehen in der
> [H2.1-Quellzuordnung](crest-hardening-2026-09-28-single-writer-source-map.md);
> Restbefunde in [Übergabe 142](../../handoffs/crest-hardening/142-single-writer-residuals/HANDOFF.md).

| Übergang | Einstufung | Kern |
| --- | --- | --- |
| Tab schließen (einzeln, temporär), Navigation → TreeNode, Schlafen/Aufwecken, Drag zwischen Fenstern, Tab aktivieren | eine Autorität | wiederholte Bindungen und Löschungen sind dedupliziert |
| Tippnavigation im leeren Workspace (Patch 0054) | eine Autorität, solange Sidebar und `WorkspaceService` übereinstimmen | siehe Befund 3 |
| Workspace-Wechsel | Doppelschreiber (per Generation abgesichert) | Aktivierung eines verborgenen Tabs durch Befehlsleiste, Remote Focus oder Erweiterung schaltet den Workspace nachträglich über `ReconcileWorkspaceSurface` um |
| Aktiven Tab in anderen Workspace verschieben | **Doppelschreiber** | Befund 3 |
| Workspace löschen (eigene Website-Sitzungen) | **Doppelschreiber** | Befund 4, Übergabe 010 R6 |
| Live-Split archivieren | **Rückkopplung** (abgeleitet) | Befund 1 |
| Strukturcommit (Archiv, Split-Persistenz) | **Rückkopplung** (abgeleitet) | Befund 2 |
| Sitzungswiederherstellung beim Start | Doppelschreiber, Reihenfolge (abgeleitet) | Befund 5 |
| Split nativ anlegen, auflösen, umordnen | Doppelschreiber native ↔ Datensatz | nicht beobachtete native Änderung wird beim nächsten `Refresh` zurückgedreht |
| Split-Mitglieder über Workspaces verschieben | derselbe Fakt doppelt gespeichert | `workspace_id` des Split-Datensatzes wird nie nachgeführt; bei temporärem Pane wandert nur ein Pane |
| Gespeicherte Zeile mit offenem Tab löschen | Rückkopplung (für gespeicherte Seiten gewollt) | temporäre Zeile kommt mit neuer ID zurück; Sync sieht Tombstone und Neuanlage |
| Quick-Window-/Popup-Übernahme | unklar | Übernahme landet im aktiven oder ersten Workspace statt im Workspace des Öffners, obwohl das `WebContents` in der Partition des Öffners bleibt |

## Befunde

1. **Archivieren eines offenen Splits zerstört dessen Wiederherstellbarkeit
   (abgeleitet).** `ArchiveAgreedPages` schließt über
   `GroupPageClose::ClosePages()` ohne den `applying`-Schutz, den
   `CloseArchived` hat (`workspace_structure_controller_archive.cc:290-292`
   gegenüber `:334`). Das Schließen ist asynchron (`ClosePage()`).
   Chromium meldet danach `kSplitTabRemoved`. `OnSplitChanged` behandelt
   das als Auflösung durch den Nutzer und setzt den Tombstone des
   Split-Datensatzes (`workspace_structure_controller.cc:131-141`). Die
   spätere Wiederherstellung bricht deshalb ab (`:449-451`).
2. **Fremde Aktivität bricht Strukturcommits ab (abgeleitet).** Die Epoche
   steigt bei jeder Tabstrip-Änderung, auch bei reiner Auswahl
   (`session_bridge_observers.cc:463-465`), bei jedem Baumschreiben, auch
   automatischen Titeln (`:546-548`), und bei `OnLocalTabTreePersisted`
   (`:225-227`). Der Hintergrundcommit bricht dann mit `kCancelled` ab
   (`session_bridge_structure_persistence.cc:147-149`). Eine aktive Seite
   kann Archivieren und Split-Persistenz dauerhaft aushungern.
3. **Der aktive Workspace hat zwei Schreiber (geprüft).** Nach dem
   Verschieben des aktiven Tabs ruft die Sidebar `ActivateWorkspace`
   (`browser_sidebar_host_command_dispatch.cc:211`, `:239`). Das setzt nur
   das Sidebar-Modell
   (`SidebarTreeController::ActivateWorkspace` → `view_model_.ResetWorkspace`),
   nicht `WorkspaceService`. Patch 0054 liest dagegen den Service
   (`session_bridge_session.cc:161-162`), und `EnsureWorkspaceSurface`
   liest das Sidebar-Modell. In diesem Fenster kann getippte Navigation
   einen Tab im alten Workspace anlegen, der sofort verdeckt wird. Das ist
   ein plausibler Beitrag zum Defekt „Navigation tut nichts“, aber nicht als
   Ursache der aufgezeichneten Reproduktion nachgewiesen.
4. **Das Löschen hängt die zu schließenden Seiten um (geprüft).** Siehe
   Übergabe 010 R6.
5. **Die Wiederherstellung wendet Fenster- vor Tab-Metadaten an
   (abgeleitet).** `TrackBrowser` erzwingt zuerst den ersten Workspace
   (`session_bridge_observers.cc:68-72`). Danach setzt
   `ApplyPendingSessionMetadata` den Fenster-Workspace, bevor die Tabs ihren
   Workspace erhalten (`session_bridge_session.cc:247-258`). Die Sidebar
   reagiert auf den halb angewendeten Zustand: Sie aktiviert den falschen
   Tab oder lässt die Leerzustands-Überlagerung stehen
   (`browser_sidebar_host_core.cc:497-512`).

Korrekturideen und Tests: [Übergabe 011](../../handoffs/crest-hardening/011-single-writer-fixes/HANDOFF.md).
