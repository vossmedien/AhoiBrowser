# Crest-Chromium-Build: Faktencheck und Übernahmekandidaten für Ahoi

Stand 28.09.2026. Nur Quellanalyse, nichts gebaut oder ausgeführt. Gelesen:
Crest Branch `chromium-control-plane` @ `097e7c56` (flacher Klon, 300 Commits),
Releases über `gh`, Ahoi-Arbeitsbaum `a48d1c89`. Die Vorgängerreview
`docs/reviews/crest-hardening-2026-09-25-crest-chromium-reference.md` (Crest @ `14e7bd6d`)
ist bekannt. Seitdem sind 289 Commits dazugekommen. Relevant sind vor allem
`097e7c56` (Fork-/CI-Verwaltung) sowie die Engine-Commits vom 25. bis 27.09.

## 1. Stimmt es? Ja, aber experimentell

- **Ein Chromium-Build existiert.** Die Pre-Releases `experimental-0.6.x-chromium-control-plane-*`
  gibt es seit 23.09. (zuletzt Build 1143 vom 28.09.). Dazu kommen getrennte Engine-Releases
  `chromium-engine-<key>`, jeweils eine arm64-ZIP mit sha256, laut Beschreibung
  „not an installable browser“. Stable- und Dev-Kanal liefern weiterhin nur WebKit.
- **Die Basis ist ungoogled-chromium-macos `152.0.7977.82-1.1`**
  (`CrestEngines/Chromium/source.lock.json`: lite-Tarball mit sha256 und rund 100
  ungoogled-Patches mit Hash). Ahoi liegt mit `153.0.8010.53` direkt auf Google Stable
  und ist damit eine Milestone weiter. Crest folgt bewusst ungoogleds Releasetakt
  (`ForkMaintenance.md`: „not the first Google Chrome announcement“).
- **Die Architektur ist grundlegend anders.** Chromium ist bei Crest eine *Engine* hinter einer
  Swift-Oberfläche und einem .NET-Core (`CrestCore/`, C-ABI in `CrestContracts/`).
  `Patches/native-host.patch` (2058 Zeilen, ca. 73 Upstream-Dateien) ersetzt über
  `--crest-control-plane` das BrowserWindow (abgeleitet von Mori, MIT). Das Overlay
  `Overlay/chrome/browser/ui/crest/` umfasst 37 Dateien und rund 11k Zeilen. Dazu kommen drei
  kleine Toolchain-Patches. Sidebar, Spaces, Befehlsleiste, Split und Sync liegen
  vollständig in Swift/C# und nicht in Chromium.
  Zum Vergleich Ahoi: 70 Patches, 31k Zeilen, 346 Upstream-Dateien, dazu 950 Overlay-Dateien.
  Der kleinere Chromium-Fußabdruck von Crest folgt aus dieser Architektur. Er lässt sich
  nicht übernehmen.
- **Lizenz:** MPL-2.0 ohne Exhibit B (nur im LICENSE-Text selbst gefunden). Damit ist eine
  Kombination mit GPL-3.0-or-later als „Secondary License“ möglich. Die Copyleft-Pflicht gilt
  dateibezogen. Mori-Teile stehen unter MIT, ungoogled-Patches unter BSD-3-Clause.
  Empfehlung: nur Konzepte übernehmen und selbst implementieren, dann entstehen keine MPL-Dateien im Baum.
- **Datenschutz/Netz:** Crest übernimmt ungoogled vollständig. Safe Browsing ist entfernt
  (`fix-building-without-safebrowsing`), GCM ist aus (`disable-gcm`), Field Trials sind aus,
  dazu Domain-Substitution, `block-requests`, **`disable-download-quarantine`** und
  `updater-disable-auto-update`. Widevine fehlt. Stattdessen wechselt die Seite zu WebKit
  (`4534ae4c`, Report `ProtectedMediaUnavailable`).

## 2. Rangliste der Kandidaten

