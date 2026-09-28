<!-- Crest H2.1, 28 September 2026. Quellstand de04e0aa; nur gelesen, nichts gebaut oder ausgeführt. Erarbeitet mit einem lesenden Hilfsagenten; die Hauptsession hat die Befunde zu S3, S5, S6 und Merge-Undo an der Quelle gegengeprüft und S3 (c) korrigiert. Ersetzt die zusammengefassten Zeilen der Übergangstabelle im Audit vom 25. September; dessen Regel bleibt gültig. -->

# H2.1 – Schreibhoheit Tab-Baum / Session / Split / Quick Window / Popup / Archiv / Workspace-Wechsel

Stand: 28. September 2026, Quellstand **`de04e0aa`** (Arbeitsbeginn `7f04f07e`;
`git diff --stat 7f04f07e de04e0aa -- overlay patches` ist leer, der Desktop-Quellstand
ist also identisch). Nur Quelllesung; nichts gebaut, ausgeführt oder geändert.
Alle Zeilennummern sind an HEAD gelesen. Was nicht im Code nachgewiesen ist, ist mit
*abgeleitet* markiert. Vorgänger: `docs/reviews/crest-hardening-2026-09-25-single-writer-audit.md`
(Stand `b141fce`), Übergabe 011.

## Dateikürzel

Pfade relativ zu `overlay/chromium/src/ahoi/browser/`.

| Kürzel | Datei |
| --- | --- |
| obs | session/session_bridge_observers.cc |
| ses | session/session_bridge_session.cc |
| sb | session/session_bridge.cc |
| ws | session/session_bridge_workspace.cc |
| rt | session/session_bridge_runtime.cc |
| rm | session/session_bridge_website_session_removal.cc |
| mg | session/session_bridge_workspace_merge.cc |
| sp | session/session_bridge_structure_persistence.cc |
| syp | session/session_bridge_sync_persistence.cc |
| wsc | session/workspace_structure_controller.cc |
| arc | session/workspace_structure_controller_archive.cc |
| nws | session/native_workspace_structure.cc |
| sri | session/session_restore_integration.cc |
| wss | navigation/workspace_service.cc |
| lrd | navigation/link_routing_dispatch.cc |
| core | ui/sidebar/browser_sidebar_host_core.cc |
| cmd | ui/sidebar/browser_sidebar_host_command_dispatch.cc |
| mv | ui/sidebar/browser_sidebar_host_move_command.cc |
| tree | ui/sidebar/browser_sidebar_host_tree_actions.cc |
| disc | ui/sidebar/browser_sidebar_host_discovery.cc |
| rta | ui/sidebar/browser_sidebar_host_runtime_actions.cc |
| wsd | ui/sidebar/browser_sidebar_host_workspace_dialog.cc |
| wmg | ui/sidebar/browser_sidebar_host_workspace_merge.cc |
| split | ui/sidebar/browser_sidebar_host_split_actions.cc |
| tv | ui/sidebar/sidebar_tree_view.cc |
| sdc | ui/split_drop/split_drop_controller.cc |
| pop | ui/popup/popup_overlay_controller.cc |
| qw | command_bar/quick_window_chromium.cc |
| cea | command_bar/command_execution_adapter_chromium.cc |
| P0054 | patches/chromium/0054-ahoi-empty-workspace-address-context.patch (in `.work`: `chrome/browser/ui/navigator/browser_navigator.cc:717-733`) |
| P0064 | patches/chromium/0064-ahoi-restore-workspace-into-existing-window.patch (in `.work`: `chrome/browser/sessions/session_restore.cc:838`) |

Gemeinsame Bausteine, auf die die Tabelle verweist:

- **Workspace-Service-Idempotenz**: `WorkspaceService::SetActiveWorkspace` kehrt bei gleichem Ziel ohne Benachrichtigung zurück (wss:112-114).
- **Sidebar-Generation**: `workspace_surface_generation_` wird bei jedem Workspace-Wechsel (core:636-640) und jeder Aktivierungs-/Entfernen-Meldung (core:655-665) erhöht; `ReconcileWorkspaceSurface` verwirft veraltete Aufträge (core:548-552).
- **Struktur-Epoche**: `LocalAuthority()` bindet `scope_->epoch` (wsc:132-138); `OnNativeChanged` erhöht sie außer bei `applying` (wsc:156-162); `OnNativeMetadataChanged` plant nur neu (wsc:164-169).
- **`applying`-Schutz** (synchron): `MaterializeSplits` (wsc:458-459), `MaterializeSplitForActivation` (wsc:419-420), `CloseArchived` (arc:360-361). Er endet mit dem Aufruf, nicht mit Chromiums asynchronem Abschluss.
- **Sync-Apply-Schutz**: `applying_synced_tree_snapshot_` (syp:184) unterdrückt Epochen-Bumps (obs:556) und markiert Bindungen als ungültig statt sie neu anzulegen (obs:580-585).

