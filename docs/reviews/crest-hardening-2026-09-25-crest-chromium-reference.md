# Crests Chromium-Host als Referenz für Ahoi (H5.1)

Stand: 25. September 2026, Lane `crest-hardening`. Gelesen wurden Crest Branch
`chromium-control-plane` auf `14e7bd6d` (MPL-2.0) und der Ahoi-Stand `f811604`.
Die Chromium-Referenz ist M153 (`153.0.8010.53`). Es handelt sich um eine
reine Quellanalyse: Nichts wurde gebaut oder ausgeführt, und keiner der
Testfälle ist gelaufen. Crest-Pfade sind relativ zu `CrestEngines/Chromium/`;
`host.mm` meint `Overlay/chrome/browser/ui/crest/crest_chrome_host.mm`.
Ahoi-Pfade mit `O/` liegen unter `overlay/chromium/src/ahoi/browser/`.

## Ergebnis

| Thema | Bewertung | Übergabe |
| --- | --- | --- |
| Downloads | gleichwertig (Upstream-Downloadmanager); offen: Downloads aus isolierten Workspace-Partitionen | Testfälle unten |
| Schließen mit Before-Unload | **Lücke** bei Mehrfach-Schließen, Archiv und Popup-Overlays | [006](../../handoffs/crest-hardening/006-batch-before-unload/HANDOFF.md) |
| Workspace-/Profil-Löschung | **ernste Lücke**: Tabs samt isolierter Partition landen im Fallback-Workspace, keine Datenbereinigung | [003](../../handoffs/crest-hardening/003-workspace-deletion-website-sessions/HANDOFF.md) |
| Berechtigungen erreichen offene Seiten | gleichwertig (native `HostContentSettingsMap`); bekannte profilweite Grenze | Testfälle unten |
| Erweiterungs-Seitenleisten in Sonderfenstern | nicht geprüft, wahrscheinlich Bedienlücke im Quick Window | Testfälle unten |

## 1. Downloads

Crest bricht Downloads eines nicht mehr existierenden Space ab. Die Freigabe
einer Warnung prüft Crest gegen das aktuelle Engine-Urteil (`host.mm:2000-2027`,
`2239-2247`). Aktionen am Download-Objekt laufen asynchron, beim Beenden
fragt Crest nach laufenden Downloads (`host.mm:2148-2165`), und das Löschen
eines Eintrags lässt die Datei stehen.

Ahoi verwendet den nativen Downloadmanager, die Bubble und den Beenden-Dialog.
Eigener Code liest den Downloadzustand nur:
- Schlafen blockieren: `O/memory/tab_sleeping.cc:86-100`
- Archivieren: `O/resource_policy/resource_policy_service.cc:179-199`

Neu offen ist das Wiederaufnehmen eines Downloads aus einer isolierten
Workspace-Partition.

Testfälle:
- **DL-ISO-01:** In einem Workspace mit eigenen Website-Sitzungen einen
  cookiegeschützten Download starten, unterbrechen, neu starten und
  fortsetzen. Er wird mit dem Cookie dieses Workspaces fortgesetzt; kein 401
  aus der Standardpartition.
- **DL-ISO-02:** Einen Tab mit laufendem Download schlafen legen oder
  archivieren. Beides wird abgelehnt.
- **DL-ISO-03:** Beenden mit laufendem Download zeigt Chromiums Warnung;
  „Behalten“ schließt nichts.

## 2. Schließen mit Before-Unload

Crest prüft eine ganze Gruppe von Seiten, bevor es eine schließt
(`Apple/ChromiumPageClosePreparer.swift:8-14`, `host.mm:2070-2112`). Ein Veto
einer Seite lässt alle übrigen offen.

In Ahoi läuft jedes einzelne Schließen über `TabStripModel` und damit über
Before-Unload. Drei Pfade tun das nicht als Gruppe:
- **Archiv:** Es committet zuerst und schließt dann einzeln mit `CLOSE_NONE`
  (`O/session/workspace_structure_controller_archive.cc:233-271`). Geschützt
  ist es nur durch die Vorprüfung `CanArchiveTab`.
