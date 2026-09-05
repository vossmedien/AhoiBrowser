# Zielprompt: AhoiBrowser Mobile – Internal-Beta-Abschluss, release-first

## Auftrag

Bringe AhoiBrowser Mobile ohne weitere Feature-Schleifen zu einem belastbaren
internen TestFlight-Beta-Stand. Das Ziel ist jetzt ausdrücklich **nicht**, vor
der ersten Beta jede denkbare Variante, jedes Gerät und jeden externen
Apple-Prozess vollständig abzunehmen. Entscheidend ist:

1. Die zentralen Browser-Journeys funktionieren auf dem exakten Kandidaten
   sichtbar.
2. Es gibt keinen bekannten Release-Blocker wie Crash, Datenverlust,
   Privacy-Leak, kaputte Navigation, unbenutzbare Kernbedienung, falsche
   Signierung oder korrumpierenden Sync.
3. Danach laufen die gezielten programmatischen Sicherheits- und
   Regressionsgates.
4. Der bestehende interne TestFlight-Build wird installiert und kurz geprüft
   oder ein minimal korrigierter Nachfolgebuild wird bereitgestellt.
5. Externe Apple-, Geräte- und öffentliche Release-Gates werden ehrlich
   separat geführt und halten die interne Beta nicht künstlich auf.

Der umfassende Produktvertrag in
`outputs/AhoiBrowser-Mobile-Zielprompt.md` bleibt die funktionale Referenz.
Dieser Abschluss-Prompt ersetzt für die aktuelle Welle jedoch dessen
Reihenfolge, Vollmatrix und Abschlusskriterien. Bei Konflikten gilt dieses
release-first-Dokument.

Arbeite selbstständig bis zum Internal-Beta-ready-Ergebnis. Eröffne keine neue
Feature-Welle und keinen großen Onboarding-Wizard. Wenn eine reale
Kernfunktion bereits nachvollziehbar funktioniert, genügt ein repräsentativer
sichtbarer Smoke statt immer neuer Varianten.

## Verbindlicher Ausgangspunkt

- Repository:
  `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`
- gemeinsamer Branch:
  `codex/desktop-core-feature-wave-20260830`
- Mobile-Releasequelle von Build 10:
  `ab2e709d9cf77c4e73d548bb8d2869090940c0a0`
- aktueller gemeinsamer HEAD zu Beginn dieser Revision:
  `d6cfa71048998b5d3052d4ab746f1969c5702971`
- interner Kandidat: AhoiBrowser Mobile `0.1 (10)`
- Bundle: `app.ahoibrowser.AhoiBrowser`
- Apple Team: `248AJ5BN47`
- CloudKit-Container: `iCloud.app.ahoibrowser.AhoiBrowser`
- Mobile-Evidenz:
  `docs/IOS_BROWSER_E2E_EVIDENCE.md`
- TestFlight-/Upload-Evidenz:
  `docs/audit-evidence/2026-09-04-mobile-testflight-fix/`

Build 10 ist archiviert, signiert, hochgeladen, verarbeitet und intern
verteilt. Eine physische Installation und sichtbare TestFlight-Journey darf
nicht allein aus dem Portalstatus abgeleitet werden. Wenn inzwischen installiert
wurde, lies die installierte Buildnummer und den Installationskanal aus, statt
unnötig neu zu installieren.

Der echte verschlüsselte CloudKit-Development-Roundtrip einschließlich
Privacy-Negativprüfung ist bereits belegt. Solange seit diesem Beleg kein
Sync-/Schema-/Crypto-Code geändert wurde, wird er referenziert und nicht aus
Prinzip vollständig wiederholt.

## Zusammenarbeit im gemeinsamen Branch

Desktop und Mobile dürfen parallel im selben Branch arbeiten, müssen aber
disjunkte Pfade besitzen.

Mobile besitzt:

- `apps/AhoiMobile/**`;
- ausschließlich Mobile-spezifische Dateien unter `artifacts/e2e/**`;
- `docs/IOS_BROWSER_E2E_EVIDENCE.md`;
- `docs/audit-evidence/2026-09-04-mobile-testflight-fix/**`;
- ausschließlich Mobile-spezifische Repository-Tests unter
  `tests/repository/test_mobile_*.py`, soweit keine andere Session sie besitzt;
- diesen Zielprompt.

Desktop besitzt insbesondere:

- `.work/chromium/**`;
- `overlay/**`;
- `patches/**`;
- Desktop-Importer, Desktop-Extensions und Desktop-UI;
- Desktop-spezifische Installations- und Computer-Use-Evidenz.