## 1. Übergänge (eine Zeile je Übergang)

Einstufung nach Regel des Audits: **eine Autorität** / **Doppelschreiber** / **Rückkopplung**.

### Tab-Laufzeit ↔ Tab-Baum ↔ Session

| # | Übergang | entscheidet | führt aus | beobachtet | Einstufung | Operationsschutz / Idempotenz |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | Tab schließen, einzeln (Chromium-UI, `tab->Close()`) | Nutzer/Chromium (extern) | `TabStripModel` | obs:481-487 (`kRemoved`+`kDelete` → `ScheduleTemporaryPageClose`, `RemoveRuntimeTab`), obs:190-193 (`OnTabWillDetach` kDelete); Sidebar core:655-665 | eine Autorität | `RemoveRuntimeTab` ist bei fehlendem Eintrag no-op (rt:142-145) |
| T2 | Temporären Tab schließen → Zeile löschen | rt:168-183 (nur `is_temporary`, nicht beim Beenden/`closing_all`, nicht archiviert) | rt:190-203 `DeleteClosedTemporaryPage` (gepostet) | Tree-Observer obs:550-613 | eine Autorität | `pending_temporary_closes_` je `node_id` (rt:181-183, rt:192); erneute Bindung verhindert Löschung (rt:193); Archivknoten ausgenommen (rt:178, rt:194) |
| T3 | Neuer Tab → temporärer TreeNode | rt:65-138 `TrackRuntimeTab` (Workspace = aktiver Workspace des Fensters, rt:114) | rt:205-251 `EnsureTreeNodeForTab` → rt:253-302 `CreateTemporaryNodeForTab` (gepostet über rt:304-322) | obs:476-479 | eine Autorität | `node_id`/`pending_node_id` reserviert die ID (rt:113, rt:281-283); Guard `node_id`/`shared_binding_invalidated`/`closing_with_deleted_workspace` (rt:212-218, rt:256-259) |
| T4 | Navigation → TreeNode (URL/Titel) | Chromium (extern) | obs:220-275 `OnTabUIChanged` → `UpdateSavedPageMetadata` (obs:267-271) | Store → `OnTabTreeChanged` (obs:550) als `kRenamed` → `OnNativeMetadataChanged` (obs:557-561) | eine Autorität | Titel nur ersetzen, wenn noch automatischer Titel (obs:257-262); URL nur bei echter Navigation (obs:266); aufgeschobene Knoten (obs:232-235, obs:277-309) |
| T5 | Schlafen legen / Aufwecken | Sidebar `kSleepTab` cmd:756-760 (`memory::SleepTab`) bzw. Chromium-Discard | Chromium ersetzt `WebContents` | obs:163-182 `OnTabWillDiscardContents` → obs:127-161 `UpdateRuntimeTabContents` | eine Autorität | Contents-Zuordnung wird ersetzt, nicht neu gebunden; Kollision mit anderem Tab → Entbinden (obs:146-153) |
| T6 | Drag zwischen Fenstern (native) | Nutzer/Chromium (extern) | Chromium Detach/Insert | obs:195-202 (Detach, gepostetes `RemoveDetachedTabIfStillUnattached` obs:311-346), obs:205-218 `OnTabDidInsert`, rt:83-95 (bestehender Eintrag behält `workspace_id`); Zielfenster-Sidebar core:655-665 → core:553-563 | Doppelschreiber (per Generation abgesichert) | Tab behält seinen Workspace; das Zielfenster wechselt nachträglich per `ReconcileWorkspaceSurface(follow_selected_tab)` (core:553-563) – Generation core:548 |
| T7 | Tab aktivieren, nativ (Tabstrip, Erweiterung `tabs.update`) | extern | `TabStripModel::ActivateTabAt` | obs:506-508 `UpdateLastActiveTab`; Sidebar core:655-665 → core:545-566 | Doppelschreiber (gewollt: externe Tatsache, per Generation) | Generation core:548; `SetActiveWorkspace` idempotent (wss:112-114) |
| T8 | Tab aktivieren durch Ahoi (Befehlsleiste, Remote Focus) | sb:603-632 `ActivateTabInItsWorkspace`; Aufrufer cea:136-139, sb:640-642 | sb:615-617 Workspace zuerst, dann sb:626-630 `ActivateTabAt` | core:628-646 (`OnActiveWorkspaceChanged` → `ActivateWorkspace`), core:655-665 findet Tab bereits sichtbar | eine Autorität | Generation (core:639); `SetActiveWorkspace` idempotent |
| T9 | Tab aktivieren durch Sidebar-Discovery „offener Tab“ | disc:462-487 | disc:477-479 `model->ActivateTabAt` **ohne** Workspace-Wechsel | core:655-665 → core:553-563 schaltet Workspace nachträglich | **Doppelschreiber** (S5-Rest) | nur Generation; kein Vorab-Workspace-Wechsel |
| T10 | Zuletzt benutzter Tab (Wheel/Relativ) | rta:583-620 (Kandidaten aus Sidebar-Viewmodel rta:589-590), ses:476-533 (Service) | rta:631-633, ses:530 | core:655-665 | eine Autorität (nur Tabs des aktiven Workspace) | keine Operations-ID; Filter auf Workspace |
| T11 | Tippnavigation im leeren/abweichenden Workspace | P0054 (Hunk-Zeilen 14-29; `.work` 717-733) über sri:197-208 → ses:154-172 `IsTabInActiveWorkspace` (liest `WorkspaceService`) | Chromium `Navigate` mit `NEW_FOREGROUND_TAB` | rt:114 bindet an aktiven Workspace; Leerzustand über core:530-543 (liest **Sidebar-Viewmodel**, core:535-536) | eine Autorität, solange Viewmodel = Service (siehe Befund 3) | keiner nötig (synchron) |
| T12 | Session-Metadaten schreiben (Fenster/Tab) | ses:535-559, ses:561-589 | `SessionService::AddWindowExtraData`/`AddTabExtraData` | – | eine Autorität | reine Projektion des Laufzeitzustands; Nachschreiben nach Einfügen gepostet (ses:370-381) |

