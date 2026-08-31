# Zielprompt: AhoiBrowser Mobile – vollständiger Feature-, E2E- und Release-Abschluss

## Auftrag

Schließe AhoiBrowser Mobile vollständig und wahrheitsgemäß ab. Arbeite zuerst
alle noch lokal kontrollierbaren Produkt-, UX-, Security-, Recovery-,
Accessibility-, Performance- und Evidenzlücken fertig. Erzeuge danach einen
Feature-Freeze-Kandidaten und führe erst auf diesem exakten Kandidaten die
vollständige sichtbare End-to-End-Abnahme aus. Führe anschließend die
programmatischen Final-Gates aus und arbeite danach die real erreichbaren
Apple-, Hardware-, CloudKit-, TestFlight- und Default-Browser-Gates in der
festgelegten Reihenfolge ab.

Dieses Dokument ergänzt den vollständigen Ausgangsvertrag in
`outputs/AhoiBrowser-Mobile-Zielprompt.md`. Bei Widersprüchen zur Reihenfolge
gilt für diese Abschlusswelle ausdrücklich der neueste Nutzerauftrag:

1. alle offenen Features vollständig implementieren und intern absichern;
2. erst nach Feature Freeze den vollständigen sichtbaren E2E-Lauf starten;
3. danach breite programmatische Final-Gates ausführen;
4. danach externe Gates real versuchen und schließen;
5. nach einer größeren E2E-Korrektur einen neuen Kandidaten bauen, zuerst die
   betroffenen sichtbaren Journeys wiederholen und anschließend die nötige
   Regression erneut ausführen.

Arbeite nach Aktivierung dieses Ziels selbstständig durch alle Phasen weiter.
Stoppe nicht nach Prompt, Inventur, Teilimplementierung, Simulatorbuild oder
Zwischenbericht. Halte nur an, wenn eine neue Nutzerentscheidung rechtlich,
öffentlich, kostenpflichtig oder irreversibel wäre oder wenn derselbe echte
externe Gate nach begrenzten realen Versuchen keine weitere kontrollierbare
Arbeit zulässt.

Kein Quelltext-, Build-, Unit-Test-, Simulatorstart-, Signatur- oder
Portalstatus darf als Ersatz für eine sichtbare Journey, eine installierte
Runtime oder einen realen Sync-Roundtrip ausgegeben werden.

## Verbindlicher Ausgangspunkt

- Repository:
  `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`
- gemeinsamer Branch: `codex/desktop-core-feature-wave-20260830`
- Ausgangs-HEAD und Remote zu Beginn: `d6e2d2c9a912c60e296dbd3afbc24ba4dac84683`
- letzter sichtbar geprüfter Produktstand:
  `954643d2bc940c79dfc77df67ab23176ce2f00b6`
- aktueller Mobile-Evidenzstand:
  `docs/IOS_BROWSER_E2E_EVIDENCE.md`
- bestehender vollständiger Mobile-Vertrag:
  `outputs/AhoiBrowser-Mobile-Zielprompt.md`

Bereits grün, aber im finalen Kandidaten erneut zu binden:

- Cold-URL-Deduplizierung;
- Harbor-Deck-/Adressleisten-Collapse, Reverse Restore, Jitter-Policy,
  Präsentations-Restore und Reduce Motion;
- providerfreier lokaler Sync-Opt-in, Persistenz, Fail-closed-Verhalten und
  Opt-out;
- Mobile Core, providerfreie CloudKit-/Security-Verträge, Mobile-
  Repository-Verträge, Development-Signing-Verträge, Swift-Parse und
  Mobile-Zeilenbudget;
- Development-Signierung für Team `248AJ5BN47`, Bundle
  `app.ahoibrowser.AhoiBrowser` und Container
  `iCloud.app.ahoibrowser.AhoiBrowser`.

Diese Ergebnisse sind Baseline, keine Freistellung von der finalen
kandidatgebundenen Gesamtregression.

## Gemeinsamer Branch und Kollisionsschutz