| # | Kandidat | Crest | Ahoi heute | Warum besser | Aufwand | Risiko | Lizenz | Lane |
|---|---|---|---|---|---|---|---|---|
| 1 | **Eigener Keychain-Eintrag für OSCrypt** („AhoiBrowser Safe Storage“, Account = Bundle-ID) | `Patches/native-host.patch` Hunk `components/os_crypt/common/keychain_password_mac.mm` (`CrestKeychainName`), inkl. dokumentiertem Vorfall 0.6.96 | Unbranded M153 nutzt `"Chromium Safe Storage"`/`"Chromium"` (`.work/chromium/src/components/os_crypt/common/keychain_password_mac.mm:41`). Lokal existiert nur dieser Eintrag. Kein Ahoi-Patch dazu. `docs/BUILDING.md:257-276` beschreibt nur Signatur-Prompts | Ahoi teilt den Profil-Schlüssel mit jedem anderen Chromium auf dem Mac, auch mit unserem eigenen `upstream-release`-Kontrollbuild und mit ungoogled. Wer dort „Immer erlauben“ klickt, gibt einer Fremd-App den Schlüssel für Cookies und Passwörter. Unterschiedlich signierte Builds lösen gegenseitig Passwort-Prompts aus. | S (ca. 30 Zeilen Patch plus Migration) | **Hoch, wenn naiv umgesetzt.** Eine Umbenennung rotiert den Schlüssel, `Secure Preferences`-Validatoren (`protection.macs.*`) schlagen fehl, und Tracked Prefs werden zurückgesetzt (Crest verlor so alle Extensions). Besser als Crest: beim ersten Start das vorhandene Geheimnis in den neuen Eintrag *kopieren*, dann ohne Rotation. Muss vor dem Launch passieren. | Konzept frei, Code selbst schreiben | desktop (Patch-Stack), crest-hardening liefert Handoff + Tests |
| 2 | **Automatischer Upstream-Sicherheitsroll** (Scheduler prüft, baut Kandidaten, öffnet PR; Milestones nur mit Review) | `.github/workflows/chromium.yml`, `Scripts/control-plane/chromium_fork.py` (`check-upstream`, `update`, strict apply ohne Fuzz, Rollback der Pins bei Fehlschlag), `fork.json` (`automaticMajorUpdates:false`) | `tools/chromium_roll.py` macht einen manuellen, isolierten Preflight (read-tree + apply). Es gibt keinen Zeitplan und keinen PR-Automaten. `config/upstream-roll-candidate.json` wird von Hand gepflegt. | Kürzere Zeit bis zum Sicherheitspatch. Der Automat baut nur einen Kandidaten und veröffentlicht nie selbst (Crest: `CHROMIUM_AUTO_RELEASE=false`). Unser Preflight ist technisch sogar strenger, es fehlt nur der Auslöser. | M | Mittel: ein persistenter Self-hosted Runner auf dem Arbeits-Mac kollidiert mit build/h3/e2e-Locks (siehe Memory „Lock checks must abort“). Kein PR-Trigger. | Idee frei | desktop (Build-Pfad), Tooling als crest-hardening-Handoff |
| 3 | **Offene Netzpunkte N6/N8/N4 nach ungoogled-Vorbild abschalten**, per Pref und ohne Domain-Substitution | ungoogled-Patches im Lock: `disable-intranet-redirect-detector`, `disable-fonts-googleapis-references`, `disable-mei-preload`, `disable-webrtc-log-uploader` | `docs/NETWORK_SILENCE_CHECKLIST.md` führt N6 als „Not handled“, N8 als „watch“, N4 als *unverified* | N6 lässt sich ohne Patch per Local-State-Pref `IntranetRedirectBehavior` = 1 in `privacy_defaults.cc` abstellen. MEI-Preload ist eine Komponente (N4). Das Prinzip „keine Pauschalsperre“ bleibt gewahrt. | S | Gering (Pref-Defaults, Audit-Rerun nötig) | BSD-3, nur Idee | desktop |
| 4 | **Explizite DCHECK-Entscheidung für Dev-Builds** | `source.lock.json` `developmentBuildArguments`: `dcheck_always_on=false`, `enable_expensive_dchecks=false`. README begründet, dass `is_debug=false` allein DCHECKs nicht abschaltet. | `config/build/ahoi-dev.gn` setzt nichts. Laut `build/config/dcheck_always_on.gni:24` sind DCHECKs in unseren Dev-Builds **an**. | Kein Ergebnis „besser als wir“, sondern Klarheit. DCHECKs in Dev sind für Ahoi eher erwünscht, weil sie Fehler finden. Visible-E2E-Timings und Perf-Aussagen aus Dev-Builds sind aber unzulässig. Das gehört in `BUILDING.md` bzw. in die Perf-Methodik (102 bindet Perf ohnehin an Release). | XS (nur Dokumentation) | keins | – | desktop |