### Workspace-Wechsel, Verschieben, Löschen, Zusammenführen

| # | Übergang | entscheidet | führt aus | beobachtet | Einstufung | Operationsschutz / Idempotenz |
| --- | --- | --- | --- | --- | --- | --- |
| W1 | Workspace-Wechsel (Sidebar-Menü, Switcher, Befehlsleiste, Discovery, Dialog Neu/Duplizieren) | cmd:234-247, cmd:549-556, workspace_transition.cc:42, cea:225-243, disc:527-540, wsd:568-569, wsd:582-583 | ws:63-76 → wss:100-125 | Bridge obs:538-548 (Fenster-Metadaten), Sidebar core:628-646 → core:377-399 → core:459-528 (aktiviert Tab oder Leerzustand) | eine Autorität (Service) | wss:112-114; Generation core:639 |
| W2 | Workspace-Wechsel als Folge einer Tab-Aktivierung (`follow_selected_tab`) | core:553-560 (Sidebar-Beobachter) | ws:63-76 | wie W1 | **Doppelschreiber** (per Generation) | core:548; Rückkehr ohne Re-Entry (core:561-563) |
| W3 | Workspace-Wechsel beim Link-Routing / Remote-Öffnen | lrd:94-105; sb:578-582 | ws:63-76, dann `OpenGURL` (lrd:106, sb:583) | T3 bindet an nun aktiven Workspace | eine Autorität | wss:112-114 |
| W4 | Hand-over in anderes Profil / getrennten Workspace | cmd:241-245, cmd:249-299 | `RunHandOver` → `PresentProfileWindow`; danach cmd:279-281 | – | eine Autorität (*abgeleitet*, `RunHandOver` nicht gelesen) | keine Operations-ID in cmd sichtbar |
| W5 | Aktiven Tab (gespeichert) in anderen Workspace verschieben | cmd:400-413 bzw. mv:57-70 (`PerformGroupedDrop`) | Tab-Baum-Store; Laufzeit folgt dem Knoten obs:589-603 | danach cmd:422-426 / mv:75-79 `SetActiveWorkspaceForWindow`; Sidebar core:628-646 | eine Autorität (S1) | wss:112-114 |
| W6 | Aktiven temporären Tab in anderen Workspace verschieben | cmd:382-390 bzw. mv:83-92 (`SaveTemporaryTabAtDrop`) | Store + Bindung | cmd:391-395 / mv:93-94 Service; Sidebar folgt | eine Autorität (S1) | wss:112-114 |
| W7 | Workspace löschen, ohne eigene Website-Sitzungen | ws:247-261 | rm:160-235 `CommitWorkspaceDeletion`: Store (rm:180-181), Laufzeit auf Fallback (rm:221-225) | Store-Observer obs:574-587 entbindet dieselben Tabs (`clear_workspace=false`) und plant Neubindung; `ReplaceWorkspaces` löscht aktive Zuordnung (wss:77-96); Fallback-Aktivierung doppelt: obs:433-439 (`ReconcileWorkspaces`) und core:616-626 → core:363-375 (`ActivateInitialWorkspace`) | **Doppelschreiber** (gleiches Ergebnis: jeweils erster Workspace; zweiter Schreiber prüft `has_value`, core:366, obs:434) | Entbinden idempotent (ws:420-435); kein Operations-ID |
| W8 | Workspace löschen mit eigenen Website-Sitzungen | rm:78-111 (`GroupPageClose::Ask`, Gate `workspace_deletion_close_` rm:87-91) | rm:113-158: Commit rm:128-129, dann `ClosePages` rm:130-132, R3-Nachzügler rm:137-145, Löschen der Partition 3 s später rm:147-156 | T3/`ReconcileWorkspaces` überspringen `closing_with_deleted_workspace` (rt:215, obs:410); Entbinden mit `clear_workspace=true` (rm:215) | eine Autorität (Befund 4 behoben, s. u.) | Gate rm:87; Flag je Tab rm:213; **keine** Operations-ID; Zeitverzögerung statt Abschluss (rm:151-155) |
| W9 | Workspaces zusammenführen (ADR 0012), gleicher Kontext | wmg:74-94 → mg:73-107 | mg:169-260: Store `MergeWorkspace` (mg:201-205), ungebundene Tabs → Ziel (mg:231-240), Fenster auf Ziel (mg:242-248), Routing (mg:249-254), Snapshot (mg:255) | gebundene Tabs folgen ihren Knoten über obs:589-603 (Kommentar mg:222-223) | eine Autorität | Gate `workspace_deletion_close_` (mg:88-92); keine Operations-ID |
| W10 | Zusammenführen mit getrenntem Kontext | mg:95-112 (Frage), mg:115-146 | mg:141-142 Commit, mg:147-158 Schließen, mg:159-165 Partition nach 3 s | Schließende Tabs markiert und ohne Workspace (mg:225-229) | eine Autorität | wie W8; kein Undo (mg:189-190) |
| W11 | Zusammenführen rückgängig | Nutzer ⌘Z → `UndoLastMutation` (core:214, tv:452, tree:559, page_actions:238) | Store (tab_tree/tab_tree_store_move_delete.cc:456; leerer Quell-Workspace über `kWorkspaceMerge`, tab_tree_store_merge_workspace.cc:136, Commit `023e6126`) | obs:552-555 → mg:262-273 `RefreshWorkspacesAfterUndo`; gebundene Tabs folgen Knoten obs:589-603 | **Doppelschreiber / unvollständig**: Bridge macht mg:231-254 nicht rückgängig (Fenster bleiben auf Ziel, ungebundene Tabs bleiben im Ziel, Routing-Umleitung bleibt; mg:262-273 ruft nur `RefreshWorkspaceSnapshot`) | Undo-Beleg im Store; Bridge ohne Operations-ID |
| W12 | Fremdänderung aus Sync (Workspace-Liste/Baum) | extern (Sync) | sb:497-526 → Store `ReplaceWithSnapshot`, sb:528-565 | obs:531-536 → obs:379-443; obs:574-587 mit `shared_binding_invalidated` | eine Autorität (externe Tatsache) | `applying_synced_tree_snapshot_` (syp:184, obs:556, obs:580); `snapshot == current` → no-op (sb:514-516) |