Arbeite parallel zur Desktop-Session im selben Branch, aber mit disjunkter
Pfadverantwortung.

Mobile besitzt grundsätzlich:

- `apps/AhoiMobile/**`;
- `spikes/cloudkit/**`, soweit eine Änderung ausschließlich den gemeinsamen
  Mobile-/CloudKit-Vertrag betrifft und vorher auf Desktop-Auswirkungen geprüft
  wurde;
- `outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`;
- `outputs/AhoiBrowser-Mobile-Zielprompt.md` nur bei zwingender
  Vertragskorrektur;
- eindeutig Mobile-spezifische Evidenz unter `artifacts/e2e/**` und
  `docs/IOS_BROWSER_E2E_EVIDENCE.md`.

Desktop besitzt insbesondere `.work/chromium/**`, `overlay/**`, `patches/**`,
Desktop-Importer, Desktop-Extensions und Desktop-UI. Gemeinsame Dateien wie
`config/external-gates.json`, `config/test-registry.json`, `docs/SYNC.md`,
`docs/RELEASING.md`, `docs/THREAT_MODEL.md` und Product Config dürfen erst nach
Live-Prüfung fremder Änderungen und mit einem gezielten Merge bearbeitet
werden.

Vor jedem Commit und Push:

1. Branch, HEAD, Upstream, Remote-SHA, Status, Index und `index.lock` prüfen;
2. laufende Git-, Xcode- und AhoiBrowser-/Chromium-Prozesse prüfen;
3. ausschließlich eigene Mobile-Pfade exakt stagen;
4. fremde Änderungen niemals resetten, stashen, formatieren oder committen;
5. DCO-Sign-off und `git diff --check` ausführen;
6. niemals force-pushen.

Vor CPU-intensiven Mobile-Arbeiten prüfe auf AhoiBrowser-/Chromium-Builds in
`.work/chromium/src` beziehungsweise `out/AhoiDev`. Beginnt ein solcher Build,
stoppe oder pausiere ausschließlich die eigenen exakt identifizierten
CPU-intensiven Prozesse und setze sie später inkrementell fort.

## Abschlussprinzipien

- Kein neues großes Onboarding und kein dekorativer Redesign-Scope.
- Native Systemmuster, semantische Farben, SF Symbols, Dynamic Type,
  VoiceOver und Reduce Motion/Transparency haben Vorrang vor eigener
  Nachbildung.
- Harbor Deck, Focus Voyage und Workspace Canvas bleiben die verbindliche
  Ahoi-Informationsarchitektur.
- Keine zweite Tab-, Such-, Sync-, Download-, Permission- oder
  Session-Architektur neben dem vorhandenen autoritativen Modell.
- Private Daten dürfen niemals in normale Session, Verlauf, Suche, Sync,
  Geräte-Tabs, Logs oder Restore gelangen.
- Unsichere beziehungsweise unbekannte Schemes bleiben fail closed.
- CloudKit-, Keychain-, Account-, Zone-, Tombstone- und Cleanup-Grenzen werden
  nicht gelockert, um Tests grün zu machen.
- Jede Swift-Datei bleibt bei höchstens 800 Zeilen; bei Wachstum zuerst
  verantwortungsbasiert schneiden.
- Swift 6 Strict Concurrency, Main-Actor-Grenzen, `Sendable`-Verträge,
  deterministische Persistenz und idempotentes Cleanup bleiben verbindlich.

## Phase A – vollständige lokale Feature-Fertigstellung

Während Phase A dürfen gezielte Compile-, Unit-, Integration-, statische und
deterministische Harness-Tests genutzt werden. Die vollständige sichtbare
E2E-Abnahme beginnt jedoch erst nach dem Feature Freeze. Ein kleiner
interaktiver Diagnose-Smoke ist nur zulässig, wenn eine konkrete
Implementierungsfrage ohne Runtime nicht beantwortbar ist; er ist keine
Abnahme und erzeugt keinen PASS-Status.