Gemeinsame Dateien wie `config/external-gates.json`, `config/test-registry.json`,
`docs/SYNC.md`, `docs/RELEASING.md` und `docs/THREAT_MODEL.md` nur nach
aktueller Ownership-Prüfung bearbeiten. Fremde Dirty-Dateien weder resetten,
stash-en, formatieren noch mitcommitten. Vor Commit und Push Branch, Upstream,
Remote-SHA, Index und laufende Git-/Buildprozesse erneut prüfen. Nur eigene
Mobile-Pfade gezielt stagen; kein Force-Push.

Vor CPU-intensiven Builds gilt die globale AhoiBrowser-Buildpriorität. Ein
bereits laufender fremder Ahoi-/Chromium-Build wird nie gestoppt.

## Astra-Neubewertung: Produkt- und Architekturentscheidung

### Was beibehalten wird

- Harbor Deck und Focus Voyage bilden eine eigenständige, verständliche
  Mobile-UX.
- Die untere Adressleiste ist die primäre Browsersteuerung.
- Die Suche in Focus Voyage ist ein zusätzlicher Einstieg, kein konkurrierendes
  zweites Bedienmodell.
- Im kompakten Zustand bleiben Zurück, sichtbarer Ursprung/Adresse, Tabs und
  Mehr verfügbar.
- Private Darstellung, Dynamic Type, Reduce Motion, Reduce Transparency,
  Mindesttrefferflächen und semantische Accessibility bleiben verbindlich.
- Exakte Kandidatenbindung mit Commit-, Receipt- und Binary-Hash bleibt
  erhalten.

### Was nicht mehr vor der Beta ausgebaut wird

- kein neues Onboarding;
- keine zusätzliche Arc-Imitation um ihrer selbst willen;
- keine vollständige Neustrukturierung der Navigation;
- keine Micro-Animation für jeden Zustand;
- keine Vollmatrix aus allen Tabzahlen, Orientierungen, Kontrast- und
  Gerätevarianten;
- keine mobile Integration von uBlock Origin als Beta-Voraussetzung.

### Bessere technische Richtung für den Browser-Chrome

Der aktuelle Ansatz besitzt gute Hysterese und stabile Control-Identitäten,
aber die Sichtbarkeit wird mittelbar von zwei Signalquellen beeinflusst:
JavaScript-Scrollereignisse liefern die Scrollabsicht, SwiftUI/WebKit-Geometrie
liefert Layout- und Pull-to-refresh-Zustand, während Präsentationen weitere
Resets auslösen. Das ist für den Beta-Freeze nur dann zu ändern, wenn die
Inkonsistenz sichtbar reproduzierbar ist.

Falls eine Korrektur nötig ist, gilt:

1. Eine einzige typisierte State Machine besitzt den Zustand
   `expanded`, `collapsed` oder `lockedExpanded(reason)`.
2. Nur direkte, zusammenhängende Nutzer-Scrollereignisse dürfen zwischen
   `expanded` und `collapsed` wechseln.
3. SwiftUI-Geometrie liefert nur Pull-to-refresh und
   Layout-/Viewport-Invalidierung, aber keine zweite Sichtbarkeitsentscheidung.
4. Navigation, Loading, Fehler, Address/Find/Permission/Dialog und Tabwechsel
   setzen deterministisch `lockedExpanded` beziehungsweise `expanded`.
5. Die bestehende Hysterese bleibt ungefähr bei 28 pt zum Einklappen und
   14 pt zum Wiederaufklappen; Jitter und programmatic scroll werden ignoriert.
6. Pro sichtbarem Zustandswechsel gibt es genau eine kurze asymmetrische
   Animation. Reduce Motion entfernt Positions-/Skalierungsbewegung und
   erlaubt höchstens einen unaufdringlichen Crossfade.
7. Kein großer Rewrite ohne roten sichtbaren Golden-Path-Test.

Dateien am 800-Zeilen-Limit werden bei einer ohnehin nötigen Änderung sinnvoll
geteilt. Reines Umorganisieren ohne Produktnutzen blockiert die Beta nicht.

## Release-Blocker

Nur folgende Befunde stoppen die interne Beta:

- reproduzierbarer Crash oder Startfehler;
- Kernnavigation lädt normale HTTPS-Seiten nicht oder bleibt allgemein hängen;
- Adresse/Suche, Zurück, Reload/Stop, Tabs oder privater Tab sind praktisch
  unbenutzbar;
- normale und private Daten werden vermischt;
- Sync verliert, dupliziert, veröffentlicht private Daten oder korrumpiert
  vorhandenen Zustand;