### Archiv und Strukturcommit

| # | Übergang | entscheidet | führt aus | beobachtet | Einstufung | Operationsschutz / Idempotenz |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | Seiten archivieren (manuell/automatisch) | arc:156-180 (`GroupPageClose::Ask`) → arc:182-193 | arc:215-338: Eintrag + `Persist` (arc:268-282), nach Erfolg `MarkSplitsClosingForArchive` + `ClosePages` (arc:313-320) oder `CloseArchived` (arc:322, arc:340-379) | obs:481-487 (Tab weg), rt:178/rt:194 (archivierter Knoten wird nicht gelöscht), wsc:191-239 | eine Autorität | Gate `archive_close_`/`persisting_`/`publish_pending_` (arc:160); Archiv-ID inhaltlich (arc:254); `applying` nur in `CloseArchived` (arc:360-361); **`done(true)` vor Schließ-Abschluss** (arc:316-319) |
| A2 | Live-Split archivieren | wie A1 | arc:315 markiert Split-Token, arc:316 schließt | wsc:197-212 verbraucht Token, lässt Datensatz lebendig | eine Autorität (S2 behoben) | Token mit Zeitablauf 30 s (`ArchiveCloseTokenLive`, wsc:34-36; Bereinigung wsc:271-274) – Zeit, keine Operations-ID |
| A3 | Archiv wiederherstellen | arc:407-433 (Nutzer) | Persist arc:505-…; Split über `MaterializeSplits` (wsc:455-534) bzw. beim Öffnen tree:246-251 → wsc:413-453 | – | eine Autorität | `restored` → sofort `done(true)` (arc:423-426); `applying` wsc:420, wsc:459 |
| A4 | Strukturcommit (Archiv, Split-Persistenz, Import) | wsc:265-309 `Refresh`, wsc:536-568 `Persist` | sp:106-172 `CommitWorkspaceStructureState` auf Persistenz-Runner | Epoche: obs:465-471, obs:556-562, wsc:171-189, syp:225-227; Abbruch über `pending_tree_apply_cancelled_` aus sb:319-321 | **Rückkopplung (teilweise behoben)** – siehe Befund 2 | Epoche wsc:132-138 + Abbruch-Flag sp:134, sp:143-148; abgebrochener Commit bleibt `dirty_` und wird erst beim nächsten Ereignis neu geplant (wsc:563, wsc:164-168) |
| A5 | Automatisches Archiv (Frist) | `ScanArchiveDeadline` (wsc:296, wsc:307) | wie A1 mit `auto_cancel` (arc:179) | wie A1 | eine Autorität | wie A1 |

