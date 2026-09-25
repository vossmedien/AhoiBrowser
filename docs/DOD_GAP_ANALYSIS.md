# DoD gap analysis and package plan — 25 September 2026

Source-reading and evidence audit against `outputs/AhoiBrowser-Master-Zielprompt.md`
"Definition of Done" (28 items). This is the working order for the agent-doable
remainder; owner-gated items stay in the desktop checkpoint's owner table.
Update the status column as packages land; never mark an item done without
installed-app evidence where the contract requires it.

Keines der 28 DoD-Items ist mit Evidenz erledigt (DONE-with-evidence). Alle 433 Registry-Fälle stehen auf `NOT_RUN` (`config/test-registry.json`, `config/test-statuses.json`), `config/release-evidence.json` hat `releasePassEnabled:false`.

Installiert ist laut Receipt `8705a7f` (`artifacts/install/installed-ahoi-dev-8705a7f-20260925T154209Z.json`), ein Development-Build mit Xcode 27, nicht release-fähig. Der Checkpoint nennt noch `f91e5b7`, das ist veraltet.

Die meisten sichtbaren M153-Durchläufe liefen auf signierten Kopien des Builds unter `/private/tmp`, nicht auf `/Applications`. Diese Kopien wurden am 25.9. gelöscht, nur die Logs unter `artifacts/` sind geblieben. Alle 15 Journeys unter `artifacts/computer-use/m153/*/verdict.json` stehen auf `pass:false`, meist mit Setup-Fehlern ("delete dialog did not open"). `e760c1f` ist die Korrektur dafür, aber nicht gebaut.

## 1. DoD-Gap-Analyse

Status: **P** = teilweise (PARTIAL), **O** = offen (OPEN), **OG** = beim Owner (OWNER-GATED). Master-Abschnitte in Klammern.