- falsche Bundle-ID, Buildnummer, Signierung, Entitlements oder falscher
  CloudKit-/Keychain-Scope;
- eine Security-Grenze wird offen statt fail-closed umgangen;
- ein produktrelevanter Fehler verhindert Installation oder Cold Launch.

Ein lokaler Fixture-Fehler ist kein Produktfehler, wenn eine unabhängige
normale HTTPS-Seite sichtbar lädt und die Fixture-Ursache belegt wird. TLS darf
nicht abgeschwächt werden, um den Test grün zu machen.

## Bekannte nicht blockierende Beta-Risiken

Diese Punkte werden dokumentiert und nur bei sehr kleiner, risikoarmer
Korrektur vorgezogen:

- Hardware-Escape schließt auf einem Simulatorpfad das fokussierte
  Adressblatt nicht, solange der sichtbare Abbrechen-Button funktioniert;
- keine aktuelle physische iPad-Abnahme mangels kompatiblem Gerät;
- keine vollständige 1-/5-/20-Tab-Performance-Matrix;
- nicht jede Permission-, Download-Recovery-, Rotation-, Pointer- oder
  Accessibility-Kombination auf Build 10;
- CloudKit Production und vollständiger Mac-iPhone-iPad-Roundtrip;
- öffentlicher TestFlight-Link, Beta Review, Default-Browser-Grant und
  öffentlicher App-Store-Release;
- uBlock Origin auf iOS.

Diese Risiken dürfen nicht als bestanden bezeichnet werden, verhindern aber
den internen Beta-Smoke nicht.

## Phase 1 – sichtbarer Golden Smoke auf dem exakten Kandidaten

Vor breiten programmatischen Tests:

1. Kandidatenidentität sichtbar beziehungsweise maschinenlesbar bestätigen:
   Version, Build, Source-Commit und Receipt/Binary-Hash.
2. Cold Launch ohne Crash.
3. Eine normale HTTPS-Seite vollständig sichtbar laden; sichtbaren
   Ursprung/Sicherheitsstatus prüfen.
4. Auf einer ausreichend langen Seite einmal bewusst nach unten scrollen:
   Harbor Deck klappt konsistent kompakt. Gegenbewegung klappt ihn wieder auf.
   Adresse, Tabs und Mehr bleiben erreichbar.
5. Adresse öffnen, eine zweite Seite aufrufen, Zurück und Reload/Stop kurz
   prüfen.
6. Einen normalen Tab und einen privaten Tab öffnen; die private Semantik und
   fehlende normale Projektion sichtbar prüfen.
7. Einen kleinen Download oder die bereits stabile Download-Übersicht öffnen,
   sofern die Fixture zuverlässig verfügbar ist.
8. Sync-Status sichtbar prüfen. Den bestehenden echten Development-Roundtrip
   referenzieren, falls kein Sync-Code verändert wurde.

Das ist der notwendige Golden Smoke, keine Vollmatrix. Pro Kernjourney genügt
eine repräsentative Variante auf iPhone. Ein iPad- oder physischer
TestFlight-Smoke wird ergänzt, sobald das Gerät verfügbar ist.

Wenn die lokale HTTPS-Fixture zwar `GET /` mit 200 beantwortet, die UI aber
bei einem Ladefortschritt hängen bleibt, zuerst unterscheiden:

- Fixture-Dokument-/Ressourcenabschluss;
- WebKit-/Navigation-Lifecycle;
- Kandidatenfehler.

Eine unabhängige HTTPS-Navigation entscheidet, ob es ein Release-Blocker ist.
Den Befund einmal kausal schließen; keine wiederholten identischen Läufe.

## Phase 2 – gezielte programmatic gates

Erst nach dem Golden Smoke beziehungsweise nach sauber dokumentierter
technischer E2E-Blockade:

- Mobile Core Tests;
- relevante CloudKit-/Crypto-/Privacy-Tests;
- Mobile Repository- und Signing-Verträge;
- Swift-6-/Strict-Concurrency-Build;
- `xcodebuild analyze` für den Releasekandidaten, sofern seit dem letzten
  belegten Lauf produktiver Code geändert wurde;
- XcodeGen-No-op, DCO, Secret Scan, `git diff --check` und Zeilenlimit;
- nur die fokussierten Tests der tatsächlich geänderten Komponente.

Keine breite Suite wird wiederholt, wenn nur Dokumentation oder Evidenz
geändert wurde. Ein roter programmatic test wird kausal eingeordnet; ein
veralteter Contract-Test wird nicht durch Produktregression „repariert“.