### Split

| # | Übergang | entscheidet | führt aus | beobachtet | Einstufung | Operationsschutz / Idempotenz |
| --- | --- | --- | --- | --- | --- | --- |
| S-a | Split nativ anlegen (Drop) | sdc:521-545 (`CreateOrAddToSplitFromDrop`) | `TabStripModel` | obs:524-529 → wsc:191-239 (`changed_native_splits_`), wsc:311-411 `CaptureSplits` erzeugt Datensatz (wsc:368-379) | eine Autorität, **außer** während `IsRestoring`/`applying`/`closing_all` (wsc:193-196): dann nicht beobachtet | Rollback-Closure im Drop (sdc:470-483, sdc:516-517) |
| S-b | Split auflösen (Sidebar „Trennen“) | cmd:595-596, cmd:712-717 | `RemoveSplit` | wsc:214-237 setzt Tombstone | eine Autorität | Tombstone nur wenn nicht schon gesetzt (wsc:222-223) |
| S-c | Split umordnen/Layout/Verhältnis | cmd:587-594, cmd:727-735, sdc:485-518, split:84-87, rta:250, tree:375, tree:588 | `TabStripModel` | wsc:214, wsc:383-407 übernimmt nur bei beobachteter Änderung | **Doppelschreiber native ↔ Datensatz**: eine nicht beobachtete Änderung (wsc:193-196) wird von `MaterializeSplits` → nws:186-214 auf den Datensatz zurückgedreht | Adoption nur mit `changed_native_splits_` (wsc:384); `applying` wsc:459 |
| S-d | Split-Fernauflösung (Sync-Tombstone) | extern | wsc:482-499 `RemoveSplit` (nur wenn kein Pane geschützt) | – | eine Autorität | `applying` + Autorität `original.Run()` (wsc:489, wsc:495) |
| S-e | Split-Mitglieder in anderen Workspace verschieben | cmd:403-413 / mv:61-70 (Gruppe aus tree:438-463) | Store | wsc:383-392 `ClassifySplitCapture` → `kWorkspaceOnly` (wsc:65-69) | eine Autorität (S6), **Rest**: Einzel-Pane-Rückfall tree:456-457 | `ClassifySplitCapture` (wsc:53-71) |
| S-f | Split beim Öffnen eines Mitglieds wiederherstellen | tree:226-251 | wsc:413-453 → nws:101-272 (`user_initiated`) | – | eine Autorität (Übergabe 072) | `applying` wsc:420 |

### Quick Window, Popup, Peek, Löschen von Zeilen, Wiederherstellung