Beginne Phase A mit einer kandidat- und dateigebundenen Gap-Map für
`MOB-USER-01` bis `MOB-USER-15`. Klassifiziere jede Journey als implementiert,
teilweise implementiert, fehlend oder primär Evidenzlücke. Prüfe die
Klassifikation am Quelltext; übernimm sie nicht ungeprüft aus Dokumentation.

Die aktuell bekannten kontrollierbaren Hochrisikoreste werden zuerst
geschlossen:

1. datensparsame, sichere Recovery abgebrochener Downloads;
2. eigene TLS-/Zertifikats- und WebContent-Fehlerdarstellung mit
   deterministischer, nicht wiederholender Recovery;
3. Dynamic Type, VoiceOver-Reihenfolge, High Contrast, iPad-Multitasking,
   Tastatur-, Pointer- und Fokusverhalten;
4. messbare Launch-, Speicher-, Discard-/Restore-, Energie- und
   1/5/20-Tab-Budgets;
5. danach alle primären Evidenzlücken in die finale Phase-B-Matrix überführen.

### A1 – Browsing- und Navigationskern

Schließe als zusammenhängenden Alltagspfad:

- Kaltstart, leeres Fenster, Adresse und Suchbegriff;
- konfigurierter Suchanbieter und sichere URL-Normalisierung;
- HTTPS-Navigation mit sichtbarer Origin;
- Zurück, Vor, Reload und Stop ohne Doppelload;
- WebContent-/Renderer-Ausfall mit verständlichem Restore;
- externe HTTP(S)-Aktivierung mit genau einem normalen Zieltab;
- keine Überschreibung eines geladenen oder privaten Tabs;
- deterministische Callback-Deduplizierung über Cold/Warm Launch.

Prüfe Implementierung, State Ownership, Fehlerpfade, Accessibility Labels,
lokalisierte Texte und Testseams. Entferne keine Sicherheitsprüfung zugunsten
eines Happy Paths.

### A2 – Tabs, Session und Workspaces

Schließe vollständig:

- normale und private Tabs anlegen, wechseln, umordnen und umbenennen;
- Close, Undo und Auswahl-Fallback;
- 1-, 5- und 20-Tab-Verhalten;
- Memory Pressure, Discard und deterministische Wiederaufnahme;
- Prozessende, Kaltstart und normales Session-Restore ohne Phantomtabs;
- aktive Seite speichern und zwischen Workspaces verschieben;
- aus Workspace Canvas, Baum, Suche und Verlauf wieder öffnen;
- Ordner/Seiten anlegen, umbenennen, sortieren, verschieben und
  tombstone-sicher löschen;
- iPhone-Sheet und iPad-Sidebar auf demselben Datenmodell.

Tab-, Workspace- und Session-Schreibvorgänge müssen revisionsserialisiert,
absturzfest und idempotent sein. Ein UI-Update darf keinen zweiten
Persistenzpfad erzeugen.

### A3 – Privatmodus und Datenisolation

Belege im Code und in deterministischen Testseams:

- normale Seiten verwenden ausschließlich den persistenten Standardstore;
- alle privaten Tabs einer laufenden privaten Session teilen genau einen
  nichtpersistenten Store;
- private Cookies, Website-Daten, Downloads und Dialogkontext bleiben vom
  normalen Modus getrennt;
- private Tabs erscheinen nicht in Session, Verlauf, Suche, Focus Voyage,
  Sync, Geräte-Tabs, Remote Commands oder Crash-/Diagnoselogs;
- Prozessende vernichtet private Sessionmetadaten;
- Background-/Snapshot-Privacy-Cover schützt sichtbare private Inhalte.

### A4 – Downloads, Uploads, Popups und Teilen

Schließe die produktiven Pfade für:

- echten File-Provider-Upload und Abbruch;
- normale und private authentifizierte Downloads über den auslösenden
  WebKit-Datastore;
- sichere Dateinamen, kollisionsfreie Ziele, Fortschritt und Cancel;
- unvollständige Downloads bei Background, Prozessende und Fehler;
- Quick Look und Share Sheet;
- `target=_blank`/Popup mit korrekter Origin-, Tab- und Moduszuordnung;
- private Artefakte ohne Persistenz- oder Verlaufleck.

