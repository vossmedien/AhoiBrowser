# Erweiterungen und Seitenpanels im aktiven Workspace

Source-Handoff für Paket 4 des [Integrationsplans](2026-10-06-crest-integration-plan.md). Der [Patch](2026-10-06-crest-panels-handoff.patch) ist vorbereitet und auf Source-Anwendbarkeit geprüft. Build, sichtbare Browserabnahme, native Regressionen, separate native Review und Auslieferung sind offen. Gemeinsame Produktdateien werden ausschließlich vom Desktop-Owner integriert.

Die Delegation `45479041` meldete Zeitüberschreitung. Ihr Worktree lag sauber und unverändert auf `5688cab2`; weder Ergebnisdatei noch eigener Goalstart waren belegt. Root übernahm die begrenzte Recherche und diese beiden Handoff-Dateien selbst, ohne Ersatzworker oder Zugriff auf den Build-/GUI-Slot. Die Ownership-Notizen `03a96333` und `a0f60bb9` sind beim Cockpit angenommen. Die fortsetzende Desktop-Session ist inzwischen als Codex auf Caeli gebunden: Session `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread/Goal `01a11179-cb6b-7dc1-9e53-f3d72fefc198`.

## Aktuelle Referenz und vorhandene Funktionen

Am 6. Oktober 2026 anhand der offiziellen GitHub-Release-API erneut geprüft: jüngster veröffentlichter Crest-Build ist [Development 0.7.11 / 1184 / r184.1](https://github.com/pauljoda/Crest/releases/tag/development-0.7.11-2026-10-06-1184-r184.1), Source `4e59eafd9c21c765a19e28d27dd2d145b24ab260`, veröffentlicht um 03:42:40 UTC. Seine Releasebeschreibung nennt die Korrektur, dass Site Controls und aufgelistete Erweiterungen auf macOS auch im Vollbild öffnen. Eine Installation oder Runtime-Abnahme von Crest wurde hier nicht durchgeführt.

Ahoi verwendet Chromium `155.0.8059.26`, Pin `16c3e55476d3564bea713314b2fff638749ce3e6`. Die tatsächlichen Caller wurden im Source dieses Pins und in Ahois vorhandener Patchfolge verfolgt:

- MV3 `sidePanel.open()` und `setOptions()` verwenden Chromiums vorhandenen SidePanelService, registrierte Entries, ExtensionViewHostFactory und Profile. Die API unterscheidet globale Panels und tabbezogene Optionen; User Gesture, Tab-/Fensterzugehörigkeit und Incognito bleiben native Prüfungen. Siehe [API-Vertrag](https://developer.chrome.com/docs/extensions/reference/api/sidePanel), [SidePanelService am Pin](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/extensions/api/side_panel/side_panel_service.cc) und [Host-Factory](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/extensions/extension_view_host_factory.cc).
- `startup_policy.cc` deaktiviert das AI-Feature `ContextualTasksSidePanel`, nicht generische Extension-Seitenpanels. Ein weiterer Feature-Schalter ist für dieses Paket nicht erforderlich.
- Ahois Navigation Surface hält Toolbar-Bubbles, Fokus und modale Widgets bereits sichtbar; native macOS-Immersive-Pfade platzieren Toolbar-Bubbles im richtigen Overlay und halten dessen Reveal-Lock. Die vorhandenen Enter-/Exit-Seams stellen den Navigation-Layer wieder her. Website-Popup-Fullscreen läuft bereits über PopupOverlayController und den nativen Fallback. Die Crest-Vollbildkorrektur ist daher eine gezielte Abnahmefrage am Ahoi-Kandidaten, bislang kein belegter Bedarf für einen zweiten Vollbildmechanismus.
- Laut [ADR 0011](../decisions/0011-optional-isolated-workspace-profiles.md) teilen Workspaces derselben Profilebene Extensions, Extension-Cookie-API und Berechtigungen. Eigene Website-Sessions trennen Website-Daten; vollständige Extension-Trennung verlangt ein eigenes Profil. Die historische Crest-0.6-Auswahl eines Panels pro Space wird nicht als vorhandene Chromium-Workspace-Pref ausgegeben.

## Belegte Ahoi-Lücke und Korrektur

`ActivateWorkspaceRuntimeTab` in `browser_sidebar_host_core.cc` blendet bei einem leeren Workspace Ahois leere Oberfläche ein. Der native TabStripModel kann dabei seinen bisherigen ActiveTab A behalten. Die Extension-Action-, Menü-, Kontextmenü- und Toolbar-Getter sowie der Kontextpanel-Getter lesen diesen nativen Tab ohne Prüfung des ausgewählten Ahoi-Workspace. Daraus folgt im Source: Ein leeres Workspace B kann UI-Kontext von A verwenden. Dies ist ein nachverfolgter Source-Befund; eine sichtbare Reproduktion ist noch offen.

Die Korrektur verwendet ausschließlich die vorhandene Funktion `ahoi::session::IsCurrentTabInActiveWorkspaceForNavigation`. Ihr registrierter SessionBridge-Provider kennt Fenster, Profil und aktive Workspace-Zuordnung. Nur das ausdrückliche Ergebnis `false` schließt den Tab aus; `nullopt` erhält das native Verhalten für Browser ohne zuständigen Ahoi-Provider. Explizite Delegate-WebContents außerhalb eines Tabs behalten ihren nativen Kontext.

Der Patch bewirkt:

1. Die gemeinsamen Extension-UI-Getter liefern keinen Tab aus einem anderen Workspace. Seitenbezogene Actions ohne sichtbaren Tab werden deaktiviert und lösen keine neue ActiveTab-Freigabe aus. Bestehende native Extension-/Profil-Dienste bleiben zuständig.
2. Beim Wechsel zur leeren Oberfläche gleicht BrowserView die kontextbezogenen Panels über `SidePanelUIBase::OnActiveTabChanged` ab. Chromium speichert und restauriert deren Views über seine eigenen Registries. Globale Fenster-Panels bleiben gemäß ihrer nativen Zuordnung verfügbar. Native TabStrip-Ereignisse erhalten einen Marker, damit dieser Abgleich nicht doppelt ausgelöst wird; es werden keine künstlichen TabStrip-Ereignisse ausgesendet.
3. Offene Extension-Popups und das Extension-Menü schließen beim Wechsel zur leeren Oberfläche. Die Toolbar entfernt alte Zugriffsanzeigen und Bestätigungen. Auch verzögerte Zugriffsanzeigen eines inzwischen versteckten Tabs werden abgefangen.
4. `PopulateSidePanel` prüft vor dem Einsetzen einer verspätet geladenen kontextbezogenen View erneut deren Tab und Workspace. Ein zuvor in A gestarteter Ladevorgang zeigt sein Panel nicht nachträglich im leeren B.

Es gibt keinen neuen Workspace-/Panel-Store, keine neue Extension-Engine, keinen Partitionstausch, keine zusätzliche Profilklasse und keine Änderung an Website- oder Extension-Berechtigungsentscheidungen. In Splits bleibt der bestehende aktive native Tab der UI-Kontext; der Patch definiert keine neue Auswahlregel zwischen Split-Panes.

## Integration

Der äußere Handoff umfasst genau fünf Produktpfade: neue native Patchdatei `0097-ahoi-active-workspace-extension-ui.patch`, `series`, kleine bestehende BrowserView-Seams in `0001-ahoi-m153-integration-seams.patch`, einen neuen Browserregressions-Source und dessen Eintrag in der vorhandenen Shell-`BUILD.gn`. Die neue native Patchdatei betrifft sieben C++- und fünf GN-Dateien. GN bindet den vorhandenen Session-Guard nur an die fünf tatsächlichen nativen Caller-Targets; gemeinsame UI-Models schützen diese Desktop-Abhängigkeit mit `!is_android`.

Basis ist der Desktop-Pfad mit bereits vorhandenem Patch `0096` für den Tab-Switcher. Der ältere Root-Sourcebranch auf `c75bc93c` enthält diesen Patch noch nicht; dort ist das unmittelbare Anwenden des Handoffs erwartungsgemäß nicht möglich. Beim Owner erst die Korrektur und Abnahme des vorhandenen Kandidaten abschließen, dann die bereits gelieferten Source-Commits und Handoffs in der dokumentierten Reihenfolge übernehmen. Panels anschließend anwenden:

```sh
git apply --check docs/reviews/2026-10-06-crest-panels-handoff.patch
git apply --check --whitespace=error-all --include='overlay/*' docs/reviews/2026-10-06-crest-panels-handoff.patch
git apply --whitespace=nowarn docs/reviews/2026-10-06-crest-panels-handoff.patch
```

Der äußere Patch enthält einen inneren Unified Diff. Dessen notwendigen Leerzeilen-Kontext mit dem Präfix ` ` als Source-Trailing-Whitespace zu prüfen war ein Fehler der ersten Prüfmethode. Deshalb äußerer Anwendbarkeitscheck separat, strenger Whitespace-Check auf den eigentlichen Overlay-Source sowie erneut auf die erzeugte native Patchfolge beim Source-Apply. `--whitespace=nowarn` beim äußeren Container verändert keine Zeile; innere Patches bleiben unverändert und werden streng geprüft.

## Erhobene Source-Evidenz und Grenzen

- Äußerer `git apply --check` am tatsächlich vorhandenen canonical Source `151004282265b5d753a95ed3daa483017c8d6e09`: PASS. Das ist der Desktop-Verlauf mit Korrektur `8681500b`, Output-Prerequisite `47885f36` und Checkpoint-Commit; es ist kein neuer Runtime-Pass.
- Äußerer Patch auf einer isolierten Kopie der fünf betroffenen Dateien angewendet; Overlay-Whitespace: PASS. Keine reservierte Produktdatei im echten Worktree geändert.
- 14 betroffene native Source-Dateien aus dem exakten offiziellen M155-Pin aufgebaut und sämtliche 18 für diese Pfade relevanten Patches der vorhandenen Serie einschließlich geänderter `0001` und neuer `0097` der Reihe nach angewendet: PASS, mit `--whitespace=error-all`. Dies prüft die Anwendbarkeit dieser Pfade, nicht den gesamten Chromium-Build.
- Beide erzeugten inneren Patches mit `git apply --numstat` syntaktisch geparst: PASS. Tatsächliche Target-Definitionen und Test-API-Signaturen am Pin nachgelesen; kein GN-Generator- oder Link-Pass.
- Eigener Test-Source: 137 physische Zeilen; betroffene Ahoi-`BUILD.gn`: 148. Der vorhandene Rahmen von 800 Zeilen wird eingehalten.
- Zwei fokussierte native Browserfälle liegen im vorhandenen Target `ahoi_floating_browser_view_browsertests`: `ExtensionWorkspaceScopeBrowserTest.EmptyWorkspaceDoesNotExposePreviousTabContext` und `.LatePanelLoadDoesNotExposeHiddenWorkspace`, beide **NOT_RUN**. Sie benutzen echte Workspace-Aktivierung, native Toolbar, Panel-Registry, Panel-Controller und verzögerte Verfügbarkeit. Die registrierte View ist ein kleiner nativer Testinhalt, kein MV3-Extension-Host; Extension-Berechtigungen und Vollbild werden damit nicht belegt.

## Offene Abnahme beim Desktop-Owner

1. Guarded gemeinsamen Kandidaten bauen. Mit einer echten MV3-Erweiterung Action-Popup und ein tabbezogenes sowie ein globales Panel öffnen. A → leeres B → A: Menü/Tooltip/AX-Zugriffsanzeige und Kontextpanel von A verschwinden in B, Rückkehr verwendet den nativen Cache und dieselben WebContents. Panel-Laden vor dem Wechsel verzögern; keine nachträgliche View aus A in B. Auch einen Wechsel zwischen zwei befüllten Workspaces und den aktiven Kontext in einem Split prüfen. Keine neue ActiveTab-Freigabe beim leeren Kontext.
2. Zweites Fenster desselben Profils und ein getrenntes Profil einbeziehen: native Tab-/Fensteroptionen, User-Gesture-Ablehnung, Incognito-Zulassung und bestehende Berechtigungen erhalten. Eigene Website-Sessions dürfen nicht als eigene Extension-Cookie-Berechtigung erscheinen. Ein konkreter Fehler an diesen Grenzen rechtfertigt die passende zusätzliche Regression; keine pauschale neue Matrix.
3. Im macOS-Vollbild Site Controls, Extension-Menü, Action-Popup und Panel tatsächlich bedienen: Klick/Fokus/Escape, Cursor vom Toolbar-Anker weg und über die vorhandene Auto-Hide-Verzögerung hinaus; das bediente Widget bleibt erreichbar, anschließend funktioniert Auto-Hide wieder. Vollbild verlassen und erneut betreten. Bestehende Website-Popup-Navigation und WebContents-Zuordnung erhalten.
4. Nach sichtbarer Abnahme die beiden fokussierten nativen Browserfälle am exakt gleichen Kandidaten ausführen. Quelltests sind kein Ersatz für den echten Extension-Host.
5. Danach vor Defaultbranch-Integration/Release genau eine separate native `codex review` des kleinsten attributierbaren Implementierungsumfangs gemäß der neuen AGENTS-Regel durchführen. Bestehendes Konto, `CODEX_HOME`, Modellrouting und YOLO-Einstellung beibehalten. Findings am Code validieren, nötige Korrekturen und betroffene Reisen wiederholen; keine rekursive oder pauschale weitere Review. Diese Source-Recherche ist keine native Review.
6. Geprüfte Revision, tatsächliche Reise-/Test-/Review-Ergebnisse und Grenzen im bestehenden Desktop-Checkpoint festhalten, verifiziert integrieren/pushen und über den bestehenden guarded Installer ausliefern. Push-Zugang ist laut aktuellem Desktop-Checkpoint noch offen. Root hat keine eigenen Inhouse-Prozesse oder Container gestartet; fremde Kandidaten, Builds und GUI-Journeys bleiben beim Owner.