Nach einer größeren Produktkorrektur zuerst die betroffene sichtbare Journey
wiederholen, danach nur die abhängigen programmatic gates. Kein „alle Tests
noch einmal“ ohne konkrete Risikokante.

## Phase 3 – TestFlight und Geräte

1. Prüfe, ob Build 10 auf `Servusla` inzwischen als TestFlight-Build
   installiert ist.
2. Wenn ja: Build `0.1 (10)`, `builtByDeveloper=false`, Cold Launch und
   eine HTTPS-Navigation belegen.
3. Wenn TestFlight selbst oder Build 10 fehlt: Installation über den bereits
   internen Build ausführen, sobald das Gerät bedienbar ist.
4. Nur wenn ein Release-Blocker Codeänderungen erzwingt, Buildnummer erhöhen,
   neuen Kandidaten bauen, signieren, hochladen und den kurzen Golden Smoke
   wiederholen.
5. Ein kompatibles physisches iPad bleibt ein externer Gate, solange keines
   verfügbar ist.

Eine öffentliche Beta oder App-Store-Veröffentlichung benötigt weiterhin eine
separate ausdrückliche Freigabe. Der interne TestFlight-Build darf dagegen im
bereits autorisierten internen Kreis geprüft werden.

## Sync-Abschluss

Sync wird risikobasiert statt erschöpfend behandelt:

- vorhandenen echten verschlüsselten CloudKit-Development-Beleg gegen den
  enthaltenen Sync-Code und Schema-Stand binden;
- sichtbaren Sync-Status auf dem Kandidaten prüfen;
- private Tabs, private History und private Downloads dürfen nie publiziert
  werden;
- bei keiner Sync-Codeänderung kein künstlicher neuer 27-Fälle-Roundtrip;
- bei Sync-Codeänderung zuerst ein echter Create/Update/Delete-Roundtrip plus
  Privacy-Negativprobe, danach gezielte Unit-/Package-Tests;
- Production-CloudKit und mehrere physische Geräte getrennt als externer
  Release-Gate führen.

## uBlock-Entscheidung

uBlock Origin Classic ist in einer WKWebView-basierten iOS-App nicht als
GitHub-Extension integrierbar wie im Chromium-Desktop-Browser. Ein begrenzter
Content-Blocker-Ansatz nach Art von uBlock Origin Lite wäre ein separates
Feature mit eigener Lizenz-, Update-, Regelcompiler-, App-Extension- und
App-Review-Prüfung. Er ist ausdrücklich **kein** Blocker für diese Beta und
wird jetzt weder als halbfertige Extension eingebaut noch weiter erforscht,
sofern kein bereits trivial lauffähiger, rechtlich sauberer Pfad vorliegt.

## Bounded-Fix-Regel

Für jeden roten Pfad:

1. einmal reproduzieren und Kandidatenbezug sichern;
2. Produkt, Fixture, Gerät oder externe Grenze klassifizieren;
3. kleinste ursächliche Korrektur vornehmen;
4. genau die betroffene sichtbare Journey erneut ausführen;
5. nur abhängige Tests ergänzen;
6. nach zwei unveränderten Wiederholungen nicht weiter loopen, sondern den
   Befund als bekannten Beta-Risk oder echten externen Gate dokumentieren.

## Definition of Done: Internal Beta Ready

Das aktuelle Ziel ist erfüllt, wenn:

1. der kurze Golden Smoke auf Build 10 oder einem notwendigen
   Nachfolgekandidaten keinen Release-Blocker zeigt;
2. jede sichtbare technische Blockade ehrlich von einem Produktfehler
   unterschieden ist;
3. die gezielten programmatischen Gates für den enthaltenen Code grün sind;
4. der echte Development-Sync-Beleg weiterhin zum Sync-/Schema-Stand passt
   oder nach Sync-Änderungen gezielt erneuert wurde;
5. der interne TestFlight-Status sowie die physische Installation entweder
   belegt oder mit einem konkreten externen Geräte-Gate dokumentiert sind;
6. eigene Mobile-Änderungen fokussiert, DCO-signiert und ohne fremde
   Desktop-Dateien auf den gemeinsamen Branch gepusht sind;
7. verbleibende iPad-, Production-CloudKit-, öffentliche TestFlight-,
   Default-Browser- und Store-Gates getrennt aufgelistet sind.

Nach Erreichen dieser sieben Punkte nicht weiter „perfektionieren“. Übergib
den internen Beta-Stand und den kleinen bekannten-Risiken-/External-Gates-
Backlog. Erst echtes Beta-Feedback eröffnet eine neue Produktwelle.