### A5 – Berechtigungen, Dialoge und externe Übergaben

Schließe Allow, Deny, Cancel und Wiederholung für:

- Kamera;
- Mikrofon;
- Kamera und Mikrofon kombiniert;
- Motion;
- JavaScript Alert, Confirm und Prompt;
- Dateiauswahl;
- erlaubte externe App-Schemes mit sichtbarer Origin und sichtbarem Ziel.

Mainframe und Subframe müssen dem auslösenden Tab und Ursprung korrekt
zugeordnet bleiben. Lokale, skriptbasierte, credentialtragende und unbekannte
Schemes werden ohne Übergabe abgelehnt.

### A6 – Fehler, Lifecycle und Recovery

Schließe deterministisch:

- offline, DNS, TLS und HTTP-/WebContent-Fehler;
- Background, Foreground, Prozessende und Session-Flush;
- Memory Pressure und Discard;
- unvollständige Downloads;
- persistente Schreibfehler und korrupte/inkompatible Sessiondaten;
- Accountwechsel, CloudKit-Zone-/Subscription-Verlust und Tombstone-
  Wiederholung;
- verständliche, nicht blockierende Wiederaufnahme ohne Datenverdopplung.

### A7 – UI, Motion und Accessibility

Schließe vor Feature Freeze alle erkennbaren UI-Inkonsistenzen:

- Harbor Deck zeigt expanded und compact eine stabile Hierarchie;
- der untere Adressbereich blendet bei Dokument- und Nested-Scroll
  konsistent ein und aus;
- Jitter kollabiert nicht, bewusste Gegenbewegung expandiert;
- Suche, Alert, Permission, File Picker, Seitenwechsel, Laden und Fehler
  expandieren bei Bedarf deterministisch;
- Animationen verwenden eine kleine konsistente Timing-/Easing-Sprache;
- Reduce Motion ersetzt räumliche Bewegung durch unmittelbaren Zustand oder
  zurückhaltendes Crossfade;
- Reduce Transparency, erhöhter Kontrast, Hell/Dunkel und Website-Tint bleiben
  lesbar;
- Dynamic Type verursacht keine abgeschnittenen Hauptaktionen;
- VoiceOver-Reihenfolge, Labels, Values, Traits, Escape und Alternativen zu
  Gesten sind vollständig;
- iPad Compact/Regular Width, Rotation, Multitasking, Hardwaretastatur und
  Pointer sind produktiv bedienbar.

Nutze die vorhandenen lokalen PNG-Referenzen unter
`docs/design/2026-08-30-mobile-directions/`. Erzeuge keine neue Designwelle,
solange keine konkrete Layoutfrage ungelöst ist.

### A8 – lokale Sync- und Remote-Verträge

Schließe alle ohne externe Apple-Freigabe kontrollierbaren Teile:

- lokaler Sync-Opt-in/-out und Persistenz;
- verschlüsselte Records und private Datenklassen-Ausschlüsse;
- Inbox/Outbox, Offline Queue, Retry, Konflikt und Tombstone;
- Device Approval, Revocation, Accountwechsel und Schlüsselrotation;
- signierte Remote Commands für genau ein Zielgerät;
- TTL, Replay-, Wrong-Target- und Invalid-Signature-Rejection;
- kein Shell-, Bulk- oder beliebiges Scheme-Kommando;
- stabile Wire-Kompatibilität mit dem Mac-Gegenpart.

Realer Transport bleibt Phase D, aber alle lokalen Zustandsmaschinen,
Negativverträge und sicheren Fehlerpfade müssen vor Feature Freeze vollständig
sein.

### A9 – Performance-, Energie- und Lifecycle-Budgets

Implementiere beziehungsweise instrumentiere reproduzierbare lokale Messungen
für:

- Cold und Warm Launch;
- 1, 5 und 20 Tabs;
- Speicherentwicklung und Discard/Restore;
- Idle-CPU, Wakeups und Netzwerkaktivität;
- Background/Foreground und Session Flush;
- Scroll-/Animation-Stabilität mit und ohne Reduce Motion.