| # | Status | Vorhandene Evidenz / was fehlt | Agent-machbarer Rest (Größe, Master-Abschnitt) |
|---|---|---|---|
| 1 Bootstrap | OG + O | Nur Docs, kein Nachweis auf frischer Maschine; Xcode 26.6 fehlt auf dem Host | Bootstrap-Trockenlauf mit Hydration-/Pin-Receipt vorbereiten, S (Phase 0/1, Bootstrap Gate) |
| 2 Sandbox/Site Isolation | O | Nicht geprüft; SEC-04 ist nur ein Secret-Scan | `chrome://process-internals` und Sandbox-Prüfung auf dem installierten Build, S (Security-Regeln) |
| 3 Patches/Roll | P | M152→M153-Roll `29dfe7a`; 57 Patches bauen EXIT0; Unit-Tests grün (Build 21/25) | Patch-Doku/-Größe prüfen, Roll-Preflight-Receipt für M153, S (Patchstrategie, Upstream) |
| 4 signiert/notarisiert | OG | Nur Apple-Development-Signatur | — |
| 5 Tree/DnD/Split/WS/Null-Tab/Cmd/Quick/Inkognito/Fenster/Restore | P | 2-Pane-Split und Archiv-Restore auf Kopie `b3e18cc`; Quit/Continue auf `79a7752`; 3/4-Pane und 2×2 nur als M152-Browsertests (`f332dd7`); breite Ablagefläche nur in Source `c510bfb`; Command-Bar-Defekt war ein Harness-Artefakt (`6a9cc02`) | Installierte CU-Journeys für TREE/SPLIT/WS/CMD/QUICK/INC, L (Tree, Split, Command Bar, Inkognito) |
| 6 Nav-Zeile/Notch/Glass/Popups | P, rot | Glass nur begrenzt auf Kopie `5ee283a`, der Nutzer bewertet Glass weiter als RED; Ecken-Stufe oben links: Patch `0047`; keine M153-Evidenz für Notch oder Popups | Glass-Korrektur plus Reduce Transparency, Kontrast und Energie-Fallbacks; Notch/Popup-Journeys, M–L (Browser-Chrome, Liquid Glass, Web-Popups) |
| 7 Downloads…Permissions | O | MiniPlayer-Code vorhanden, keine Evidenz | DL/MEDIA/PERM-Journeys mit lokalen HTTPS-Fixtures; Kamera/Mikrofon brauchen TCC-Freigabe, M (Medien, Downloads) |
| 8 Extensions/PW-Manager | P + OG | AnyChat und uBO Classic 1.74.0 (Installation, Filter, Neustart) auf installiertem `ca1e5a7`, aber nur im Isolationsprofil MacA; kein Default-Profil | Web-Store-Standardfall, lokaler Passwortmanager, uBO-Negativ-/Updatefall, bewusste Lite→Classic-Ablösung, M; 1Password/Bitwarden-Vault = OG |
| 9 HTTP Auth | P | 28 Unit-Tests grün; Crashfix `38eebb9` nur mit leerem Profil; Journey-Skript `tools/desktop_e2e/http-auth-journey.sh` vorhanden | Vollständige AUTH-Gruppe (27 Fälle) installiert, M (HTTP-Auth) |
| 10 Dev Toolkit + Privacy-Modi | O | Code in `developer_toolkit/` und `privacy/`; sichtbare Evidenz nur M152 (DevTools-Menü) | DEV (29) und PRIV (18) installiert, L (Developer Toolkit, Privacy) |
| 11 Gesten | O | Code für Workspace-Swipe, ⌘-Scroll und Autoscroll vorhanden, keine Evidenz | NAV-Journeys; physische Magic Mouse braucht Owner-Assistenz, M (Navigation und Gesten) |
| 12 Netzwerk/Telemetrie | O, rot | Idle-Audit PASS nur auf M151; frisches M153-Profil sendet GCM-Anfragen; Patch `0056` gebaut, NET-GCM-01 nicht gelaufen | Fresh-Profile-Audit auf dem installierten Build nach `docs/NETWORK_SILENCE_CHECKLIST.md`, S–M (Fresh-Profile-Netzwerkaudit) |
| 13 Mac–Mac–iOS | OG | Kein Round Trip: Mac wartet auf den iCloud-Keychain-Schlüssel; Mac–Mac nur mit zwei Profilen auf einem Mac (`native-peer-55abcf7`); Merge-Vektoren rev. 3 grün in C++ und Swift | Lokale Voraussetzungen: Tab-Write/Seed-Unit-Test nach CloudKit-Bootstrap-Ack, Recovery-UI „pending“ reparieren, S–M (CloudKit-Sync) |
| 14 keine Secrets im Sync | P | Nur Unit-Tests und redigierter Leak-Scan | Leak-Test über alle Record-Typen inklusive ADR-0011-Zonen, S; Geräte-Nachweis = OG |
| 15 Updates | OG + O | Sparkle 2.9.6 als Code und Doku, keine Evidenz | Lokales Appcast-/Tamper-Testbed mit Testschlüssel, M (Updater) |
| 16 H.264/AAC | OG | — | — |
| 17 Widevine | OG | — | — |
| 18 Performance | P | H3-Lauf auf `f91e5b7`: alle Budgets `INSUFFICIENT`/`NOT_MEASURED`, keine Baseline | Kontroll-Baseline und PERF-03/04 messen, Memory-Saver/Aufwecken-Journey, M (Performance Gate); Upstream-Release-Kontrolle ist OG |
| 19 Daily-Driver-Tag | O | — | Soak auf dem Endkandidaten, L, zuletzt |
| 20 keine P0/P1 | O / OG | Glass und Sync sind rot; kein Bug-Register | P0/P1-Liste führen und abbauen, laufend |
| 21 Reviews | OG | `THIRD_PARTY_REVIEW.md`, `THREAT_MODEL.md` vorhanden | Review-Pakete vorbereiten, S |
| 22 Arc-Import | P | Echter lokaler Arc-Stand importiert, No-op auf installiertem `a24a792` (M152); `artifacts/e2e/0.0.1-dev/IMPORT-ARC-12/RED-20260903.md`; keine M153-Wiederholung, keine redigierte Endevidenz | M153-Wiederholung mit Snapshot, Rollback und Idempotenz, redigierter Bericht, M (Migration aus Arc) |
| 23 Release-Artefakte/SBOM | OG + O | `tools/release/` und Tests vorhanden | SBOM/Checksum-Generierung als lokale Probe, S |
| 24 Lean | O | Messung nur M152; `test_lean_chromium.py` rot seit `75e20c1` | Test reparieren, M153 messen, Null-Aktivitätsnachweise, M (Lean Chromium) |
| 25 Website-Sitzungen | P | Cookie-, LocalStorage-, IndexedDB-, ServiceWorker- und SharedWorker-Trennung auf Kopie `79a7752` hinter Flag; ADR-0011-Stufen implementiert; alle Level-Journeys `pass:false` | Build mit `e760c1f`, WS-ISO/WS-DEL-Journeys installiert, Popup/Transfer/Worker-Restart, Default-Flag-Entscheid, L (Workspace-Sitzung, ADR 0011) |
| 26 Setup-Sync ADR 0010 | P + OG | Adapter vorhanden; Mobile-Settings-Journey nur lokal (DebugLocal 18) | Lokale Restore-Journey Extension + Settings mit zwei Profilen, M; neuer echter Mac = OG |
| 27 Crest-Empfehlungen | P | Routing-Kern `0057` nur mit Unit-Tests, Editor `3b16f31` ungebaut; Portabilität teilweise (Import und No-op `51d7e79`, `455652b`; Auswahl-Commit `NOT_RUN`); iOS-Privatsperre und iOS-Reader im Simulator; Peek (`bfe39d7`) und Kürzel-Katalog/MRU (`5ddb39c`…`f4c3f00`) als Code vorhanden, sichtbar noch nicht abgenommen; Desktop-Reader vorhanden; iOS-Home-Journey rot | WORKFLOW-01…08, siehe Tabelle 2 |
| 28 Auto-Archiv + Split/Archiv-Sync | P + OG | Manueller Archiv-Restore auf `b3e18cc`; Auto-Archiv-Journey `8821ed1` ohne Ergebnis; `SPLIT_ARCHIVE_SYNC.md` | Auto-Archiv-Journey, endgültiges Löschen, Restore ohne Elternordner, M; Desktop-Paar lokal (zwei Profile), M; echte zwei Macs = OG |