Nur vier Kandidaten, weil Ahoi die übrigen Punkte bereits gleichwertig oder besser gelöst hat
(Abschnitt 3).

## 3. Geprüft und nicht übernehmen

- **Engine-Key und Artefakt-Wiederverwendung** (`chromium_engine.py`, Release `chromium-engine-<key>`):
  bei uns schon umgesetzt als `tools/engine_input_key.py` (Handoff 001, integriert `2ab7909`).
- **PGO/ThinLTO mit Profil-Checksumme** (`configure-chromium.py`): gleichwertig durch
  `tools/perf/optimization_receipt.py` (Handoff 102).
- **Speicherdruck-Entladen** (`97730434`): Crest musste es neu bauen, weil es Chromiums
  Tabstrip umgeht. Ahoi nutzt PerformanceManager-Eligibility mit Audio-, PiP-, Capture-,
  Download- und Formular-Sperren (`overlay/.../memory/tab_sleeping.cc:52-103`). Das ist besser.
- **Crash-Recovery-Budget** (`1b5bec29`), **PiP zurück zum Tab** (`9eda3bfc`), **Side Panel
  folgt Tab** (`2b4ac366`): Das sind Nachbauten dessen, was Chromiums nativer
  TabStrip/SidePanelCoordinator bei Ahoi schon liefert. Offen bleiben nur die Tests
  SP-QW-01..03 aus der Vorgängerreview.
- **Native-Messaging-Fallback auf Chromes `NativeMessagingHosts`** (Hunk
  `launch_context_posix.cc`): Ahois Vertrag lehnt kopierte Chrome-Manifeste
  ausdrücklich ab (`config/test-registry.json:2479`, 1Password nur über „Additional Browsers“).
  Bewusst nicht übernehmen.
- **Private Fenster ohne jede Extension** (`e4133dd3`): Chromiums Standard ist ohnehin
  Opt-in pro Extension. Kein Gewinn.
- **ungoogled als Basis, Safe Browsing aus, Download-Quarantäne aus, Auto-Update aus:**
  Das ist sicherheitlich schwächer und hinkt Google Stable hinterher. Ahois Entscheidung in
  `NETWORK_SILENCE_CHECKLIST.md:9-12` bleibt richtig. GCM (N1) und Field Trials (N2) sind
  bei uns bereits gezielt abgeschaltet (Patch 0056, `disable_fieldtrial_testing_config`).
- **lite-Tarball statt gclient** (ca. 10 GB statt über 100 GiB): Das spart Platz, dem
  lite-Tarball fehlen aber Testdaten. Ahoi braucht Unit- und Browser-Tests aus dem Checkout. Nicht übernehmen.
- **UI-Features** (Sidebar, Spaces, Befehlsleiste, Split, Sync): Sie liegen in Swift und C#
  außerhalb von Chromium. Es gibt nichts zu portieren. Die Konzeptvergleiche stehen bereits in
  `docs/reviews/2026-09-12-crest-vergleich.md`.

## 4. Einordnung

Crests Chromium-Weg ist echt, aber ein experimenteller Kanal (nur arm64, Stable
unverändert). Laut `097e7c56` sind weder ein frischer Engine-Build noch ein Milestone-Upgrade
getestet. Für Ahoi ist Kandidat 1 der einzige echte Befund mit Sicherheitswirkung. Er sollte
vor dem Launch umgesetzt werden, solange keine Nutzerprofile existieren. Kandidat 2 ist ein
Prozessgewinn, 3 und 4 sind kleine Aufräumarbeiten.