Busy-Host-Messungen sind Diagnose, keine Release-Evidenz. Überschrittene
Budgets müssen entweder behoben oder mit reproduzierbarer Begründung fail
closed bleiben.

### A10 – Dokumentation und Evidenz-Infrastruktur

Vor Feature Freeze:

- korrigiere die dokumentierte Development-CloudKit-Wahrheit, ohne fremde
  Desktop-Änderungen zu überschreiben;
- harmonisiere `docs/IOS_BROWSER_E2E_EVIDENCE.md`, `docs/SYNC.md`,
  `docs/RELEASING.md`, `docs/THREAT_MODEL.md`, Product Config und
  `config/external-gates.json` nur nach gezielter Ownership-Prüfung;
- erzeuge einen kandidatgebundenen, hashbaren Evidenzpfad unter
  `artifacts/e2e/<candidate-id>/`;
- sorge dafür, dass Result Bundles, Logs, Screenshots/Videos, Signaturdaten,
  Entitlements, Geräte-/OS-Daten und Exitcodes manifestiert werden;
- lasse `config/test-registry.json` den unveränderten Anforderungskatalog
  darstellen; echte Ergebnisse gehören in das Kandidatenmanifest.

## Feature-Freeze-Gate

Ein finaler Kandidat darf erst gebaut werden, wenn alle folgenden Punkte grün
sind:

1. A1 bis A10 sind vollständig reviewt; jede Lücke ist implementiert oder als
   wirklich externer Gate klassifiziert.
2. Keine Mobile-`TODO`-, `FIXME`-, Dummy-, Demo- oder Placeholder-Pfade
   verstecken eine erforderliche Journey.
3. Alle Mobile-Swift-Dateien erfüllen Swift 6 Strict Concurrency und das
   800-Zeilen-Limit.
4. Gezielte Unit-/Integration-/Static-Checks für neue Implementierung sind
   grün; dies ist noch keine E2E-Abnahme.
5. Mobile-Pfade sind sauber committed und DCO-signiert.
6. Der Feature-Freeze-Commit, die generierte Projektkonfiguration und die
   Buildnummer sind unveränderlich notiert.
7. Ein neuer kandidatgebundener Evidenzordner und ein anfängliches Manifest
   existieren.

Erst danach beginnt Phase B.

## Phase B – vollständige sichtbare E2E-Abnahme

### B1 – exakten Kandidaten erzeugen

- XcodeGen deterministisch zweimal ausführen und No-op beweisen;
- Debug-/Release-Simulatorvarianten für iPhone und iPad bauen;
- Kandidaten-ID aus Version, Buildnummer und Feature-Freeze-SHA bilden;
- `AHOI_SOURCE_COMMIT` im erzeugten Produkt muss exakt diesem SHA entsprechen;
  `NOT_AVAILABLE`, ein Platzhalter oder ein älterer Produkt-SHA macht den
  Kandidaten ungültig;
- Bundle, Binärdatei, Toolchain, Projektinputs und Buildprodukte hashen;
- Simulatoren auf definierten Zustand bringen, ohne fremde Daten zu löschen;
- deterministische Loopback-Fixtures für Auth, Popup, Upload, Download,
  Permission, Offline, TLS-/Failure-Seams und externe Schemes starten.

### B2 – `MOB-USER-01` bis `MOB-USER-15`

Führe auf demselben Feature-Freeze-Kandidaten vollständig sichtbar aus:

1. Kaltstart, Adresse, HTTPS und sichtbare Origin;
2. Suche, Ergebnisnavigation, Back/Forward/Reload/Stop;
3. Tab Create/Reorder/Rename/Close/Undo/Terminate/Restore;
4. Workspace Save/Move/Reopen aus Baum und Suche;
5. private Cookie-/Storage-/Session-/History-/Sync-Trennung inklusive
   Prozess-Tod;
6. pre-grant sichere externe URL-Behandlung und Deduplizierung; echtes
   Systemdefault-Routing bleibt bis zum Grant separat blockiert;
