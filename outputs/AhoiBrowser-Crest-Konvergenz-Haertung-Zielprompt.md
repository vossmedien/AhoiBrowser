# AhoiBrowser – Zielprompt Crest-Konvergenz-Härtung (Parallel-Lane)

**Geltungsstand: 25. September 2026.** Vom Nutzer beauftragt: die fünf
Nachschärfungen aus der Bewertung von Crests Chromium-/Control-Plane-Umbau
vollständig umsetzen, **parallel** zur laufenden Desktop-, Mobile- und Sync-
Arbeit und ohne diese zu stören. Dieser Zielprompt ergänzt den
[Master-Zielprompt](AhoiBrowser-Master-Zielprompt.md); er ändert weder dessen
Produktumfang noch Architektur, Paketfolge oder Eigentümerschaften.
Begründung und Quellen: [Crest-Chromium-Kern-Bewertung](../docs/reviews/2026-09-25-crest-chromium-core.md).
Aktueller Lane-Stand: [Lane-Checkpoint](../docs/ACTIVE_CREST_HARDENING_LANE.md).
Maschinenlesbare Grenzen: [`config/agent-lanes.json`](../config/agent-lanes.json).

## Ausgangslage in einem Absatz

Crest (bisher WebKit) macht seit dem 23. September Chromium 152 zur Mac-Engine,
behält seine SwiftUI-Oberfläche und verlagert Domäne und Sync-Semantik in einen
gemeinsamen portablen Kern. Das bestätigt Ahois Engine-Entscheidung. Ahoi
übernimmt ausdrücklich **nicht** den .NET-Kern, den Ersatz der Chromium-
Oberfläche durch SwiftUI, eine zweite Mac-Engine oder ungoogled-chromium als
Basis. Übernommen werden fünf Arbeitsprinzipien, die unsere bekannten
Schwachstellen adressieren: doppelte Sync-Logik in C++ und Swift, unklare
Schreibhoheit zwischen Tab-Baum und Chromium-Session, fehlende vergleichbare
Performance-Methodik, teure Build-Wiederholung und unvollständige Netzwerk-
Stille eines frischen Profils.

## Gesamtziel

Setze die Pakete H1–H5 vollständig bis zu ihrer jeweiligen Definition of Done
um. Arbeite dabei ausschließlich in der Lane `crest-hardening`. Alles, was
fremden Besitz berührt, entsteht als geprüfte, anwendungsfertige Übergabe und
wird vom jeweiligen Eigentümer in dessen nächstes geplantes Paket übernommen.
Ein Paket gilt erst als abgeschlossen, wenn die Übergabe integriert und am
exakt zugeordneten Kandidaten belegt ist; eine fertige Übergabe allein ist
„übergeben“, nicht „abgeschlossen“.

## Pakete und Definition of Done

### H1 – Sync-Konformität zwischen C++ und Swift absichern

Problem: Modell, Codec, Merge und Store existieren zweimal. Die bestehenden
Goldens pinnen Bytes, nicht Merge-Ergebnisse.

Umsetzen:

1. **Merge-Konformitätsvektoren** für Format 3: je Fall Ausgangszustand,
   Folge lokaler/entfernter Änderungen (inklusive verzögerter Batches,
   gleicher Feld-Uhren, Tombstones, Quarantäne-Auslösern, Offline-Union mit
   Nachfolger-Uhr, atomarer Gruppen `location`/`liveness`) und erwarteter
   kanonischer Endzustand. Abgedeckt werden alle aktiven Entityklassen der
   `config/sync-format.json`; neue Format-3-Domänen (Split, Archiv, Home-URL,
   Routing, Shortcuts) erhalten Vektoren, sobald ihre Feldkarten dort
   veröffentlicht sind.
2. **Zwei Runner**, die dieselben Vektoren ausführen: ein C++-Unittest gegen
   die vorhandene Merge-/Validierungslogik und ein Swift-XCTest gegen das
   vorhandene Mobile-Pendant. Beide melden abweichende Fälle einzeln.
3. **Differenzielle Zufallsprüfung**: deterministisch geseedeter Generator
   erzeugt zusätzliche Operationsfolgen; ein Repository-Tool vergleicht die
   kanonischen Endzustände beider Runner. Seeds eines Fehlschlags werden als
   neuer fester Vektor aufgenommen.