| # | Übergang | entscheidet | führt aus | beobachtet | Einstufung | Operationsschutz / Idempotenz |
| --- | --- | --- | --- | --- | --- | --- |
| Q1 | Quick-Window-Übernahme, gemeinsamer Kontext | qw:128-155 (Zielfenster = zuletzt aktives normales Fenster des Profils) | qw:184-196 `DetachTabAtForInsertion`/`InsertDetachedTabAt`, qw:97-124 `ShowAdoptingWindow` | T6/T3: Tab erhält aktiven Workspace des Zielfensters (rt:114) | eine Autorität | **keine**; wiederholter Aufruf verschiebt den dann aktiven Tab |
| Q2 | Quick-Window-Übernahme in Workspace mit eigenen Sitzungen | qw:162-167 | qw:177 `OpenGURL`, qw:178, qw:179-180 `CloseWebContentsAt` | T3 | eine Autorität (Neuladen statt Verschieben, S7) | **keine** |
| P1 | Popup/Peek zum Tab befördern | pop:341-346 (`SelectOpenerWorkspace` pop:72-88) | pop:347-357 `ReleaseForTransfer` + `AddWebContents` | rt:65-138 bindet an den (jetzt Öffner-)Workspace des Fensters (rt:114); Sidebar core:628-646 | eine Autorität (S7) – Bindung indirekt über den Fenster-Workspace, nicht explizit | Eigentumsübergabe: zweiter Aufruf scheitert an `popup_contents()` (pop:342); Ergebnis von `SetActiveWorkspaceForWindow` wird ignoriert (pop:85-86) |
| P2 | Popup mit Öffner teilen (Split) | pop:362-373 | pop:385-396, bei Fehler Rücknahme pop:408-427 | wie P1 und S-a | eine Autorität | Eigentumsübergabe + Rollback |
| P3 | Popup schließen / ausblenden (Dismiss) | pop:267-275, pop:520-550 | pop:552-579 | – | eine Autorität | `dismissal_generation_` (pop:546, pop:553, pop:566) |
| P4 | Popup in eigenes Fenster (Fallback) | pop:291-296, pop:439-455 | pop:461-466 (`NEW_POPUP`) | nicht verfolgt (nur `TYPE_NORMAL`, obs:42) | eine Autorität | Eigentumsübergabe |
| D1 | Temporäre Zeile mit offenem Tab löschen | cmd:739-742, tv:511-518 → tree:682-695 | `tab->Close()` (tree:693), dann T2 | T1/T2 | eine Autorität (S8) | node-ID in `pending_temporary_closes_` (rt:181); kein Operations-ID für den Close selbst |
| D2 | Gespeicherte Zeile mit offenem Tab löschen | cmd:743-744, tv:519 → Store `DeleteNode` | Store | obs:574-587 entbindet (`clear_workspace=false`) → ws:443-445 plant Neubindung → rt:250 neue temporäre Zeile mit neuer ID (ws:437) | **Rückkopplung (gewollt)** | keiner |
| D3 | Zeilen-Aktion „Schließen/Löschen“ | tree:669-680 | `tab->Close()` bzw. `DeleteNode` | wie D1/D2 | eine Autorität | – |
| R1 | Start: Fenster verfolgen | obs:46-92 | obs:68-73 erzwingt ersten Workspace (`kDataReconciliation`) | Sidebar: aufgeschoben (core:461, core:548-551 über core:569-582) | Doppelschreiber, Reihenfolge (s. Befund 5) | wss:112-114 |
| R2 | Start: Tab-/Fenster-Metadaten anwenden | Session-Restore über sri (`RestoreWindowSessionExtraData`, P0064 für wiederverwendetes Fenster) | ses:192-249 (vor Baum: vormerken), ses:251-276 (Tabs vor Fenstern, S4), ses:278-308, ses:310-387 | Sidebar wartet auf `RegisterOnSessionRestoredCallback` (core:575-580) und stimmt einmal ab (core:584-604, `follow_selected_tab=false`) | eine Autorität (S4) für den ausstehenden Pfad; direkter Pfad (Baum schon bereit, ses:219, ses:248) wendet in Chromium-Reihenfolge an – Fenster vor Tabs (*abgeleitet* aus Kommentar in Übergabe 040) | einmaliges `session_restore_notified_` (core:571, core:590) |
| R3 | Start: Splits wiederherstellen | wsc:121-128 (nach Restore), wsc:279-285 | wsc:455-534 | – | eine Autorität | `IsRestoring`-Sperre (wsc:193-194, wsc:276) |

### Weitere Schreiber (gefunden, nicht vertieft)

- Arc-Import: `importer/arc/arc_split_runtime.cc:189`, `:205` rufen `SetActiveWorkspaceForWindow` (eine Autorität, *abgeleitet*).
- Ordner aufdecken: core:254-265 setzt erst den Service, dann nur bei Abweichung das Viewmodel (eine Autorität).
- Initialer Workspace der Sidebar: core:363-375 schreibt den Service nur, wenn keiner gesetzt ist (Doppelschreiber mit obs:433-439, gleiches Ergebnis).