7. Upload, Download, Cancel, Quick Look, Share und Popup;
8. Permissions, Dialoge, File Picker und externe App-Bestätigung;
9. Rotation, Dynamic Type, VoiceOver, Kontrast, Reduce Motion und Reduce
   Transparency;
10. iPad Sidebar, Multitasking, Tastatur, Pointer, Rotation und Reorder;
11. Offline, TLS, WebContent, Memory, Prozessende und Download-Recovery;
12. lokale/candidate-preparable Sync-Projektion und private Ausschlüsse; realer
    Cross-Device-Transport folgt Phase D;
13. unsichere Scheme-Rejection und erlaubte externe Übergabe;
14. 1/5/20 normale und private Tabs;
15. visuelle Konsistenz über iPhone/iPad, normal/privat, Hell/Dunkel,
    Website-Tint und Accessibility-Modi.

Nutze XCUI beziehungsweise sichtbare Computer-Use-Bedienung. Ein Test muss die
reale Oberfläche treffen; direkte Modellmutation oder eine unsichtbare
Test-API ist keine sichtbare E2E-Journey.

### B3 – Red-Journey-Regel

Bei einem roten sichtbaren Pfad:

1. Fehler und exakten Kandidaten sichern;
2. kleinste tragfähige Ursache beheben;
3. neuen Source-SHA und neue Kandidaten-ID erzeugen;
4. zuerst genau die betroffene sichtbare Journey wiederholen;
5. anschließend alle davon abhängigen Journeys wiederholen;
6. vor Abschluss die vollständige kritische E2E-Matrix erneut ausführen.

Kein programmatischer Test darf einen roten sichtbaren Pfad überstimmen.

## Phase C – programmatische Final-Gates und Release-Evidenz

Erst nach der grünen sichtbaren Phase B:

- vollständige `AhoiMobileCoreTests`;
- vollständige CloudKit-/Security-Package-Tests;
- alle Mobile-Repository- und Signing-Verträge;
- Swift-6-/Strict-Concurrency-Builds;
- `xcodebuild analyze`;
- Debug- und Release-Buildmatrix für iPhone-/iPad-Simulator;
- fünf positive und negative Signing-/Preflight-Modi;
- XcodeGen-Determinismus;
- Mobile-Zeilenbudget;
- Secret Scan mit vollständiger Redaction;
- DCO, `git diff --check` und Evidenzvalidator;
- Performance-/Energie-/Lifecycle-Messungen auf sauberem Host;
- Backup-/Migration-/No-op-Zweitlauf für unterstützte Vorgängerstände.

Nach einer größeren Korrektur in Phase C wird ein neuer Kandidat erzeugt und
die betroffene sichtbare Phase-B-Journey zuerst erneut ausgeführt.

Retainiere danach das vollständige Kandidatenmanifest samt Hashes, Exitcodes,
Geräten, OS-Versionen, Videos/Screenshots, Result Bundles und bekannten
Grenzen. Transiente `/private/tmp`-Artefakte allein reichen nicht.

## Phase D – externe Gate-Leiter

Externe Gates beginnen erst, wenn Phase A bis C für einen Kandidaten grün
sind. Arbeite sie in dieser Reihenfolge ab. Wiederhole einen unveränderten
externen Versuch nicht endlos; sichere den exakten Grenzpunkt und bearbeite
parallel alle unabhängigen kontrollierbaren Punkte weiter.

### D1 – physisches iPhone und CloudKit Development

- `Servusla` entsperrt, verbunden und als einziges Zielgerät verifizieren;
- exakten `CloudKitDevelopment`-Kandidaten installieren;
- Signatur, Profil, Team, Bundle, Container, Environment und Keychain-Gruppen
  am installierten Kandidaten prüfen;
- guarded Real-Mutation-Harness mit explizitem Opt-in und frischem UUID-Token
  ausführen;