4. **Schema-Drift-Gate**: Ein Generator liest `config/sync-format.json` und
   erzeugt Feldkarten/Typtabellen für C++ und Swift. Ein Repository-Test
   schlägt fehl, wenn die handgeschriebenen Feldkarten davon abweichen.
   Die Ablösung handgeschriebener Codecs durch generierten Code ist **optional**
   und nur zulässig, wenn der Sync-Owner sie ausdrücklich annimmt.

DoD: Vektoren und Generator im Repository; beide Runner grün am exakt
integrierten Desktop- bzw. Mobile-Stand (oder jede Abweichung als konkreter
Defekt mit Vektor an den Sync-Owner übergeben); Drift-Gate in
`./scripts/test-repository.sh` grün. Keine Änderung des Wire-Formats, keine
Aktivierung einer Capability, kein neuer Versionsdefault.

### H2 – Eine Schreibhoheit für Tab-Baum und Chromium-Session

Problem: Wiederkehrende Split-, Null-Tab-, Leerer-Workspace- und Restore-
Fehler deuten auf Pfade, in denen Ahoi-Baum und `TabStripModel`/Session beide
Zustand schreiben oder Beobachter-Rückkopplungen erneut schreiben.

Umsetzen:

1. **Audit** aller schreibenden Übergänge zwischen `tab_tree`, `session`,
   Split, Quick Window, Popup/Peek, Archiv und Workspace-Wechsel: wer
   entscheidet, wer führt aus, wer beobachtet. Ergebnis als Tabelle mit
   Datei/Zeile je Übergang und Einstufung „eine Autorität“ / „Doppelschreiber“
   / „Rückkopplung“.
2. **Regel festschreiben**: Eine Ahoi-Operation entscheidet und committet den
   semantischen Übergang; Chromium führt aus und meldet Abschluss;
   Beobachtung einer selbst ausgelösten Änderung schreibt nicht erneut.
   Wiederholte oder verspätete Abschlüsse (Promotion, Dismiss, Close) sind
   über eine prozesslokale Operations-ID idempotent. Die Regel wird als
   Abschnitt in die Architektur-Dokumentation übergeben.
3. **Regressionstests** je gefundenem Doppelschreiber/Rückkopplungspfad,
   rot vor der Korrektur, und eine **minimale Korrektur** je Befund.
   Der offene Empty-Workspace-Navigationsdefekt bleibt beim Desktop-Owner;
   H2 liefert ihm Audit-Befunde, keinen konkurrierenden Fix.

DoD: Audit und Regel übergeben und vom Desktop-Owner übernommen; jede
Korrektur mit Test im nächsten Desktop-Paket gebaut, installiert und die
betroffenen sichtbaren Journeys (Split, Null-Tab, Workspace-Wechsel, Restore,
Quick-Window-Übernahme) am exakten Kandidaten grün.

### H3 – Vergleichbare Performance-Methodik für Paket 5

Umsetzen:

1. **Methodik-Dokument**: gleiche Build-Konfiguration als Vergleichsbasis
   (Ahoi gegen Chromium ohne Ahoi-Oberfläche desselben Pins, danach
   Releasekonfiguration gegen passende Chrome-Version), PGO/ThinLTO nur mit
   gepinntem Profil, feste Fenstergröße/Vordergrund/Energiezustand,
   Accessibility-Zustand explizit protokolliert (aktive AX-Inspektion
   verfälscht Speedometer), Aufwärm- und Wiederholungsregeln, Streuung.
2. **Harness** unter `tools/perf/`: Kaltstart bis erste Paint, Workspace-
   Wechsel, Command-Bar-Suche, Speicher bei N Tabs mit Memory Saver,
   Speedometer über DevTools-Protokoll; Ergebnis als kandidatgebundenes
   Evidenz-JSON mit Build-Konfiguration und Host-Last.
3. **Budget-Abgleich** mit den bestehenden Bundle-/CPU-/RAM-/Netzwerkbudgets.

DoD: Methodik und Harness im Repository mit Repository-Tests des
Auswertungsteils; mindestens ein vollständiger Messlauf am exakt installierten
Kandidaten unter dem Ressourcen-Lease `host-quiet` (siehe unten). Keine
Behauptung „schneller/leichter“ ohne Vergleichslauf gleicher Konfiguration.