- **„Alle temporären Tabs schließen“:** Es schließt einzeln
  (`O/ui/sidebar/browser_sidebar_host_runtime_actions.cc:645-669`).
- **Popup-Overlays:** Sie liegen nicht im Tabstrip. Beim Abbau des Dienstes
  (`O/popup/popup_overlay_service.cc:20`) wird die Seite ohne Before-Unload
  verworfen. Das ist aus dem Code abgeleitet, nicht beobachtet.

Details und Tests: Übergabe 006.

## 3. Workspace-Löschung und Website-Daten

Crest löscht den Profilspeicher in fester Reihenfolge und crash-sicher
(`Apple/ChromiumProfileRemover.swift`, `host.mm:686-730`, `3189-3230`).

In Ahoi setzt `SessionBridge::DeleteWorkspace`
(`O/session/session_bridge_workspace.cc:206-251`) für offene Tabs nur
`runtime.workspace_id = fallback->id`. Ihre WebContents bleiben in der
Partition des gelöschten Workspaces. Die Bindung in
`ahoi.session.website_session_bindings` und die Partitionsdaten werden nie
entfernt. Das widerspricht `docs/WORKSPACE_SESSIONS.md` („never switch the
local account of an already-open page“). Details und Tests: Übergabe 003.
Für die Stufe „Vollständig getrennt“ regelt ADR 0011 den Löschpfad über
Chromiums Profil-Löschung.

## 4. Berechtigungen erreichen offene Seiten

Crest hat seine Berechtigungsdaten in Chromiums `HostContentSettingsMap`
verlegt (`host.mm:2652-2681`). Ahoi nutzt dieselbe native Autorität:
- Page Info, Einstellungen und das Developer Toolkit schreiben dorthin
  (`O/developer_toolkit/developer_toolkit_chromium_adapters.cc:326-364`).
- Der Privacy-Modus verteilt über alle geladenen Partitionen
  (`patches/chromium/0001…`).

Die bekannte Grenze: Berechtigungen gelten profilweit, nicht pro Workspace
(`WORKSPACE_SESSIONS.md`).

Testfälle:
- **PERM-LIVE-01:** Kamera läuft; in Page Info auf Blockieren stellen. Der
  Track endet ohne Neuladen.
- **PERM-LIVE-02:** Derselbe Origin ist in Workspace A und B offen,
  Standort ist erlaubt. In A widerrufen und das Verhalten in B protokollieren;
  die Produktentscheidung steht in ADR 0011 unter „Open for the user“.
- **PERM-LIVE-03:** Strengen Privacy-Modus für einen Origin setzen, während
  zwei Hintergrundtabs offen sind. Drittanbieter-Cookies werden ab der
  nächsten Anfrage blockiert.

## 5. Erweiterungs-Seitenleisten in Sonderfenstern

Crest zeigt in Fenstern ohne Kartenleiste den Hinweis „Side panels open in
the main window“ (`Apple/CrestChromiumRoot.swift:783-800`).

Ahois Quick Window ist ein `TYPE_POPUP`-Browser
(`O/command_bar/quick_window_chromium.cc:41-51`). Upstream legt auch dort einen
Side-Panel-Koordinator an. Eine Seitenleiste öffnet sich deshalb vermutlich
im kleinen Quick Window. Ahoi behandelt das nicht eigens.

Testfälle:
- **SP-QW-01:** Im Quick Window AnyChat öffnen oder `sidePanel.open`
  auslösen. Die Seitenleiste ist bedienbar oder ein klarer Hinweis
  erscheint; kein Absturz, keine unsichtbare Leiste.
- **SP-QW-02:** Den Quick-Window-Tab ins Hauptfenster übernehmen. Der
  Seitenleistenzustand der Erweiterung folgt dem Tab.
- **SP-QW-03:** In einem Split eine Seitenleiste pro Tab für das rechte Pane
  öffnen. Sie gehört zum aktiven Pane.

## Übertragbare Ideen (nur Konzept, MPL-2.0 dateibezogen)

- Gruppenprüfung vor Mehrfach-Schließen mit Revisionsschutz.
- Feste, persistierte Löschreihenfolge mit Wiederaufnahme beim Start.
- Fester Hinweis für Seitenleisten aus Popup-Fenstern.