- verschlüsselten Ownership-Marker und Active Record schreiben/readbacken;
- Tombstone schreiben/readbacken;
- Server-/Log-Privacy negativ prüfen;
- nur marker-authentifizierten Scope idempotent bereinigen;
- relevante sichtbare iPhone-Journeys auf exakt dieser Installation
  wiederholen.

Führe danach auf demselben installierten pre-grant Kandidaten alle ohne
Default-Browser-Grant ausführbaren `MOB-USER-*`- und `IOS-*`-Gerätejourneys
aus. Datei-Provider, TCC, VoiceOver, externe Apps, Background/Memory Pressure
und Systeminteraktionen dürfen menschlich assistiert werden. `MOB-USER-06` und
`IOS-04` bleiben bis zum tatsächlichen Apple-Grant ausdrücklich am
Systemdefault-Teil blockiert.

Ein gesperrtes Gerät, abgelaufene Session oder fehlende Berechtigung ist ein
externer Gate, kein PASS und kein Produktfehler.

### D2 – kompatibles physisches iPad

- vorhandenes autorisiertes iPad mit iPadOS 26+ verwenden;
- keinen Kauf oder fremde Hardwareorganisation voraussetzen;
- Kandidaten installieren und iPad-spezifische `MOB-USER-09`, `10`, `14`,
  `15` sowie relevante `IOS-*` sichtbar ausführen;
- existiert kein kompatibles Gerät, `PHYSICAL_IPAD_REQUIRED` mit Modell,
  OS-Grenze und letztem erreichbaren Schritt belegen.

### D3 – Mac–iPhone–iPad Development Sync

- Apple-Development-signierten Mac-Gegenpart mit demselben Development-
  Container und kompatiblem Wire-/Key-Vertrag bereitstellen;
- `SYNC-01` bis `SYNC-27` sowie `IOS-01` bis `IOS-15` real ausführen;
- Offline Queue, Konflikt, Tombstone, Replay, Wrong Target, Invalid Signature,
  Approval, Revocation, Accountwechsel, Zone Recovery und Key Rotation prüfen;
- normale Tabs/Workspaces/Verlauf synchronisieren und private/verbotene Daten
  negativ ausschließen;
- Push-/Background-Reconciliation ohne Polling-Schleife belegen.

Der benannte Mac–iPhone–iPad-Roundtrip benötigt mindestens diese drei
Gerätetypen. Der vollständige `SYNC-01`-bis-`SYNC-27`-Releasevertrag wird mit
zwei passend signierten Macs und mindestens einem realen iPhone/iPad
ausgeführt; fehlende Hardware wird exakt getrennt ausgewiesen.

### D4 – Production, Archive und interner TestFlight

- Production-Schema erst nach grüner Development-Matrix promoten;
- `TestFlightBootstrap`-Archive ohne Default-Browser-Entitlement erzeugen;
- Archive, Export, Signatur, Profile, Entitlements, Source-SHA, Buildnummer und
  Hash belegen;
- Developer-ID-provisionierten/notarisierten Mac-Production-Gegenpart prüfen;
- App Store Connect Record, Metadaten, Privacy und erforderliche Verträge
  vervollständigen;
- Export-Compliance, Trader-, Rechts-, Steuer- und öffentliche
  Produktangaben niemals raten; sie benötigen eine bestätigte menschliche
  Aussage;
- Build hochladen, Verarbeitung abwarten und internen TestFlight-Build auf
  iPhone/iPad installieren;
- kritische Browser-, Migration-, Sync- und Privacy-E2E-Journeys mit den
  installierten Store-Bits wiederholen.

GitHub-hosted Actions mit null ausgeführten Schritten wegen Billing bleiben
unverfügbare Infrastruktur. Nutze lokale Gates oder bestehende autorisierte
self-hosted Runner; fordere kein bezahltes Billing an.

### D5 – öffentlicher TestFlight und Default-Browser-Antrag

- externe Beta Review und öffentlichen TestFlight-Link real abschließen;
- erst mit verarbeitetem öffentlichem Link den Managed Default Browser Antrag
  stellen;