### H4 – Engine-Eingabeschlüssel und Kandidaten-Wiederverwendung

Umsetzen:

1. `tools/engine_input_key.py`: deterministischer Schlüssel über Chromium-Pin,
   `patches/chromium/series` samt Patchinhalten, Overlay-Fingerprint
   (vorhandenes `tools/overlay_fingerprint.py` wiederverwenden), wirksame
   GN-Argumente, Toolchain-/SDK-Identität und Build-Ziel.
2. **Lookup-Modus**: findet vorhandene Build-Receipts unter
   `artifacts/build/` mit identischem Schlüssel und meldet, ob ein Neubau
   vermeidbar ist. Nur lesend.
3. **Übergabe** an den Build-Owner: Aufnahme des Schlüssels in den Receipt
   von `build-ahoi.sh`/`tools/build_provenance.py`.

DoD: Tool und Tests grün; Lookup korrekt an mindestens zwei vorhandenen
Receipts geprüft; Receipt-Integration vom Build-Owner übernommen und im
nächsten regulären Build erzeugt. Kein zusätzlicher Build allein für H4.

### H5 – Crest als Chromium-Referenz und Netzwerk-Stille

Umsetzen:

1. **Referenz-Review** von Crests Chromium-Host (Download-Adapter,
   Close-/Before-Unload-Vorbereitung, Profil-Entfernung, Berechtigungs-
   Rückgabe an offene Seiten, Extension-Seitenleisten) gegen Ahois
   entsprechende Pfade: je Thema Ahoi-Status und konkrete Testfälle.
   Nur Ideen; jede tatsächliche Codeübernahme braucht eine dateibezogene
   MPL-2.0-/Notice-Prüfung. Keine Crest-Markenassets.
2. **Netzwerk-Checkliste** aus ungoogled-chromiums Patch- und
   Domain-Substitutionslisten für Chromium 153, abgeglichen mit Ahois
   bestehender Entgooglifizierung und dem Fresh-Profile-Netzwerkaudit.
   Safe Browsing, Widevine-Komponentenversorgung und Updater bleiben
   bewusst erhalten.
3. **Konkreter Befund GCM**: Der am 25. September beobachtete
   `google_apis/gcm`-Registrierungsverkehr eines frischen Profils wird
   ursachengenau belegt und als minimaler Patch-/Pref-Vorschlag übergeben.

DoD: Review und Checkliste im Repository; GCM-Korrektur vom Desktop-Owner
integriert und im Fresh-Profile-Netzwerkaudit des exakten Kandidaten ohne
unerwartete Google-Endpunkte belegt.

## Parallelvertrag: kollisionsfreie Zusammenarbeit

### Lanes und Eigentum

| Lane | Eigentümer | Besitz (Kurzform; verbindlich ist `config/agent-lanes.json`) |
| --- | --- | --- |
| `desktop` | laufender Desktop-Agent laut `docs/ACTIVE_DESKTOP_CHECKPOINT.md` | `overlay/`, `patches/`, Build-/Install-/Signing-Skripte, `.work/chromium/src`, `out/AhoiDev`, `/Applications/AhoiBrowser.app`, Desktop-Checkpoint |
| `sync` | Sync-Owner laut `docs/ACTIVE_SYNC_COORDINATION.md` | gemeinsamer Sync-Vertrag, `config/sync-*.json`, C++/Swift-Sync-Code |
| `mobile` | Mobile-Agent laut `docs/ACTIVE_MOBILE_CHECKPOINT.md` | `apps/AhoiMobile/` |
| `crest-hardening` | dieser Auftrag | nur die in `config/agent-lanes.json` gelisteten eigenen Pfade |

### Harte Regeln für `crest-hardening`

1. **Nie direkt** in `overlay/`, `patches/`, `apps/`, `config/sync-*.json`,
   `config/test-registry.json`, `scripts/` oder in fremde Checkpoints
   schreiben. Neue Dateien unter `overlay/` sind ebenfalls verboten: sie
   ändern den Overlay-Fingerprint und damit die Provenienz fremder Builds.
2. Beiträge für fremden Besitz entstehen vollständig unter
   `handoffs/crest-hardening/<NNN>-<thema>/` als Pfadspiegel plus
   `HANDOFF.md` (Zweck, Zielpfade, Anwendung, erwartete Tests, Risiken).
   Der Eigentümer übernimmt sie gebündelt in sein nächstes geplantes Paket;
   danach setzt er im Handoff den Status `integrated <commit>`.
