# Crest-Findings in Ahoi integrieren

Der Nutzer hat am 6. Oktober 2026 den zuvor vorgelegten Übernahmeplan zur Umsetzung freigegeben und ausdrücklich Subsessions verlangt. Dieser Bericht enthält den Vertrag und die Übergaben dieser Ergänzung; der [Desktop-Checkpoint](../ACTIVE_DESKTOP_CHECKPOINT.md) bleibt für den gemeinsamen Kandidaten, Builds und Auslieferung maßgeblich.

## Ausgangspunkt und Grenzen

- Ahoi-Source-Basis: `c75bc93cd5c576b65c8097b162ae048122e9e0c9`. Eigener Integrationsbranch: `cockpit/crest-integration-20261006`. Der abgeschlossene Recherchebranch und Commit `f0b60753` bleiben erhalten.
- Zuletzt belegter ausgelieferter Ahoi-Kandidat laut Desktop-Checkpoint: `ef982afe`, Chromium 155.0.8059.26. Neuere Source-Commits sind noch keine sichtbare Abnahme dieser Ergänzung.
- Aktuelle Crest-Referenz: Development 0.7.11, Build 1184, Commit `4e59eafd`, veröffentlicht 06.10.2026 um 03:42:40 UTC. Quellen: [Release](https://github.com/pauljoda/Crest/releases/tag/development-0.7.11-2026-10-06-1184-r184.1), [Quellenrecherche](2026-10-06-crest-reddit-feedback.md), [Repository-Abgleich](2026-10-06-crest-adoption.md). Die historischen 0.6-Einstellungen und damals geplanten Funktionen belegen keine aktuelle Runtime-Umsetzung.
- Bestehende Chromium-Tabs, WebContents, Profile, Permissions, Dienste und Ahoi-Baum-/Sync-Identitäten bleiben zuständig. Kein zweiter Browser-/Theme-/Sync-Store, keine Chrome-Sync-Aktivierung und keine neuen Privacy-Defaults.
- Build, Installation, sichtbare E2E und Defaultbranch-Integration werden über den bestehenden Desktop-Owner koordiniert. Diese Session und ihre Source-Worker bedienen den Build-Mac nicht und starten dort keine Builds, Simulatoren oder Installationen.
- Keine fremden Artefakte verändern, keine Profile/CloudKit-Daten zurücksetzen, kein `git add -A`. Commits mit DCO und `Lane: desktop`.

## Pakete und Abnahme

| Paket | Ergebnis | Owner und Stand | Abschlusskriterien |
| --- | --- | --- | --- |
| 1 · Rückmeldungen und Tab-Organisation | Toasts, Mehrfachauswahl, visueller Tab-Switcher | Bestehender Desktop-Owner Claude. Toasts `72e2335f`, Auswahl `e633fe07`, Abbruchkorrektur `cb57ce54`, Auswahlzeichnung `c75bc93c`; Source vorhanden, neue Runtime-Abnahme offen. Switcher-Stand angefragt. | Bestätigung erst nach erfolgreicher Aktion; Auswahl bleibt nach Abbruch/Fehler erhalten. Tastatur/VoiceOver, Splits, BeforeUnload, Undo und Neustart am gemeinsamen Kandidaten. |
| 2 · Kleine Alltagshilfen | Ordnerinhalt im Hover, verständliche Navigation, Download-Feedback, lokale Command-Bar-Ergänzung | Root. Drei Ordner-Hover-Dateien zur Freigabe angefragt; gemeinsame Wiring-Dateien bleiben beim Desktop-Owner. | Auch unbesuchte gespeicherte Seiten sind erreichbar; scrollbar und mit vorhandener Identität öffnen. Always-show-Navigation ist bereits ein vorhandener Auto-Hide-Schalter und wird nicht dupliziert. Zusätzliche Source-Arbeit nur für belegte Lücken. |
| 3 · Extension-Tabgruppen | Native Gruppen als benannte/farbige Open-Ordner, bidirektional und nach Neustart | Neuer Worker `3e38308c`. Gemeinsame Host-/Session-/BUILD-Edits zunächst als Patch-Handoff. | Keine Observer-Schleifen/Duplikate, keine Reloads oder Kontextwechsel zum Gruppieren; Ungroup löscht keine Saved-Seiten. Flache Native Groups eindeutig gegenüber verschachtelten Ahoi-Ordnern abbilden. |
| 4 · Panels und Vollbild | Extension-Panels, Popups und Site Controls in minimalem Chrome/Vollbild | Root nach Source-Handback; vorhandene native Funktionen zuerst am Kandidaten prüfen. | Kontext, Berechtigungen, Panel-Auswahl und Vollbild-Zustand korrekt; nur bestätigte Ahoi-Lücken ändern. |
| 5 · Medien | Optionales automatisches PiP über native Chromium-Dienste | Root: [Source-Handoff](2026-10-06-crest-auto-pip-handoff.md) aktiviert die vorhandene browserinitiierte M155-Fähigkeit; native Ask/Allow/Block bleibt zuständig. WebUI-Entdeckbarkeit nach Dichte-Handoff, Runtime-Abnahme offen. | Tab-/Workspace-/Split-Wechsel, Rückkehr und manuelles Schließen; Zustimmung und Nutzereinstellungen erhalten. Kein eigenes PiP-WebContents oder Medien-Backend. |
| 6 · Darstellung und Aufgabenhilfe | Drei Tab-Dichten, Live-Änderung/Reset; freiwillige kurze Übungen | Dichte: neuer Worker `2bd8907a`. Aufgabenhilfe: Root nach Settings-Handback. | Bisheriger Standard bleibt Standard; Deutsch/Englisch, Tastatur/AX und Hit-Targets. Übungen verändern ausschließlich ihren eigenen Beispiel-Workspace; Reset berührt keine fremden Daten. |
| 7 · Workspace-Verständlichkeit | Teilen/Trennen von Logins und Extensions im Dialog klar erklären | Root nach Dialog-Handback. | Erklärung entspricht Ahois vorhandenen drei Isolationsstufen. Keine neue Subspace-/Profilarchitektur. |
| 8 · Performance und Robustheit | Nur belegte Restlücken aus aktuellen/älteren Findings schließen | Root koordiniert mit Desktop-/Sync-/Mobile-Owner. | Vorhandene Source-Fixes und Abnahmen konsumieren. Gleiche Tabs/Baum/Kandidaten für Messungen; betroffene sichtbare Reise und gezielte Regression nach einer echten Korrektur. |

Stock-Funktionen erhalten gezielte Runtime-Prüfung im gemeinsamen Kandidaten: Vollbild-/Pointer-Lock-Hinweise, Link-Zielanzeige, Retina-Favicons, Extension-Installation/Anmeldung und Bildschirmteilen. Eine bestätigte Lücke gehört in das jeweilige Paket. Frühere CloudKit-/Mobile-Findings werden gegen den aktuellen Source-/Abnahmestand geprüft, bevor weitere Änderungen entstehen.

Wappenbaukasten und zusätzliche rein dekorative Einstellungen sind zurückgestellt. Fokussierte Fenstertransparenz wird nicht aus der alten 0.6-Liste übernommen: Crest entfernte sie in [Stable 0.7.2](https://github.com/pauljoda/Crest/releases/tag/v0.7.2) wegen WindowServer-Last.

## Delegationen und Zuständigkeit

| Auftrag | Beobachteter Start | Schreibgrenze |
| --- | --- | --- |
| `2bd8907a` · Dichte | Session `FE65AC60-B06B-4696-86AA-E4DD823BF36F`, neuer Thread `01a110d0-ddd4-7531-90ef-6631630b7324`, GPT-6.1-Sol/high; eigener Worktree `AhoiBrowser-crest-reddit-recherche-9a68d1f3-codex-2bd8907a`, Branch `cockpit/codex-2bd8907a`. Eigenes Teilgoal ist in der Sessionliste sichtbar. | Neue `ui/appearance/sidebar_density*`-Dateien und `2026-10-06-crest-density-handoff.{md,patch}`; bestehende Pref-/Views-/Settings-/BUILD-Dateien nur als minimaler Wiring-Diff bis Handback. |
| `3e38308c` · Tabgruppen | Session `390F31CB-E48E-445E-AFD4-1BC0FD3867AB`, neuer Thread/Goal `01a110d1-573a-7963-96c7-5defc6533dbd`, GPT-6.1-Sol/high; eigener Worktree `AhoiBrowser-crest-reddit-recherche-9a68d1f3-codex-3e38308c`, Branch `cockpit/codex-3e38308c`. Eigenes neues Goal vom Worker bestätigt. | Neue `extensions/tab_group_sidebar_adapter*`-Dateien und `2026-10-06-crest-tabgroups-handoff.{md,patch}`; gemeinsame Host-/Session-/BUILD-Dateien nur als Wiring-Diff bis Handback. |

Beide Worker haben einen eigenen Teilauftrag mit eigenem Goal, keinen Auftrag für fremde Build-/Release-Ressourcen. Ihre Source-Rückgabe wird vom Root geprüft und in den gemeinsamen Kandidaten integriert; ein Workerabschluss ersetzt keine Runtime-Abnahme. Der bestehende Claude-Owner ist Session `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread `049a3c1d-5edf-4d2e-a173-c89f7f9e0663`, Canonical-Branch `codex/desktop-core-feature-wave-20260830`.

Direkte Orchestrator-Informationsanfrage `e72ba675-80db-46eb-bda2-c3ca1eb03d4f` scheiterte mit „Die Orchestrierung ist derzeit nicht verbunden“. Der separate Delegationsweg hat beide neuen Worker tatsächlich gestartet. Es wurden keine Ersatzworker oder fremden Goals angelegt. Modell/Effort sind beobachtet; ein gesonderter Jev-Entscheidungsbeleg liegt bisher nicht vor.

Claude-Handback konsumiert: Dichte-Worker darf `appearance_prefs.*` und Settings-WebUI direkt ändern. `appearance_views.*`, sämtliche `ui/sidebar/*`, Toasts, Navigation Surface, Shortcuts und `ui/tab_switcher/` bleiben bei Claude; `visual_style.h` und gemeinsame BUILD-/Host-Änderungen kommen als Handoff. Root ändert keine dieser reservierten Dateien. Claude baut Kandidat `c75bc93c` (Toasts, Auswahl, Notch, Glas-Clip) und nimmt anschließend die betreffenden Reisen ab. Tab-Switcher ist noch nicht begonnen und bleibt bei Claude. Root/Worker liefern Source-Commits für den nächsten gemeinsamen Kandidaten; Claude cherry-pickt, baut und prüft sie.

## Nächste Schritte

1. Worker erstellen vollständige Source-Pakete mit minimalem tatsächlichem Wiring; Root prüft währenddessen vorhandene Navigation/Ordner-/Medienpfade und bereitet die eigenen Ergänzungen vor.
2. Dateihandbacks des aktiven Desktop-Owners konsumieren; Source-Pakete integrieren, eigene Diffs und erforderliche Regressionen prüfen. Kein zweiter Overlay-/Buildstand.
3. Gemeinsamen Kandidaten über den bestehenden guarded Buildpfad bauen, installieren, betroffene sichtbare E2E-Reisen und danach notwendige fokussierte Tests ausführen lassen.
4. Erst nach belegter Abnahme integrierten/gepushten Commit, ausgeliefertes Artefakt/Revision und genaue verbleibende Gates im Desktop-Checkpoint festhalten. Kein Paket allein aufgrund eines Source-Commits als ausgeliefert markieren.