- Antragsstatus dokumentieren, aber Warten nicht als Fortschritt ausgeben;
- Ablehnung oder Rückfrage mit unverändertem Antrag nicht in Schleife
  wiederholen.

### D6 – Grant und post-grant Kandidat

- nach tatsächlichem Grant neue Profile und Buildnummer erzeugen;
- `ReleasePostGrant`-Archive bauen, signieren, hochladen und über TestFlight
  installieren;
- Systemdefault auf echtem iPhone und kompatiblem iPad setzen;
- `MOB-USER-06` und `IOS-04` erstmals vollständig real ausführen;
- vollständige kritische Browser-/Sync-/Accessibility-/Privacy-Regression
  wiederholen;
- installierte Store-Build-ID, Receipt und Processing-Metadaten mit dem Upload
  verbinden, ohne falsche Bytegleichheit zu behaupten.

### D7 – öffentlicher Release

Eine öffentliche App-Store-Einreichung oder Veröffentlichung erfolgt nur nach
separater ausdrücklicher Nutzerfreigabe. Nach Veröffentlichung Store-Build
installieren und kritischen sichtbaren Smoke erneut ausführen.

## Externe versus kontrollierbare Blocker

Als extern gelten nur nach realem Versuch belegte Grenzen wie:

- gesperrtes oder nicht verfügbares physisches Gerät;
- kein kompatibles physisches iPad;
- fehlende Apple-Rolle, Portalberechtigung, Vereinbarung oder Trader-Angabe;
- ausstehende Beta Review, Processing oder Managed-Entitlement-Entscheidung;
- fehlende menschliche Bestätigung für rechtliche/öffentliche Angaben;
- nicht autorisierte öffentliche Veröffentlichung.

Nicht extern sind:

- fehlender Code, Test, Fixture oder Dokumentation;
- unstabile UI, Crash, Datenverlust oder Test-Harness-Fehler;
- fehlende lokale Build-/Analyze-/Signing-Verträge;
- fehlendes Kandidatenmanifest;
- vermeidbare Simulator-, XcodeGen-, DCO-, Secret- oder Zeilenbudgetfehler;
- ein roter sichtbarer E2E-Pfad.

## Definition of Done

AhoiBrowser Mobile ist erst vollständig abgeschlossen, wenn:

1. A1 bis A10 ohne kontrollierbare Restlücke abgeschlossen sind;
2. `MOB-USER-01` bis `MOB-USER-15` auf demselben finalen Kandidaten sichtbar
   grün oder ausschließlich am exakt belegten externen Teilgate blockiert sind;
3. alle unabhängigen Phase-C-Gates auf diesem Kandidaten grün sind;
4. physisches iPhone, physisches iPad, CloudKit Development,
   Mac–iPhone–iPad, Production, Archive, interner/öffentlicher TestFlight,
   Default-Browser-Antrag, Grant und post-grant E2E jeweils als PASS,
   BLOCKED_EXTERNAL oder NOT_AUTHORIZED mit Beleg klassifiziert sind;
5. kein externer Gate eine unabhängige kontrollierbare Aufgabe verdeckt;
6. die dauerhafte Evidenz Commit, Build, Signatur, Profile, Entitlements,
   Geräte, OS, Resultate, Hashes, Privacy-Negativbeweise und Cleanup bindet;
7. alle eigenen Änderungen fokussiert, DCO-signiert und ohne Force-Push auf
   dem gemeinsamen Remote-Branch liegen;
8. der Abschlussbericht eine knappe Wahrheitsmatrix liefert: umgesetzt,
   Feature Freeze, sichtbares E2E, Programmatik, Simulator, physisches iPhone,
   physisches iPad, CloudKit Development, Mac–iPhone–iPad, CloudKit
   Production, Archive, interner TestFlight, öffentlicher TestFlight,
   Default-Browser-Antrag, Grant, post-grant E2E und optionaler Release.

Ein externer Restgate darf als solcher offen bleiben. Eine kontrollierbare
Feature-, E2E-, Security-, Recovery-, Performance-, Dokumentations- oder
Evidenzlücke darf nicht als „fertig“ bezeichnet werden.