3. **Kein Build, keine Installation, kein Overlay-Refresh**, kein Zugriff auf
   `.work/chromium/src` oder `out/AhoiDev`, kein Beenden fremder Prozesse.
   Chromium-seitige Runner laufen ausschließlich im Paket des Desktop-Owners.
4. **Geteilte Ressourcen nur per Lease**: `installed-app` (Start des
   installierten Kandidaten für Messung/Audit), `host-quiet` (Performance-
   Messung; kein Build und keine schwere Last anderer Lanes) und
   `simulator` (Mobile-Simulator). Ein Lease wird im Lane-Checkpoint
   angefragt und gilt erst, wenn der Eigentümer der Ressource ihn in seinem
   eigenen Checkpoint bestätigt. Ohne Bestätigung: unabhängige Arbeit.
5. **Git im gemeinsamen Arbeitsbaum**: nie `git add -A`, `stash`, `reset`,
   `checkout`/`restore` fremder Pfade, `rebase` oder Push mit Force.
   Commits sind pfadbegrenzt (`git commit -- <eigene Pfade>`), enthalten den
   Trailer `Lane: crest-hardening` und keine fremden uncommitteten Änderungen.
   Eine Einfügung in eine fremd modifizierte Datei (nur Master-Zielprompt und
   AGENTS.md mit Verweis) wird hunk-genau gestaged.
6. Bei jeder Unsicherheit über Besitz: nicht schreiben, im Lane-Checkpoint
   als offene Frage vermerken, unabhängig weiterarbeiten.

### Pflichten der anderen Lanes

- Keine Änderungen an den `crest-hardening`-Pfaden; Hinweise dorthin
  gehören in den eigenen Checkpoint.
- Vor dem Schnitt eines neuen Desktop-/Sync-/Mobile-Pakets offene Handoffs
  mit Status `ready` prüfen und entweder übernehmen oder mit Begründung
  zurückstellen. Übernahme erzeugt keinen Extra-Build.
- Lease-Anfragen im eigenen Checkpoint bestätigen oder ablehnen.

### Überwachung durch den Terminal-Cockpit-Orchestrator

Der Orchestrator prüft regelmäßig und vor jeder Paketübergabe:

1. `python3 tools/check_lane_boundaries.py --all --since <Basis-Commit>`
   – schlägt fehl, wenn ein Commit einer Lane fremde exklusive Pfade
   berührt oder ein `crest-hardening`-Commit außerhalb seiner Allowlist
   schreibt; `--worktree` prüft zusätzlich uncommittete Änderungen in
   den `crest-hardening`-Pfaden.
2. Offene Leases in `docs/ACTIVE_CREST_HARDENING_LANE.md` gegen die
   Bestätigungen in den Eigentümer-Checkpoints.
3. Handoffs mit Status `ready`, die älter als ein Desktop-Paket sind, und
   meldet sie dem Desktop-/Sync-Owner.
4. Keine zwei aktiven Builds, Installationen oder Messläufe gleichzeitig.

Ein Verstoß wird gemeldet, nicht automatisch zurückgesetzt; die betroffene
Lane korrigiert ihn selbst.

## Reihenfolge

Unabhängig und sofort: H4, H5 (Review/Checkliste), H3 (Methodik/Harness),
H1 (Vektoren/Generator/Drift-Gate), H2 (Audit). Danach gebündelte Handoffs an
Desktop/Sync/Mobile. Messläufe (H3), Audit am Kandidaten (H5) und sichtbare
Journeys (H2) folgen dem jeweils nächsten regulären Kandidaten des
Desktop-Owners. Nachgewiesene Crashes, Datenverlust und Sicherheitsfehler
anderer Lanes haben Vorrang; diese Lane blockiert keine fremde Arbeit.

## Nicht-Ziele

Kein portabler .NET- oder sonstiger Fremdsprachen-Kern, keine SwiftUI-Ablösung
der Chromium-Oberfläche, keine zweite Mac-Engine, kein ungoogled-chromium als
Basis, kein neues Sync-Format oder Versionsdefault, kein Abschalten von Safe
Browsing oder Widevine, keine Windows-/Linux-/Android-Portierung.