## 2. Status S2–S8 und Befunde 1–5 an `de04e0aa`

| Punkt | Status | Beleg |
| --- | --- | --- |
| **S1** (Kontrolle) | behoben | `8a9fc917` („move follows the service … 011 S1“); cmd:391-395, cmd:422-426, mv:75-79, mv:93-94 |
| **S2** Live-Split archivieren | behoben | `5f1ce2ca` („Keep an archived split's record restorable (crest 034, S2)“), Nachtrag `739cea6b` (Token-Ablauf, 058); arc:195-213, arc:315; wsc:197-212, wsc:271-274. Laut Übergabe 034 auf Build 39 abgenommen. Rest: Token ist zeitbasiert (30 s), keine Operations-ID. |
| **S3** Epoche nur für Struktur | **teilweise** | `aa1058fc`; Klassifikatoren wsc:38-51, Anwendung obs:465-471, obs:556-562, wsc:171-189. **Offen (geprüft):** (a) jede Baumänderung, auch `kRenamed` aus T4, ruft obs:605 `ScheduleTabTreePersistence` → sb:319-321 `CancelPendingSyncedTabTreeApply` → syp:74-79 setzt das Abbruch-Flag des laufenden Strukturcommits (sp:134, sp:143-148) → `kCancelled`. (b) Nach jedem lokalen Baum-Persist erhöht syp:212-227 die Epoche (`OnNativeChanged`, syp:225-227), also auch nach reinen Titeländerungen. (c) Ein abgebrochener Commit lässt `dirty_` gesetzt (zurückgesetzt nur bei Erfolg, wsc:563) und wird über `OnNativeMetadataChanged` → `Schedule()` (wsc:164-168) erneut geplant; das Risiko ist Aushungern bei fortlaufender Aktivität, kein dauerhafter Verlust (Hauptsession-Prüfung). Dass (a)/(b) in der Praxis Archive aushungern, ist *abgeleitet* (Zeitfenster nicht reproduziert). |
| **S4** Wiederherstellung Tab- vor Fenster-Metadaten | behoben (ausstehender Pfad) | `00381bae`; ses:261-275 (Tabs zuerst), core:461, core:548-551, core:569-604. Unverändert: obs:68-73 erzwingt weiter zuerst den ersten Workspace; wirkt dank Aufschub nicht mehr auf die Sidebar. Abnahme laut Übergabe 040 per `restore-surface-journey` auf Build 33 geplant (hier nicht geprüft). |
| **S5** Aktivierung verborgener Tabs | **teilweise** | `6f66a44f`; sb:603-632, cea:134-139, sb:640-642. **Offen:** Sidebar-Discovery „offener Tab“ aktiviert direkt (disc:477-479) und überlässt den Workspace-Wechsel `ReconcileWorkspaceSurface` (core:553-563). Erweiterung `tabs.update` bleibt bewusst externer Pfad. |
| **S6** Split-Datensatz folgt Mitgliedern | **teilweise** | `c64c358c`; wsc:53-71, wsc:383-392. **Offen:** Einzel-Pane-Rückfall bei fehlender Knoten-ID tree:453-458 (von Übergabe 032 bewusst nicht gepatcht; Reproduktion ausstehend). |
| **S7** Popup-/Quick-Window-Übernahme | behoben (mit Einschränkung) | `047d7395`; pop:68-88, pop:346, pop:373; qw:158-181. Einschränkung: Bindung indirekt über Fensterwechsel (rt:114), Rückgabewert ignoriert (pop:85-86); schlägt der Wechsel fehl, landet der Tab im angezeigten Workspace. Quick Window nimmt absichtlich den aktiven Workspace des Zielfensters (qw:164-165). |
| **S8** Temporäre Zeile löschen | behoben | `72109fcc`; tree:682-695, cmd:739-742, tv:515-518. Nicht abgedeckt: Löschen eines Ordners, der temporäre Zeilen mit offenem Tab enthält (D2-Pfad; ob so ein Baum entstehen kann: *abgeleitet*/offen, wie in Übergabe 038 genannt). |
| **Befund 1** Archiv zerstört Split | behoben = S2 | wie S2; `ClosePages()` läuft weiter ohne `applying` (arc:313-316), der Token ersetzt den Schutz. |
| **Befund 2** Fremde Aktivität bricht Commits ab | **teilweise** = S3 | Auswahl (obs:466, wsc:38-40), `kRenamed` (obs:557, wsc:42-46), Ressourcen (wsc:177-188) behoben. Unverändert: syp:225-227 (`OnLocalTabTreePersisted` → Epoche) und neu gefunden der Abbruch über sb:319-321. |
| **Befund 3** Aktiver Workspace mit zwei Schreibern | behoben für das Verschieben (S1); Rest | Verschieben schreibt den Service (W5/W6). Rest (geprüft): `EnsureWorkspaceSurface` liest weiter das Sidebar-Viewmodel (core:535-536), P0054 liest den Service (ses:161-162). Beide stimmen nur überein, solange `ActivateWorkspace` im Viewmodel gelingt; bei Fehlschlag (core:383-386) bleibt das Viewmodel alt (*abgeleitet*, kein Auslöser gefunden). |
| **Befund 4** Löschen hängt Seiten um (010 R6) | behoben | `17f53192`; rm:203-219 (`closing_with_deleted_workspace`, `clear_workspace=closing`), rt:215, obs:410. Offen aus 010: R4 (3-s-Verzögerung rm:151-155; gleiche in mg:160-164). |
| **Befund 5** Wiederherstellung Fenster vor Tab-Metadaten | behoben = S4 | ses:261-275; Sidebar-Aufschub core:569-604. obs:68-73 unverändert. |

## 3. Übergänge ohne prozesslokale Operations-ID für verspäteten/wiederholten Abschluss

Geprüft wurde, ob ein Abschluss (Beförderung, Dismiss, Schließen, Commit) eine eigene,
nur in diesem Prozess gültige ID trägt, gegen die ein verspäteter oder doppelter Abschluss
verworfen wird. Vorhanden sind nur: `dismissal_generation_` (pop:546, pop:553),
`workspace_surface_generation_` (core:548) und die Struktur-Epoche (wsc:132-138, gilt
für ganze Commits, nicht je Operation).

1. **Archivieren mit Schließen (A1/A2):** `done(true)` vor Abschluss von `ClosePage()` (arc:316-319); Split-Schutz über Zeit-Token (wsc:34-36), nicht über Operations-ID; nur ein Archiv gleichzeitig (arc:160).
2. **Workspace löschen mit eigenen Sitzungen (W8):** Abschluss der Seiten wird nicht verfolgt; Partition wird nach festen 3 s geleert (rm:151-155); Kennzeichen je Tab (rm:213), keine ID.
3. **Workspace zusammenführen mit getrenntem Kontext (W10):** wie W8 (mg:147-165, mg:225-229).
4. **Zusammenführen rückgängig (W11):** Store hat Undo-Beleg (`023e6126`), Bridge ordnet ihn keiner Operation zu; Fenster/Routing/ungebundene Tabs werden nicht zurückgeführt (mg:262-273).
5. **Popup/Peek befördern (P1/P2):** Idempotenz nur über Eigentumsübergabe (pop:342, pop:347-351); der vorangehende Workspace-Wechsel (pop:85-86) hat weder ID noch Ergebnisprüfung.
6. **Quick-Window-Übernahme (Q1/Q2):** keine ID; zweiter Aufruf wirkt auf den nächsten aktiven Tab (qw:185, qw:179-180); `PresentProfileWindow(..., DoNothing)` ohne Abschlussbindung (qw:117-118).
7. **Temporäre Zeile löschen (D1):** `tab->Close()` (tree:693) wird bei wiederholtem Auslösen erneut aufgerufen; Dedup erst beim Löschen des Knotens (rt:181).
8. **Gespeicherte Zeile löschen (D2):** Neubindung unter neuer ID (ws:437, ws:443-445) ohne Bezug zur auslösenden Operation.
9. **Strukturcommit (A4):** Abbruch per geteiltem Flag (sp:134) und Epoche; Neuversuch nur beim nächsten Ereignis (`dirty_`, wsc:563, wsc:164-168), ohne Zuordnung, welche Operation den Abbruch verursacht hat, und ohne Fortschrittsgarantie bei Dauertätigkeit.
10. **Split beim Öffnen wiederherstellen (S-f):** synchroner `applying`-Schutz (wsc:420) endet mit dem Aufruf; die nachfolgenden Chromium-Meldungen werden über `native_split_token`/`observed_split` (wsc:444-447) erkannt, nicht über eine ID.
11. **Remote Focus/Close (T8, `CloseNormalTabFromRemoteCommand` sb:652-667):** keine ID; ein wiederholter Remote-Befehl wirkt erneut.
12. **Workspace-Hand-over (W4):** in cmd:249-299 keine ID sichtbar (*abgeleitet*, `RunHandOver` nicht gelesen).