## 2. Features außerhalb einer einzelnen DoD-Zeile

| Bereich | Status | Evidenz / Lücke | Rest |
|---|---|---|---|
| Zen-Import | O | Nur Verfügbarkeitsanzeige („nicht installiert“), IMPORT-ZEN 6× `NOT_RUN` | Fixture-basierte Journey, M |
| Import-Zielvorschau (WORKFLOW-06) | P | Portable Vorschau vorhanden; Arc-Hub-Vorschau nicht abgenommen | M |
| Portabler Export | P | siehe 27 | Kollisionswahl, Rollback, installiert, M |
| AnyChat / uBO Classic | P | siehe 8 | Default-Profil, Lite-Ablösung, S–M |
| Split 2×2 persistiert | P | Nur M152-Browsertests | Installierte SPLIT-Journeys (40 Fälle), L |
| Archiv | P | siehe 28 | M |
| Link-Peek (Desktop) | P | Kontextmenü „Link in Vorschau öffnen“ (Patch `0059`, `bfe39d7`), Overlay im WebContents der Opener-Partition, Schließen/Übernahme über den Popup-Lebenszyklus; Journey `link-peek-journey.sh` auf Build 29 eingereiht | Modifier-Klick, Befehl, optionales Auto-Peek aus gespeicherten Seiten, M |
| Tastenkürzel/MRU | P | Katalog `keyboard_shortcuts` mit Konfliktprüfung, BrowserView-Dispatch (Patch `0058`), ⌃` zum zuletzt benutzten Tab im aktiven Workspace, Editor in den Einstellungen, Menü-Anzeige; Journey `keyboard-shortcuts-journey.sh` auf Build 29 eingereiht. `config/shortcuts.json` ist eine ältere Default-Liste (dort ⌘S statt ⇧⌘S für die Seitenleiste) | Quick Window/Undo/Command Bar/Speichern umbelegbar machen, Command-Bar-Einträge aus dem Katalog, Default-Liste abgleichen, M |
| Reader/Markdown (Desktop) | P | Code vorhanden (`362e338`, `69ee9f5`: Chromium-Lesemodus als Overlay, Link/Markdown-Link kopieren, Command-Bar-Einträge); Titel-Bereinigung und Inkognito-Clipboard `WORKFLOW-07`-Nachbesserung | installierte WORKFLOW-07-Journey, S |
| Gespeicherte Ausgangsadresse | P | Desktop nur Unit-Tests; iOS-Journey rot | Desktop-E2E und iOS-Fix, S–M |
| Workspace-Routing | P | Kern `0057` mit Unit-Tests | Editor bauen, WORKFLOW-01, M |
| Quick Window | P | Nur M152; Profil-Fallback bei isolierten Workspaces | QUICK-Journeys, ADR-0011-Schritt 2, M |
| Developer Toolkit | O | siehe 10 | L |
| Privacy-Modi | O | siehe 10 | M |
| HTTP Auth | P | siehe 9 | M |
| Aufgabenhilfe / verständliche Zustände | P | Nur Screenshot `03-settings-help.jpeg` (M152) | S–M |
| Mobile | P | 292 Core-Tests grün; Privatsperre und Reader im Simulator; Layout-Tests nie ganz grün (2 lastabhängige Timeouts); Peek-UI `NOT_RUN`; kein aktueller Build auf echtem Gerät (OG) | Layout-Flakes, Home-Journey, Peek-Journey, Privatsperre mit Passcode/iPad/VoiceOver, M |
| Crash/Recovery (RECOVERY-MAC, CRASH) | O | Keine Evidenz | Kill-/Journal-Journeys, M |
| Lokalisierung, A11y, Pseudoloc | O | UI/A11Y `NOT_RUN` | M |

## 3. Paketplan für den agent-machbaren Rest

„Installiert“ = braucht sichtbare E2E auf dem installierten App-Build. Die Reihenfolge folgt Abhängigkeiten und dem DoD-Nutzen pro Aufwand; Pakete mit gemeinsamem Build-/E2E-Zyklus sind gebündelt.

| # | Paket | DoD | Größe | Installiert |
|---|---|---|---|---|
| 1 | Checkpoint auf den echten installierten Stand `8705a7f` korrigieren; P0/P1-Register anlegen; Registry-Status aus `artifacts/` belegen statt pauschal `NOT_RUN` | 20, alle | S | nein |
| 2 | Build 25: `e760c1f`, Routing-Editor `3b16f31`, Zone `620e5c3`, Review 018 zusammen bauen und installieren | 25, 27 | M | Install |
| 3 | Auf Build 25 alle HID-Journeys wiederholen: Level/Löschen/Isolation/Empty-Workspace/Auto-Archiv/HTTP-Auth | 5, 9, 25, 28 | M | ja |
| 4 | Fresh-Profile-Netzwerkaudit plus NET-GCM-01/02; Sandbox/Site-Isolation-Readback (gleicher Kandidat, dann H3-Leasefenster) | 2, 12 | S | ja |
| 5 | Leistungsbaseline: Kontrollbuild-Äquivalent, PERF-03/04, Memory Saver / Tab Sleeping | 18 | M | ja |
| 6 | `test_lean_chromium.py` reparieren, M153-Bundle-/Runtime-Bilanz messen | 24 | M | nein, Messung am Build |
| 7 | Glass-Korrektur plus Fallbacks, Notch/Auto-Hide, Popup-Overlays als ein UI-Build | 6 | L | ja |
| 8 | Sidebar-DnD (breite Ablagefläche `c510bfb`), Ordneranimation, Workspace-Slide/Swipe/Dots | 5, 11 | M | ja |
| 9 | Split 2/3/4 und 2×2 als volle SPLIT-Matrix mit Restore und mehreren Fenstern | 5 | L | ja |
| 10 | Command Bar, Quick Window (inkl. Profil-Fallback, ADR-0011-Schritt 2), Inkognito | 5, 25 | M | ja |
| 11 | Navigation: ⌘-Scroll, Autoscroll, Swipe-Konfiguration; Magic Mouse mit Owner-Assistenz | 11 | M | ja |
| 12 | Downloads, Uploads, PDF, Druck, Medien, MiniPlayer, PiP, WebRTC, Permissions mit HTTPS-Fixtures | 7 | M | ja |
| 13 | HTTP-Auth-Vollgruppe (mehrere Konten, Update, Wechsel, Abmelden) | 9 | M | ja |
| 14 | Extensions: Web-Store-Standardfall, lokaler Passwortmanager, uBO-Negativ-/Update-/Lite-Ablösung, AnyChat im Default-Profil (mit Nutzerfreigabe) | 8 | M | ja |
| 15 | Auto-Archiv komplett (endgültiges Löschen, Restore ohne Elternordner) plus lokales Desktop-Paar mit zwei Profilen für Split-/Archiv-Sync | 28 | M | ja |
| 16 | Neu implementieren: Desktop-Peek | 27 | L | ja |
| 17 | Neu implementieren: Tastenkürzel-Katalog plus MRU | 27 | L | ja |
| 18 | Desktop-Reader/Markdown-Link abnehmen (Code existiert); Aufgabenhilfe und Zustände | 27 | S–M | ja |
| 19 | Portabilität fertig: Kollisionswahl, Rollback, Auswahl-Commit; Import-Zielvorschau (WORKFLOW-06) | 27 | M | ja |
| 20 | Arc-Import auf M153 wiederholen (Snapshot, Rollback, Idempotenz, redigierter Bericht); Zen-Fixture-Import | 22 | M | ja |
| 21 | Developer Toolkit (DEV, 29 Fälle) | 10 | L | ja |
| 22 | Privacy-Modi (PRIV, 18 Fälle), Safe Browsing | 10 | M | ja |
| 23 | Sync lokal vorbereiten: Bootstrap-Ack-Tests, Recovery-UI, Secret-Leak-Matrix, ADR-0010-Restore mit zwei Profilen | 13, 14, 26 | M | teilweise |
| 24 | Mobile: Layout-Flakes, Home-Journey, Peek-Journey, Privatsperre mit Passcode/iPad/VoiceOver | 27 | M | Simulator |
| 25 | Release-Vorbereitung: lokales Sparkle-Tamper-Testbed, SBOM/Checksums, Bootstrap-Skript-Probe, Crash/Recovery-Journeys; zuletzt Daily-Driver-Soak | 1, 15, 19, 23 | M–L | ja |

Beim Owner bleiben (OWNER-GATED), laut Tabelle oben in `docs/ACTIVE_DESKTOP_CHECKPOINT.md`:
- Developer ID und Notarisierung (DoD 4, 15, 23)
- echte zweite Geräte und der Apple-Schlüssel (13, 26, 28)
- H.264/AAC- und Widevine-Rechte (16, 17)
- formale Reviews (21)
- 1Password-/Bitwarden-Vaults (8)
- die Referenz-Toolchain Xcode 26.6 (1–3)
- Bildschirmaufnahme-Freigabe; bis dahin nur Accessibility- und DevTools-Evidenz, keine Bilder
- jede Veröffentlichung

Die Pakete 16–17 enthalten fehlenden Produktcode (Peek, Kürzel/MRU; der Desktop-Reader existiert bereits), keine bloße Abnahme. Sie bestimmen, wann DoD 27 überhaupt schließbar ist.
