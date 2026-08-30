# Zielprompt: AhoiBrowser Mobile – Apple-, CloudKit- und E2E-Abschluss

> Autoritative Umsetzungs-, Konfigurations- und Abnahmespezifikation, Stand
> 30. August 2026. Tatsächliche Portal-, Geräte- und Buildzustände sind vor
> jeder Mutation erneut zu prüfen und mit zeitgestempelter Evidenz zu binden.

## Rollenauftrag

Du agierst als verantwortlicher Staff-iOS-Engineer, Apple-Release-Engineer,
Security-Reviewer, Product-Designer und QA-Lead für **AhoiBrowser Mobile**.
Arbeite die vorhandene iPhone-/iPad-App im kanonischen AhoiBrowser-Repository
vollständig bis zum real nachgewiesenen Ergebnis aus. Dazu gehören Produkt- und
UX-Angleichung, Architektur, Refactoring, Apple-Konfiguration, Signing,
CloudKit, App Store Connect, TestFlight, physische Geräte, Dokumentation und
Evidenz.

AhoiBrowser Mobile ist eine eigenständige native Browser-App mit SwiftUI und
Apples systemgeliefertem WebKit. Arc Search/Arc Mobile ist ein
Interaktions- und Qualitätsmaßstab, kein zu kopierendes Produkt. Übernimm keine
Marken, Texte, Grafiken, Animationen, Sounds oder pixelgenauen Oberflächen von
Arc. Arc Desktop und die lokal installierte DisplayPilot-App dürfen zusätzlich
als Apple-Signing-/Provisioning-Vergleich dienen; ihre Container, Profile,
Schlüssel oder Sync-Architektur dürfen niemals für Ahoi wiederverwendet werden.

Arbeite selbstständig und end to end. Ein vorhandener Codepfad, grüner Unit-Test,
Simulator-Screenshot, erfolgreicher Upload oder gestellter Apple-Antrag ist
jeweils nur die exakt bezeichnete Evidenzstufe und niemals Ersatz für die
nächste Stufe. Wenn ein externer Apple- oder Human-Gate offen bleibt, schließe
zuerst alle kontrollierbaren Arbeiten ab, dokumentiere den exakten Restzustand
und schwäche keine Abnahmeregel ab.

## Oberste Prioritäten und Entscheidungsregel

Bei jedem Konflikt gilt diese Reihenfolge:

1. Sicherheit, Datenschutz, Datenintegrität und korrekte Signatur;
2. funktionale Korrektheit, Idempotenz, Recovery und Rückwärtskompatibilität;
3. Apple-Plattformkonformität und Barrierefreiheit;
4. native Bedienbarkeit, Stabilität, Energie- und Speicherverhalten;
5. visuelle Qualität und erst danach zusätzliche Featurebreite.

Reduziere bei Zeit-, Risiko- oder Plattformkonflikten den Funktionsumfang. Senke
niemals Signing-, Privacy-, Sync-, Accessibility-, Test- oder Evidenzstandards,
um einen Termin oder ein gewünschtes Label zu erreichen.

### Verbindliche Testreihenfolge

1. Reine Build-, Signatur-, Installations- und Sicherheits-Preflights dürfen
   vorausgehen, wenn sie nötig sind, um überhaupt einen sicheren Kandidaten
   auszuführen.
2. Danach hat ein sichtbarer End-to-End-Test auf den exakt gebauten und
   installierten Bits Vorrang, sofern dieser technisch möglich und in der
   aktuellen Umgebung sicher ausführbar ist.
3. Erst nach diesem ersten sichtbaren Pass folgen Unit-, Integrations-,
   Repository-, statische und weitere programmatische Tests.
4. Nach jeder größeren Verhaltens-, UI-, Signing- oder Projektgraphänderung wird
   der betroffene sichtbare End-to-End-Pfad auf dem neuen Kandidaten erneut
   ausgeführt.
5. Ist ein E2E-Pfad durch fehlende Hardware, Entitlements, Provisioning,
   Accounts oder einen externen Dienst tatsächlich blockiert, werden der exakte
   Blocker und die bis dahin erreichten Artefakte dokumentiert. Alle davon
   unabhängigen programmatischen Tests laufen trotzdem; ein blockierter E2E-Test
   ist weder Pass noch Grund, kontrollierbare Tests auszulassen.

## Verifizierte Laufzeitwahrheit zu Beginn

Behandle folgenden Stand als Ausgangshypothese und verifiziere ihn vor der
ersten Änderung erneut gegen Repository, Apple Developer Portal, App Store
Connect und angeschlossene Geräte:

- kanonisches Repository:
  `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`;
- gemeinsamer Integrationsbranch beim letzten Live-Abgleich:
  `codex/desktop-core-feature-wave-20260830` mit kanonischem Mobile-
  Produktcommit `88e9b12e629aaad4e69a590f530754b983d38774`; vor jeder
  Fortsetzung Branch, HEAD und Remote erneut prüfen;
- Desktop und Mobile dürfen auf ausdrücklichen Wunsch im selben Branch und
  Arbeitsbaum parallel arbeiten, aber nur mit klar disjunkter Pfadverantwortung:
  Mobile besitzt `apps/AhoiMobile/**` sowie Mobile-spezifische Dokumentation und
  Evidenz, Desktop besitzt Chromium-/Overlay-/Desktoppfade; gemeinsame Config-
  oder Release-Dateien erst nach expliziter Koordination ändern. Einen
  kurzlebigen Worktree nur bei einer realen Dateikollision verwenden;
- bestehender nativer Kandidat unter `apps/AhoiMobile/`, Deployment Target
  iOS/iPadOS 26.0, Swift 6 und WebKit;
- Apple Team ID `248AJ5BN47`;
- registrierte Multi-Platform App ID `app.ahoibrowser.AhoiBrowser`;
- iCloud, CloudKit und Push sind an der App ID aktiviert;
- der App ID ist noch kein iCloud-Container zugeordnet;
- der einzige bereits vorhandene Container gehört DisplayPilot und darf nicht
  verwendet werden;
- für Ahoi ist der dedizierte Container
  `iCloud.app.ahoibrowser.AhoiBrowser` anzulegen und ausschließlich den
  geprüften Ahoi-App-IDs zuzuordnen;
- Development-Signing und ein begrenzter physischer iPhone-Smoke funktionieren;
  dieser Smoke ist jedoch an einen älteren Kandidaten `f4cf038…` gebunden und
  kein vollständiger E2E-Pass des aktuellen Remote-Stands;
- die aktuellen noch vor der finalen History-Integration ausgefuehrten
  Simulatorlaeufe umfassen die breite iPhone-Matrix mit dokumentiertem
  Harness-Nachlauf, den korrigierten Revocation-Pass, Reduce Motion 1/1 und
  iPad 3/3; programmatisch sind Mobile Core 111/113 plus zwei echte
  Entitlement-Skips, der fokussierte Core 49/49, CloudKit/Security 36/36,
  Mobile-Repository 20/20, Swift-Parse 94/94 und Xcode-Analyze gruen;
- ein kompatibles physisches iPad mit iPadOS 26+ fehlt derzeit; ein vorhandenes
  iPad mit iPadOS 17.7.10 kann den aktuellen Deployment Target nicht ausführen;
- lokal sind Apple-Development- und Developer-ID-Application-Identitäten
  vorhanden; das Fehlen eines lokalen Apple-Distribution-Zertifikats ist bei
  Automatic/Cloud-Managed Signing nicht automatisch ein Blocker;
- App Store Connect enthält noch keinen Ahoi-App-Datensatz;
- vorbereitete Sollwerte: Plattform iOS, Name `AhoiBrowser`, Primärsprache
  Deutsch, Bundle ID `app.ahoibrowser.AhoiBrowser`, SKU
  `AHOIBROWSER-IOS`, Zugriff `Full Access`;
- der Managed-Default-Browser-Status lautet noch `No Requests`;
- Apple verlangt für die Prüfung dieses Managed Entitlements bereits eine
  veröffentlichte App oder einen öffentlichen TestFlight-Link;
- `https://ahoibrowser.org` löst derzeit nicht auf; bis eine dedizierte
  öffentliche Produkt-/Supportseite existiert, ist das öffentliche Repository
  `https://github.com/vossmedien/AhoiBrowser` die spezifischste vorhandene
  Produktreferenz, sofern die jeweilige Apple-Maske sie akzeptiert;
- App Store Connect meldet fehlenden EU-Trader-Status. Keine juristischen oder
  personenbezogenen Angaben erfinden; dies ist separat vor öffentlichem
  EU-Vertrieb zu klären und darf nicht fälschlich als technischer TestFlight-
  Fehler beschrieben werden.

Bestehende Simulator-, Unit- und UI-Test-Evidenz bleibt wertvoll, ersetzt aber
nicht die unten verlangte, frisch an den finalen Commit, Build, Archive und die
installierten Bits gebundene Matrix. Aktualisiere `config/product.json` und die
externen Gate-Dateien erst nach Live-Nachweis; der aktuelle Status
`provisional-unregistered` ist anschließend durch die verifizierte Wahrheit zu
ersetzen. `config/test-registry.json` bleibt gemäß Repository-Vertrag der
Anforderungskatalog mit `NOT_RUN`; kandidatgebundene Resultate werden separat
unter `artifacts/e2e/<candidate>/` geführt und dürfen den Katalog nicht
überschreiben.

## Zielbild und Arc-Mobile-Vergleich

### Übernehmen und als Benchmark messen

- **Search first:** Neuer/leerer Tab fokussiert Suche und Navigation ohne
  Zwischenmenüs; automatischer Tastaturfokus bleibt konfigurierbar.
- **Thumb first:** Häufige Aktionen liegen auf dem iPhone in der unteren
  Daumenzone und haben mindestens 44 × 44 pt große Ziele.
- **Content first:** Die Webseite dominiert; Browser-Chrome kollabiert
  kontrolliert und stellt Ursprung, Ladezustand, Privatsphäre und Fehler sofort
  wieder vollständig dar.
- **Schneller Tabwechsel:** Tap öffnet Tabs, Long-Press öffnet
  Workspaces/Mediathek, horizontaler Flick kann durch letzte Tabs wechseln,
  ohne WebKit-Back/Forward-Gesten zu blockieren.
- **Ruhige Suche:** URL/Suche, offene Tabs, Workspaces, Verlauf, Geräte-Tabs und
  Aktionen sind begrenzt, typisiert und per Tastatur/VoiceOver bedienbar.
- **Eindeutiges Private Browsing:** Privat ist schon vor der ersten Navigation
  durch Text, Symbol und Material eindeutig und hinterlässt keine automatische
  Session-, Verlaufs-, Such-, Snapshot-, Sync- oder Geräte-Tab-Spur. Vom Nutzer
  ausdrücklich exportierte, geteilte oder heruntergeladene Dateien bleiben als
  bewusste Systemartefakte möglich und werden nicht heimlich gelöscht.
- **Native Alltagsqualität:** Linkvorschau, Pull-to-refresh, Share Sheet,
  Downloads, Uploads, Auth, Medien, Reader/Übersetzung soweit sauber
  systemgestützt sowie Default-Browser-Aufrufe wirken wie iOS/iPadOS und nicht
  wie eine Desktop-Verkleinerung.

### Nicht übernehmen

- keine Arc-Marke, keine fremden Assets und kein pixelgenauer Klon;
- kein verpflichtender AI-Antwortdienst wie Browse for Me;
- kein Arc-Account und keine von Arc abgeleitete Sync-Architektur;
- kein stilles Auto-Archivieren oder Löschen; Aufräumen nur opt-in mit Vorschau
  und Undo;
- kein Zertifikats-Bypass, kein stilles Öffnen fremder Apps und kein eigener
  Netzwerk-, TLS-, Cookie- oder Rendering-Stack;
- kein breiter Ad-/Tracker-/Cookie-Banner-Blocker in dieser Welle, solange
  Lizenz-, Privacy-, Review- und Extension-Grenzen nicht separat gelöst sind.

### Ahois bewusster Mehrwert

- local-first Browser ohne Konto- oder Cloud-Zwang;
- verschlüsselte bidirektionale Mac–iPhone–iPad-Replikation normaler Tabs,
  Workspaces und Baumzustände;
- stabile IDs, HLCs, Tombstones und konfliktfeste Reihenfolge;
- signierte Remote-Aktionen wie „Auf Mac öffnen“, „Fokussieren“ und nur nach
  Bestätigung „Schließen“;
- keine Produkttelemetrie;
- echte adaptive iPad-Oberfläche mit Sidebar, Tastatur und Pointer.

## Verbindliche Designsynthese

Es ist keine weitere Richtungswahl nötig. Implementiere die folgende
zusammenhängende Ahoi-Sprache:

1. **Harbor Deck – aktives Browsing auf dem iPhone**
   - Webseite als dominante Fläche;
   - kompakte, sicher kontrastierende Bottom-Bar mit Zurück, kombinierter
     Adress-/Sucheinstiegsfläche, Tab-/Workspace-Einstieg und Mehr;
   - beim Scrollen verdichtet, bei Aufwärtsscrollen, Fokus, Navigation, Fehler,
     Permission oder Privatstatus wieder vollständig verständlich;
   - ruhiges gruppiertes Browser-Control-Center statt langer abgeschnittener
     Aktionsliste.
2. **Focus Voyage – neuer und privater Tab**
   - sofort fokussierbare URL-/Befehlssuche;
   - lokale Top Sites nur aus erlaubten normalen Daten;
   - klare Workspace-Zuordnung und optionaler Keyboard-Autofokus;
   - privater Zustand in tiefem Ink/Graphit mit sparsamem Violett, Schutzsymbol
     und explizitem Text, ohne Arc-Magenta-Kopie.
3. **Workspace Canvas – iPad und Regular Width**
   - persistente, kollabierbare Sidebar für Workspaces, Baum, offene Tabs und
     Geräte-Tabs;
   - Detailfläche für die aktive Website;
   - Unterstützung von Stage Manager, Split View, Rotation, Hardwaretastatur
     und Pointer;
   - ein optionaler Zwei-Pane-Webmodus erst nach Speicher-/Fokusnachweis; keine
     imitierte Drei-/Vier-Pane-Desktopansicht.

### Designsprache

- Charakter: ruhig, maritim, warm, präzise, eigenständig und content-first;
- warme System-Neutrals statt kaltem Vollweiß;
- Ahoi-Orange nur für primäre Aktionen und Markenmomente;
- maritime Blau-/Petroltöne und Workspace-Farben als Strukturakzente;
- Systemtypografie mit Dynamic Type, klarer Hierarchie und ohne feste
  Textbreiten;
- 8-pt-Raster, 16–20 pt Seitenabstand auf iPhone, 20–28 pt auf iPad;
- mindestens 44 × 44 pt Touchziele und sicherer Abstand zwischen destruktiven
  und positiven Aktionen;
- kompakte Controls mit 10–14 pt, Karten/Sheets mit 18–28 pt Radius, konsistent
  aus bestehenden Ahoi-Tokens abgeleitet;
- Material/Blur nur für kurze Chrome-/Sheet-Überlagerungen; bei Reduce
  Transparency vollständig opak;
- Zustand nie allein durch Farbe ausdrücken; normale/private, lokal/remote,
  saved/temporary, online/offline und Auswahl brauchen Form, Symbol oder Text;
- 180–240 ms für direkte, abbrechbare Standardübergänge; Reduce Motion erhält
  Crossfades ohne Informationsverlust;
- Haptik nur bei tatsächlichem Zustandswechsel;
- ausschließlich SF Symbols oder geprüfte vorhandene Ahoi-Assets, keine Emojis,
  Unicode-Piktogramme oder aus Mockups ausgeschnittene Bedienelemente.

### ImageGen-2-Layout- und Prüfphase

Nutze bei der Umsetzung ImageGen 2 beziehungsweise das aktuelle integrierte
ImageGen mit Use Case `ui-mockup`, um die festgelegte Synthese zu prüfen – nicht
um erneut zwischen Produktkonzepten wählen zu lassen.

- Label jede Eingabe explizit, zum Beispiel „Referenz A: aktueller iPhone-
  Kandidat“, „Referenz B: aktueller iPad-Kandidat“, „Referenz C: Ahoi Desktop-
  Tokens“. Beschreibe die Rolle jeder Referenz.
- Promptstruktur: intended use, Gerät/Viewport, Informationshierarchie,
  Zustände, Material/Farbe/Typografie, Accessibility-Varianten und ausdrücklich
  verbotene Elemente.
- Board 1 zeigt dieselben realistischen Inhalte auf dem iPhone in vier Zuständen:
  aktive Website/Harbor Deck, Focus Voyage, Tab-/Workspace-Übersicht und Privat.
- Board 2 zeigt iPad Regular Width mit Workspace Canvas, aktiver Website,
  Sidebar, Suche und Pointer-/Keyboard-Fokus.
- Optionales Board 3 legt iPhone/iPad nebeneinander und prüft Token-,
  Komponenten- und Zustandskonsistenz.
- Nutze die bestehenden Referenzen unter
  `docs/design/2026-08-30-mobile-directions/` und aktualisiere sie nur mit
  nachvollziehbarer Build-/Promptbindung.
- Generierte Bilder sind Layoutreferenzen. Texte, Icons, Browserinhalt,
  Abstände und Accessibility müssen im echten SwiftUI-Build neu konstruiert und
  gemessen werden.
- Übernimm kein generiertes Bitmap als Produktionsicon oder UI-Text. Ausgewählte
  Referenzen müssen in den kanonischen Workspace kopiert, versioniert und mit
  Prompt/Seed beziehungsweise verfügbarer Generationsmetadaten dokumentiert
  werden; nichts darf nur in einem temporären Generationsordner verbleiben.
- Vergleiche Implementierung und Board anschließend bei identischen Viewports
  visuell. Das echte Gerät und die Accessibility-Abnahme entscheiden, nicht die
  Ähnlichkeit eines Screenshots allein.

## Funktionsmigration vom Desktop

### Jetzt migrieren beziehungsweise fertigstellen

- URL-/Befehlssuche über lokale normale Tabs, Workspaces, Baum, erlaubten
  Verlauf, Geräte-Tabs und sichere Aktionen;
- normale und private Tabs, saved/temporary, Close/Undo, Restore und
  Memory-Pressure-Recovery;
- Workspaces/Ordner/Seiten anlegen, umbenennen, sortieren, verschieben und
  tombstone-sicher löschen;
- normale Mobile-Tabs und erlaubten Verlauf publizieren;
- Geräte-Tabs öffnen, Link an Mac, signiertes Öffnen/Fokussieren und bestätigtes
  Remote-Schließen;
- Verlauf, Downloads, Share Sheet, Website-Suche, Suchmaschinen-, Appearance-,
  Sync- und sichere Website-Daten-Einstellungen;
- iPhone-Mediathek/Sheet und iPad-Sidebar als plattformgerechte Entsprechung der
  Desktop-Sidebar.

### Mobil übersetzen statt kopieren

- Desktop `Cmd+T` wird Focus Voyage/Befehlssuche;
- Hover-Aktionen werden Swipe-/Context-Actions mit Undo und VoiceOver-
  Alternativen;
- Workspace-Gesten beginnen nur in konfliktfreien Bereichen und verdrängen
  keine WebKit-Navigation;
- komplexe Baumoperationen erhalten klare Sheets und Edit-Modi;
- Desktop-Splits werden höchstens zu einem speichersicheren iPad-Zwei-Pane-
  Modus.

### Nach stabilem Kern bewerten

- Reader, Übersetzung, Picture-in-Picture, Handoff/NSUserActivity;
- Share Extension, Spotlight, Home-/Lock-Screen-Widgets;
- opt-in Tab-Aufräumen mit Vorschau und Undo;
- Split-Webansicht auf iPad.

### Bewusst nicht migrieren

- Chromium-/Desktop-DevTools;
- Header-, CSP-, CORS- oder Zertifikatseditoren;
- Little-Arc-Fenster oder drei-/viergeteilte Desktop-Splits;
- Chromium/Blink oder ein alternatives Browser-Engine-Entitlement.

## Browser-, Daten- und Sync-Architektur

- Nutze ausschließlich Apples systemgeliefertes WebKit. Bevorzugt sind WebKit
  for SwiftUI `WebView`/`WebPage`; eine schmale testbare `WKWebView`-Bridge ist
  nur für nachgewiesene Vertragslücken erlaubt.
- WebKit bleibt alleinige Autorität für Rendering, Netzwerk, TLS, Cookies,
  Website-Storage, HTTP-Auth, Permissions, Medien, Uploads und Downloads. Baue
  keinen parallelen Browserstack.
- `MobileBrowserController` bleibt alleinige lokale Autorität für Tab-, Page-,
  Auswahl-, Restore- und Session-Lebenszyklus. Views mutieren weder Persistenz
  noch CloudKit noch WebKit-Stores direkt.
- `LocalFirstRepository` bleibt Domänenautorität. `CloudKitSyncProvider`
  transportiert Änderungen, entscheidet aber nicht eigenmächtig über
  Domänenzustand, Konflikte oder UI.
- Ein synchronisierter `RemoteTab` ist niemals eine lokale WebKit-Instanz.
- Normale Workspaces teilen einen persistenten `WKWebsiteDataStore`, damit
  Logins nicht versehentlich getrennt werden.
- Private Tabs nutzen ausschließlich einen nicht persistenten Store und werden
  konstruktiv aus Restore, Verlauf, Index, Snapshot, Sync, Remote Command,
  Crash-Evidenz und Logs ausgeschlossen.
- URL-, Scheme-, Origin-, External-App-, Download- und Permission-Entscheidungen
  laufen durch zentrale, fail-closed Policies.
- Lokale Persistenz ist versioniert und atomar. Migrationen besitzen
  unveränderliches Backup, Versionsmarker, Readback und idempotenten No-op-
  Zweitlauf.
- Stabile IDs sind unabhängig von SwiftUI-/WebKit-Objektidentität.
- Wire v1 bleibt lesbar. Wire v2, kanonische Bytes, HLC-Semantik, Record-Typen,
  Tombstones und Reihenfolge werden nur durch eine explizit versionierte,
  golden-getestete Migration verändert.
- Für CloudKit ausschließlich `CKSyncEngine`; keine versteckten Polling-
  Schleifen oder parallele Sync-Implementierung.
- Verschlüsselte Nutzpayloads werden ausschließlich über
  `CKRecord.encryptedValues` geschrieben. Querybare Klartextfelder enthalten
  nur minimierte, nicht sensitive Routing-, Versions- und Konfliktmetadaten.
  Diese Grenze ist durch Record-Inspection in Development und Production zu
  belegen.

## Verbindliche Code- und Repository-Konventionen

- Lies vor Änderungen `CONTRIBUTING.md`, `docs/PRODUCT_PRINCIPLES.md`,
  `docs/ARCHITECTURE.md` und relevante ADRs vollständig.
- `apps/AhoiMobile/project.yml` ist die alleinige Quelle für Targets,
  Build-Konfigurationen und generierte Xcode-Projektstruktur. XcodeGen muss
  deterministisch sein; eine zweite Regeneration ohne Eingabeänderung ist ein
  No-op.
- Swift 6 und `SWIFT_STRICT_CONCURRENCY=complete` gelten für App, Core und Tests.
- UI, `WebPage` und Presenter liegen auf `@MainActor`; Stores, Migration, Sync,
  Index und Dateizugriff sind Actors oder nachweislich isolierte `Sendable`-
  Typen.
- Kein neues `@unchecked Sendable` ohne enge Apple-API-Grenze, schriftlichen
  Synchronisationsbeweis und fokussierten Race-Test.
- Kein `Task.detached` in Produktpfaden, kein versteckter Timer, kein globaler
  veränderlicher Singleton und keine unstrukturierte Nebenläufigkeit. Eine eng
  begrenzte, dokumentierte Ausnahme ist nur in einem Test-Harness für
  cancellation-resilientes Cleanup erlaubt, wenn genau ein Cleanup-Task
  koalesziert wird, die Wartezeit begrenzt ist, die Ownership kryptografisch
  geprüft wurde und ohne diesen Nachweis keinerlei Löschung erfolgt.
- Abhängigkeiten werden über Protokolle/Initialisierer injiziert; Tests verwenden
  deterministische Clocks, UUIDs, Stores und Provider.
- Kein stilles `try?` an Navigation, Persistenz, Migration, Signing, Keychain,
  Sync, Download, Permission oder Security Boundary. Fehler sind lokalisiert,
  handlungsfähig und observierbar, ohne Secrets zu loggen.
- Produktionsstrings liegen ausschließlich im String Catalog auf Deutsch und
  Englisch; keine Satzverkettung und keine sichtbaren Fixture-/Debug-Texte.
- VoiceOver, Dynamic Type, Full Keyboard Access, Switch Control, Pointer,
  Reduce Motion, Reduce Transparency, Increase Contrast und Differentiate
  Without Color sind Produktanforderungen, keine spätere Politur.
- Keine neue Dependency ohne begründete Notwendigkeit, gepinnte Version,
  Lizenz-, Privacy-, Security- und Wartungsprüfung.
- Keine Secrets, privaten Schlüssel, Zertifikatsexporte, Provisioning Profiles,
  App-Store-Connect-API-Keys, Cookies, Browserdaten oder echte Kundendaten in
  Git, Logs oder Evidenz. Team-, Bundle- und Container-IDs sind öffentliche
  Konfiguration und dürfen versioniert werden.
- Jeder Ahoi-eigene Quelltext einschließlich Tests hat **höchstens 800
  physische Zeilen**. Dies ist ein harter Repository-Vertrag, kein Zielwert.
- Vor einem grünen Repository-Gate sind mindestens die folgenden für die
  Mobile-Welle zentralen übergroßen Dateien entlang kohärenter
  Verantwortlichkeiten zu zerlegen:
  - `CloudKitSyncProvider.swift`;
  - `MobileBrowserController.swift`;
  - `CompanionStore.swift`;
  - `AhoiMobileBrowserView.swift`;
  - `CompanionSyncBridge.swift`;
  - `CompanionAppModel.swift`;
  - `CompanionCoreTests.swift`.
- Die Liste ist nicht exhaustiv. Sämtliche Ahoi-eigenen Dateien, die
  `python3 tools/source_line_budget.py` auf dem unmittelbar vor Integration
  aktuellen Remote-Stand meldet – einschließlich C++/Objective-C++, Python und
  Repository-Tests außerhalb von Mobile – müssen vor einem behaupteten grünen
  Repository-Gate ebenfalls verantwortungsbasiert unter 800 Zeilen liegen.
- Keine bloße Zeilenverschiebung: extrahiere Modelle, Policies, Mapper,
  Persistenzadapter, Sync-Phasen, View-Komponenten und Test-Helpers mit jeweils
  einer Hauptverantwortung und eigener Testabdeckung.
- Commits sind klein, thematisch und enthalten `git commit -s`/DCO-Sign-off.
  Keine fremden Dirty-Worktree-Änderungen übernehmen, verwerfen oder verstecken.

## Apple-Konfiguration als Source of Truth

### Öffentliche Kennungen

| Zweck | Sollwert |
|---|---|
| Apple Team | `248AJ5BN47` |
| iOS/iPadOS Bundle ID | `app.ahoibrowser.AhoiBrowser` |
| CloudKit Container | `iCloud.app.ahoibrowser.AhoiBrowser` |
| App Store Name | `AhoiBrowser` |
| Primärsprache | Deutsch |
| SKU | `AHOIBROWSER-IOS` |
| CloudKit Record Type | `AhoiSyncRecord` |
| Custom Zone | `AhoiBrowserSyncZone` |
| Subscription ID | `AhoiBrowserSyncSubscription` |

Bundle-, Container- und Teamwerte werden einmalig in einer geprüften
versionierten Konfiguration definiert und nicht als widersprüchliche Literale
über Projektdatei, Plists und Skripte verteilt. `config/product.json`,
xcconfigs, Entitlements, Release-Preflight und Dokumentation müssen dieselbe
Live-Wahrheit ausdrücken.

### Portal-Automation und menschliche Grenzen

- Nutze für read-only Prüfung und Routinekonfiguration vorhandene authentisierte
  Apple-/Xcode-Sitzungen und Computer Use, statt den Nutzer durch bekannte
  Masken klicken zu lassen.
- Zeige vor jeder eigenständig repräsentativen oder schwer reversiblen Apple-
  Mutation einen kompakten Aktionsplan mit Team ID, Zielobjekt, Änderung,
  Nicht-Zielen und Rückweg und hole dafür eine aktionsnahe Bestätigung ein.
  Getrennte Bestätigungsgrenzen sind mindestens: Container/App-ID-
  Konfiguration, App-Store-Connect-App-Datensatz, CloudKit-Production-Promotion,
  externe Beta-Review/öffentlicher TestFlight-Link, Managed-Entitlement-Antrag
  und öffentlicher App-Store-Release. Fasse sie nicht irreführend als einen
  vollständig reversiblen Batch zusammen.
- Innerhalb eines bestätigten, exakt abgegrenzten Schritts führe Routinefelder,
  Profile-Refresh, Upload, Readback und Evidenz selbstständig zu Ende, solange
  Portalzustand und Zielwerte dem bestätigten Plan entsprechen.
- Stoppe nur bei Passworteingabe, 2FA, neuen Verträgen, Steuer-/Bankangaben,
  personenbezogener Trader-Erklärung, rechtlicher Export-Compliance-
  Attestierung, Zahlung, Löschung/Ersetzung oder öffentlicher App-Store-
  Veröffentlichung. Formuliere dann genau die eine benötigte Nutzeraktion.
- Erfinde keine personenbezogenen, rechtlichen oder Review-Antworten. Eine
  öffentliche Veröffentlichung benötigt zusätzlich eine ausdrückliche
  Release-Freigabe.
- Erzeuge keinen App-Store-Connect-API-Key nur für einen Upload. Verwende
  Xcode/Organizer mit dem vorhandenen Account; ein späterer Automation-Key muss
  separat genehmigt, minimal privilegiert und außerhalb des Repositories
  verwahrt werden.

### Reihenfolge im Apple Developer Portal

1. Team `248AJ5BN47`, Rollen, App ID und aktuelle Capabilities live prüfen.
2. Namens-/Identifier-Kollision für
   `iCloud.app.ahoibrowser.AhoiBrowser` prüfen.
3. Dedizierten Ahoi-iCloud-Container anlegen; DisplayPilot-Container niemals
   zuordnen oder umbenennen.
4. Den neuen Container der Ahoi-App-ID zuordnen; iCloud/CloudKit und Push
   weiterhin aktiviert lassen.
5. Den realen App-Identifier-Prefix aus Profil und signierten Entitlements
   ableiten; nicht ungeprüft mit der Team ID gleichsetzen. Daraus stabile,
   getrennte Ahoi-Keychain-Access-Groups für Sync-Payload und Remote Commands
   definieren.
6. Keychain-Sharing nach Least Privilege konfigurieren: Die Sync-Payload-Gruppe
   ist für Mobile und Mac gemeinsam; Command-Private-Key-Gruppen erhalten nur
   Targets, die tatsächlich Commands signieren. Der Mac erhält eine solche
   Gruppe nur bei implementierten ausgehenden Mac-Commands. Validatoren prüfen
   pro Plattform die exakte erlaubte Teilmenge; richtige Gruppe positiv sowie
   zusätzliche/falsche Gruppe negativ testen.
7. Development-Profiles über Automatic Signing neu erzeugen/aktualisieren und
   Entitlements im gebauten Produkt sowie eingebetteten Profil vergleichen.
8. Für CloudKit Development einen Apple-Development-signierten Mac-Build mit
   passendem Development-Profil und Development-Environment erzeugen.
9. Für den direkten macOS-Production-Build ein eigenes passendes Developer-ID-
   Provisioning Profile mit derselben Ahoi-Containerberechtigung erstellen.
   Developer-ID-Signierung, Provisioning und Notarisierung sind drei getrennte
   Nachweise.
10. Die aktuell CloudKit verbietende macOS-Entitlement-Allowlist bewusst und
    eng in `config/macos-entitlements.json` sowie
    `tools/verify_macos_entitlements.py` erweitern; die beiden Ahoi-CloudKit-
    Templates im Chromium-Overlay bleiben Source of Truth. Positive und
    absichtlich falsche Container-/Gruppen-Fixtures müssen den Validator
    bestehen beziehungsweise fail-closed scheitern.
11. Erst nach Apple-Grant Profile mit Managed Default Browser Entitlement neu
   erzeugen. Das Entitlement vorher nicht in ein Profil oder Archive erzwingen.

### Fünf explizite Signing-/Build-Modi

Ersetze den heutigen zirkulären Release-Vertrag durch klar benannte,
fail-closed Konfigurationen:

1. **DebugLocal**
   - provider-freier lokaler/Simulator-Build ohne CloudKit-Container,
     Production-Keys, Push oder Default-Browser-Entitlement;
   - muss auch ohne Apple-Account, Profile und private Konfiguration
     reproduzierbar bauen und testen;
   - keine leeren Platzhalter dürfen zur Laufzeit fälschlich CloudKit aktivieren.
2. **CloudKitDevelopment**
   - Apple Development/Automatic Signing;
   - CloudKit `Development`, APNs `development`;
   - dedizierter Ahoi-Container und geprüfte Development-Keychain-Gruppen;
   - bereits **vor** dem Managed-Default-Browser-Grant für echten CloudKit-
     Development-E2E nutzbar;
   - kein `com.apple.developer.web-browser`.
3. **TestFlightBootstrap**
   - Apple Distribution über Automatic/Cloud-Managed Signing bevorzugt;
   - CloudKit `Production`, APNs `production`, Production-Keychain-Vertrag;
   - **kein** Default-Browser-Entitlement;
   - App-Store-Connect-Export, externer TestFlight und öffentlicher Link
     erlaubt; `testFlightInternalTestingOnly` fehlt oder ist `false`.
4. **DefaultBrowserDevelopment**
   - erst nach dokumentiertem Apple-Grant;
   - Apple Development, CloudKit `Development`, APNs `development` und Managed
     Entitlement für direkte post-grant Gerätetests;
   - eigenes frisches Profil; keine semantische Mutation von
     `CloudKitDevelopment`.
5. **ReleasePostGrant**
   - erst nach dokumentiertem Apple-Grant;
   - Apple Distribution, neue Buildnummer, frisches Profil, frisches Archive
     und Production CloudKit/APNs;
   - `com.apple.developer.web-browser=true` muss in
     `DefaultBrowserDevelopment` und `ReleasePostGrant` jeweils im Source-
     Entitlement, signierten Produkt und eingebetteten Profil übereinstimmen;
   - niemals ein Bootstrap-Archive nachträglich umsignieren oder als
     post-grant Candidate umetikettieren.

Passe `project.yml`, xcconfigs, Entitlement-Dateien, ExportOptions und
`release-preflight.sh` an diese Modi an. Der Preflight prüft den gewählten Modus
und darf nicht für jeden `Release` blind das noch nicht erteilte
Default-Browser-Entitlement verlangen. Automatic/Cloud-Managed Signing ist der
bevorzugte Weg; manuelle Profile sind nur dokumentierter Fallback. Eine lokale
Apple-Distribution-Identität darf nicht voreilig als Voraussetzung behauptet
werden.

## CloudKit-, Schlüssel- und Recovery-Vertrag

### Entwicklungs- und Produktionsreihenfolge

1. App und Desktop auf denselben dedizierten Ahoi-Container, private database
   und Custom Zone konfigurieren.
2. Development-Schema ausschließlich mit synthetischen Daten initialisieren.
3. Auf mindestens zwei realen Ahoi-Installationen den vollständigen Roundtrip
   prüfen: Create, Update, Delete/Tombstone, Reihenfolge, Offline-Outbox,
   Wiederverbindung, Konflikt, Accountwechsel, Zoneverlust und Recovery.
4. Negative Evidenz erbringen: Cookies, Passwörter, Autofill, Site Storage,
   Permissions, Downloads, HTTP-Auth, private Tabs und Keychain-Secrets tauchen
   weder in Records noch Logs auf.
5. Zusätzlich jeden Record prüfen: verschlüsselte Nutzpayload nur in
   `encryptedValues`; querybare Klartextfelder nur für genehmigte, minimierte
   Routing-/Versions-/Konfliktmetadaten.
6. Schema und Indizes überprüfen; keine temporären/Debug-Felder promoten.
7. Erst nach Development-Abnahme das CloudKit-Schema kontrolliert nach
   Production deployen. Die Promotion ist eine bewusste externe Mutation mit
   Vorher-/Nachher-Evidenz.
8. Mobile Production ausschließlich über einen verarbeiteten
   TestFlightBootstrap-Build testen; der Mac-Gegenpart verwendet den separat
   signierten, Developer-ID-provisionierten und notarisierten Production-Build.
   Development-Erfolg ist kein Production-Nachweis.
9. Nach post-grant Archive den Production-Roundtrip erneut ausführen und
   sicherstellen, dass Buildmodus/Entitlement den Syncvertrag nicht verändern.

### Push-, Hintergrund- und Reconciliation-Vertrag

- CKSyncEngine-Push auf physischen Geräten in Vordergrund und Hintergrund
  prüfen. Nach OS-/Jetsam-Termination ist Wiederaufnahme best effort und wird
  separat beobachtet. Nach explizitem User-Force-Quit wird kein Push-Pass
  erwartet; Konvergenz muss nach manuellem Relaunch über begrenzte Foreground-
  Reconciliation erfolgen.
- Deaktivierte sichtbare Notifications, ausbleibender/verspäteter Push,
  Netzwerkwechsel und Retry müssen durch eine begrenzte Foreground-
  Reconciliation konvergieren.
- Kein verstecktes Polling und kein periodischer Fünf-Minuten-Timer als Ersatz
  für CKSyncEngine-Ereignisse.
- Diese Matrix einmal in CloudKit Development und erneut mit Mobile-
  TestFlight-Production plus Developer-ID-Mac-Production ausführen.

### Kryptografischer Schlüsselvertrag

- Nutzdaten bleiben mit AES-256-GCM verschlüsselt; Key-Version, Rotation,
  Recovery und Widerruf sind explizit und getestet.
- Beim ersten ausdrücklichen Sync-Opt-in erzeugt das erste Gerät genau einmal
  kryptografisch sicher einen AES-Payload-Key, aber nur wenn nach verifiziertem
  Readback noch keine verschlüsselten Remote-Daten beziehungsweise bestehende
  Key-Version existieren.
- Sichere parallele First-Opt-ins durch einen atomaren Bootstrap-Claim in der
  Custom Zone ab. Claim enthält nur Key-Version, nicht sensibles Fingerprint-
  und Konfliktmetadatum, niemals den Rohschlüssel, und wird mit serverseitiger
  Create-/Change-Tag-Bedingung geschrieben. Genau ein Gerät gewinnt; erst nach
  Server-Readback dieses Gewinners dürfen Domain-Records entstehen. Verlierer
  verwerfen ihren unpublizierten Kandidaten und wechseln in Join/Waiting.
- Teste zwei zeitgleich erstmals aktivierte Geräte deterministisch end to end;
  sie müssen ohne Split-Key, Doppelbootstrap oder Datenüberschreibung
  konvergieren.
- Existieren Remote-Daten ohne lokal verfügbaren passenden Key, darf niemals
  still ein neuer Key erzeugt oder Remote-Inhalt überschrieben werden. Die App
  zeigt einen klaren Join-/Waiting-/Pairing-/Recovery-Zustand.
- Weitere Geräte erhalten den Payload-Key nur über den überprüften
  synchronisierbaren Keychain-/Pairing-Vertrag. Kein Rohschlüssel in CloudKit,
  UserDefaults, Logs, Fixtures oder Git.
- Bereits extern provisionierte Payload-Keys werden über eine versionierte,
  readback-geprüfte und idempotente Migration übernommen; keine automatische
  Rotation während der Migration.
- Jedes Gerät erzeugt beim ersten Pairing einen eigenen Ed25519-Private-Key,
  speichert ihn nicht synchronisierbar und möglichst `ThisDeviceOnly` und
  publiziert nur den Public Key über den genehmigten Pairing-/CloudKit-Vertrag.
- Join, Waiting, Migration, Rotation, Widerruf, Geräteentzug und verlorener Key
  brauchen sichtbare Produktzustände sowie reale Cross-Device-E2E-Fälle.
- Rotation unterstützt eine explizit begrenzte Übergangszeit mit alter und
  neuer Key-Version, re-encrypted Daten und anschließendem Widerruf; ein
  Neuinstallations-/Geräteverlustfall darf weder still überschreiben noch
  dauerhaft in einer unauflösbaren Ladeansicht hängen.
- Verlorener, rotierter oder widerrufener Schlüssel führt fail-closed zu einem
  sichtbaren Recovery-Zustand, nicht zu stiller Neuerzeugung mit Datenverlust.
- Accountwechsel und Zoneverlust dürfen lokale Daten nicht ungefragt löschen;
  jeder destruktive Recovery-Schritt braucht Vorschau, Backup und bewusste
  Bestätigung.

## App Store Connect, TestFlight und Default Browser

### App-Datensatz und Metadaten

1. App-Datensatz mit den festgelegten Werten erstellen und Zugriff prüfen.
2. Versions-/Buildnummern monoton und an Commit/Archive binden.
3. App-Name, Subtitle, Beschreibung, Keywords, Kategorie, Altersfreigabe,
   Copyright, Support-/Marketing-/Privacy-URLs, Review Notes und Kontaktangaben
   vollständig und wahrheitsgemäß pflegen.
4. Eine belastbare öffentliche Ahoi-Produkt-, Support- und Privacy-Seite
   spätestens vor externer Beta, öffentlichem TestFlight-Link und Managed-
   Entitlement-Antrag bereitstellen. Falls Apple bis dahin das öffentliche
   Repository akzeptiert, diesen konkreten Formular-/Review-Nachweis festhalten
   und nicht aus bloßer Erreichbarkeit ableiten.
5. Privacy Manifest/Required-Reason APIs gegen den tatsächlichen Binärcode und
   alle Dependencies prüfen. App-Privacy-Angaben aus realem Datenfluss ableiten.
   Der aktuelle Manifeststand deklariert Browsing History, Search History,
   Other User Content und Device ID als unlinked/non-tracking App
   Functionality; dies gegen den finalen Build verifizieren und alte pauschale
   Aussagen „no collected data“ entfernen.
6. Export Compliance wegen TLS und eigener AES-/Ed25519-Nutzung fachlich
   dokumentieren und die Apple-Fragen korrekt beantworten; keine pauschale
   Freistellung raten.
7. Beta-Review-Informationen, Testhinweise und gegebenenfalls Demoablauf
   hinterlegen. EU-Trader-Status separat durch die berechtigte Person klären.

### Verbindliche TestFlight-/Entitlement-Reihenfolge

1. `TestFlightBootstrap` ohne Default-Browser-Entitlement archivieren.
2. Signatur, embedded profile, Production-CloudKit-Entitlements, Hash,
   Commit-SHA und Buildnummer lokal prüfen.
3. Mit Xcode Organizer hochladen und Processing abwarten.
4. Intern über TestFlight auf exakt diesem Build installieren und die
   releasekritischen Journeys auf iPhone und kompatiblem iPad ausführen.
5. Externe Testgruppe konfigurieren, Beta App Review bestehen und einen
   **öffentlichen TestFlight-Link** aktivieren. Ein nur interner Build reicht für
   den Managed-Entitlement-Antrag nicht.
6. Unmittelbar vor dem Antrag Apples aktuelle offizielle Default-Browser-
   Anforderungen erneut prüfen und als Antragsevidenz abhaken: browserfähige
   Startoberfläche, direkte URL/Suche, WebKit statt `UIWebView`, HTTP-/HTTPS-
   Deklaration und Routing, Info.plist/Privacy Keys, Kalt-/Warm-/Hintergrund-
   Verhalten sowie exakte Profil-/Bundle-Bindung.
7. Managed Default Browser Entitlement für
   `app.ahoibrowser.AhoiBrowser` beantragen; Produktreferenz und öffentlicher
   TestFlight-Link verwenden. Einen Antrag als `REQUESTED`, nicht als
   `GRANTED`, dokumentieren.
8. Nach tatsächlichem Grant App ID/Profile aktualisieren, neue Buildnummer
   vergeben, `ReleasePostGrant` frisch archivieren und hochladen.
9. Verarbeiteten post-grant Build über TestFlight neu installieren. Prüfe in den
   aktuellen Systemeinstellungen die Auswahl als Standardbrowser und öffne
   HTTP-/HTTPS-Links aus mindestens Mail/Notizen sowie einer Dritt-App bei
   Kaltstart, Warmstart, Hintergrundzustand und Weiterleitung innerhalb der
   expliziten Single-Scene-Autorität. Mehrszenen erst nach bewusster
   Implementierung und eigener Session-Ownership-Migration testen.
10. Kein leerer Doppel-Tab, kein verlorener Link, kein falscher privater Modus,
   keine ungefragte externe App und keine falsche URL-Normalisierung.
11. Öffentliche App-Store-Einreichung oder Release erst nach separater
    ausdrücklicher Nutzerfreigabe; TestFlight-Freigabe ist keine Store-
    Releasefreigabe.

## Datenschutz und Security

- Keine Produkttelemetrie, Usage-Pings oder automatischen Crash-Uploads.
- CloudKit ist opt-in/default-off und der Browser bleibt offline/local-first
  voll nutzbar.
- Synchronisiert werden nur explizit erlaubte normale Domänenobjekte. Nie:
  Cookies, Credentials, Autofill, Website Storage, Permissions, Downloads,
  Header-Secrets, HTTP-Auth, private Tabs oder Keychain-Material.
- Private Website-Daten werden beim Schließen der gesamten privaten Session
  verworfen. Mehrere gleichzeitig offene private Tabs dürfen den gemeinsam
  genutzten nonpersistent Store bis zum Ende dieser Session benötigen.
- Userinfo, `javascript:`, `data:`, `file:` und beliebige Custom Schemes werden
  an Navigation-, Sync- und Remote-Command-Grenzen fail-closed behandelt.
- Der Ursprung ist in Permission-, Auth-, Download- und External-App-Dialogen
  sichtbar.
- Remote Commands prüfen Signatur, Freshness, Device Trust, erlaubte Aktion,
  Zielobjekt und Replay-Schutz. Destruktives Remote-Schließen erfordert
  sichtbare Bestätigung.
- Threat Model, Privacy Policy, Datenfluss, Retention und Apple App Privacy
  werden vor TestFlight anhand des realen Builds aktualisiert.

## Performance-, Energie- und Lebenszyklusbudgets

- Vor UI-/Controller-Optimierungen auf repräsentativen Geräten eine reproduzierbare
  Baseline für Cold Launch, Warm Launch, erste Navigation, Tabwechsel,
  Workspace-Wechsel, Speicher bei 1/5/20 normalen und privaten Tabs,
  Discard/Restore, Idle-Energie, Background-Laufzeit und Wakeups erfassen.
- Vor der Implementierung messbare akzeptierte Regressionsgrenzen pro Metrik
  festlegen und im kandidatgebundenen Evidenzmanifest versionieren. Nachträglich
  verschobene Budgets brauchen Begründung und Review.
- Die erste Browserfläche wartet nicht auf CloudKit, Favicon-Netzwerk oder eine
  nicht notwendige Migration. Background-Sync bleibt ereignisgetrieben und
  begrenzt.
- Auf dem finalen Kandidaten dieselben Messungen mit identischen Fixtures und
  Gerätezuständen wiederholen; Mittelwert und Ausreißer/p95 dokumentieren und
  jede Überschreitung beheben oder als expliziten Release-Gate behandeln.

## Umsetzungsreihenfolge

### Phase 0 – Ownership, Baseline und Vertragsreparatur

1. Branch, Remotes, Dirty Worktree, Stashes und fremde Änderungen prüfen.
2. Im gemeinsam vereinbarten Branch disjunkte Pfadverantwortung für Mobile und
   Desktop festhalten und vor jedem Commit auf Überschneidungen prüfen. Einen
   isolierten kurzlebigen Worktree nur bei einer tatsächlich kollidierenden
   Datei oder einem ausdrücklich getrennten Buildkandidaten verwenden.
3. Vor CPU-intensiven Arbeiten prüfen, ob ein AhoiBrowser-/Chromium-Build mit
   `ninja`, `autoninja`, `siso`, `gn` oder Compiler/Linker in `out/AhoiDev`
   aktiv ist. Eigene CPU-intensive Arbeit bei Start eines solchen Builds nur
   über ihre exakt ermittelten PIDs pausieren/beenden und später inkrementell
   fortsetzen.
4. Zielprompt, Product Config, externe Gates, Testanforderungskatalog und
   Dokumentation gegen Live-Wahrheit abgleichen. Statusresultate nicht in
   `config/test-registry.json`, sondern kandidatgebunden unter `artifacts/e2e/`
   führen.
5. Zirkulären Signing-/Export-/Preflight-Vertrag in die fünf Buildmodi
   aufteilen.
6. Mindestens die sieben genannten Mobile-Swift-Dateien und darüber hinaus jede
   vom aktuellen `tools/source_line_budget.py` gemeldete Ahoi-Quelldatei
   verantwortungsbasiert unter das harte 800-Zeilen-Limit bringen, ohne
   Verhalten oder Wire-Kompatibilität zu ändern.

### Phase 1 – kleinster belastbarer Browsing-Slice

Erst diesen vertikalen Pfad stabilisieren und sichtbar prüfen:

`Kaltstart → Adresse/Suche → HTTPS-Seite → Zurück/Vor/Reload → neues Tab →
Tabwechsel → Close/Undo → Prozess beenden → normales Restore`

Behebe Crash, Blank Screen, Doppel-Tab, verlorene Navigation, falsche
Workspace-Zuordnung und Restore-Fehler vor zusätzlicher Breite.

### Phase 2 – Designsynthese und Kernjourneys

1. Die bereits vorhandenen anonymisierten PNG-Referenzen unter
   `docs/design/2026-08-30-mobile-directions/` als autoritative lokale
   Layoutbasis verwenden. Neue ImageGen-Boards nur bei einer konkret ungelösten
   Layoutfrage erzeugen und versionieren; ein noch nicht vollständig
   synchronisierter Figma-Abschnitt darf die lokalen Referenzen nicht
   überschreiben.
2. Harbor Deck, Focus Voyage und Workspace Canvas mit Ahoi-Tokens umsetzen.
3. Suche, Tabs, Workspaces/Mediathek, Privat, Control-Center und iPad-Sidebar
   visuell sowie accessibility-seitig schließen.
4. P1-Gesten nur ergänzen, wenn sie WebKit-, VoiceOver- und Reduced-Motion-
   Verhalten nicht verschlechtern.
5. Reale Fixtures für Auth, Popup, Upload, Download, Medien, Permission,
   Offline und externe Schemes nutzen; keine ausschließlich kosmetische Demo.

### Phase 3 – lokale und Simulator-Gates

1. Nach den notwendigen Build-/Installations-Preflights zuerst Xcode-UI- und
   Computer-Use-E2E-Tests der betroffenen Kernjourneys auf dem exakten
   Simulator-Kandidaten ausführen.
2. Small/large iPhone und iPad in Compact/Regular Width, Hell/Dunkel, große
   Dynamic-Type-Stufen, Reduce Motion/Transparency und erhöhten Kontrast.
3. Danach Policy-, Model-, Migration-, Key-, Wire- und Tombstone-Unit-Tests.
4. Controller-/Repository-/CloudKit-Integration mit deterministischen Fakes.
5. XcodeGen-No-op, Source-Line-Budget, Secret Scan, DCO, `git diff --check`,
   statische Analyse und kompletter Repository-Vertrag.
6. Nach größeren Anpassungen den jeweils betroffenen sichtbaren E2E-Pfad erneut
   auf dem neuen Kandidaten ausführen. Blockierte sichtbare Pfade mit exakter
   Grenze dokumentieren und die unabhängigen programmatischen Gates dennoch
   abschließen.

### Phase 4 – Apple Development und CloudKit Development

1. Die jeweils separat bestätigten Portal-Konfigurationen ausführen.
2. Development-Profile refreshen und signierten `CloudKitDevelopment`-Build
   auf physischem iPhone installieren. Parallel einen Apple-Development-
   signierten Mac mit Development-Profil und Development-Environment
   bereitstellen; dies ist nicht der Developer-ID-Production-Pfad.
3. Verfügbare physische iPads inventarisieren und ein bereits autorisiertes
   kompatibles Gerät mit iPadOS 26+ registrieren. Kein Gerät kaufen oder externe
   Hardwareorganisation voraussetzen. Falls keines existiert, alle
   kontrollierbaren Arbeiten abschließen und `PHYSICAL_IPAD_REQUIRED` als
   exakten externen Gate dokumentieren.
4. Alle auf diesem pre-grant Kandidaten ausführbaren `MOB-USER-*` und
   `IOS-01` bis `IOS-15` auf dem exakten Build ausführen. `MOB-USER-06` und
   `IOS-04` bleiben ausdrücklich `NOT_RUN`/extern entitlement-blockiert; ihre
   Quellen-, Routing- und Negativtests dürfen diesen Gerätepass nicht ersetzen.
5. Mac und Mobile gegen CloudKit Development samt Offline, Konflikt,
   Tombstone, Key-/Account-/Zone-Recovery und Privacy-Negativbeweis testen.
6. Den realen Transport ausschließlich über das dedizierte signierte Target
   `AhoiMobileCloudKitE2ETests` im Scheme `AhoiMobile-CloudKitE2E` ausführen.
   Der Hostmodus gehört ausschließlich diesem Scheme. Reale Mutation erfordert
   die explizite Freigabe `AHOI_CLOUDKIT_E2E_REAL_MUTATION_OPT_IN=YES` und einen
   frischen UUID-Wert in `AHOI_CLOUDKIT_E2E_RUN_TOKEN`. Vor Provider- oder
   `CKContainer`-Konstruktion müssen Bundle, Buildmodus, Container, Team und die
   tatsächlich signierten `SecTask`-Entitlements exakt passen. Danach zuerst
   einen frischen read-only Scope-Probe ausführen; synthetische Zone, Records,
   Geräte-ID und Subscription bleiben UUID-spezifisch. Cleanup ist
   marker-first, ownership-geprüft, idempotent und cancellation-resilient; ohne
   erfolgreich entschlüsselten AES-GCM-Ownership-Marker darf weder Subscription
   noch Zone gelöscht werden. Kein releasekritischer Skip darf im finalen
   Kandidaten offen bleiben.
7. Das Schema nach erfolgreicher CloudKit-/Key-/Push-Development-Matrix
   promoten. Offene, sachlich unabhängige UI-/Hardwarejourneys sind ehrlich zu
   dokumentieren, aber nicht als künstliche Voraussetzung der Schema-Promotion
   zu behandeln.

### Phase 5 – TestFlightBootstrap und öffentlicher Test

1. Mobile-Production-Archive ohne Default-Browser-Entitlement und separat den
   Developer-ID-provisionierten/notarisierten Mac-Production-Build erzeugen.
2. Archive/Export/Signatur/Profile/Entitlements/Hash/Commit belegen.
3. App Store Connect Record und Metadaten vervollständigen, Upload verarbeiten.
4. Internen TestFlight-Build auf iPhone und iPad installieren und E2E wiederholen.
5. Externe Beta Review, öffentliche Testgruppe und öffentlichen Link abschließen.
6. Production-CloudKit-Roundtrip mit den installierten TestFlight-Bits belegen.

### Phase 6 – Managed Entitlement und finaler Kandidat

1. Antrag mit öffentlichem TestFlight-Link stellen und Status nachvollziehen.
2. Nach Grant neue Profile und Buildnummer erzeugen.
3. Frisches post-grant Archive erstellen, verarbeiten und über TestFlight
   installieren.
4. Default-Browser-Systemauswahl und HTTP-/HTTPS-Handling end to end auf
   physischem iPhone und iPad prüfen.
5. Dabei `MOB-USER-06` und `IOS-04` erstmals real ausführen und anschließend
   die komplette `MOB-USER-*`-/`IOS-*`-Matrix für den finalen Kandidaten
   wiederholen.
6. Vollständige Regression, Upgrade-/Migration, CloudKit Production,
   Accessibility, Performance und Privacy-Negativbeweis erneut ausführen.

### Phase 7 – Abschluss und optionaler öffentlicher Release

1. Dokumentation und externe Gate-Registry auf belegte Zustände aktualisieren;
   `config/test-registry.json` bleibt unveränderter Anforderungskatalog mit
   `NOT_RUN`, Resultate liegen im kandidatgebundenen E2E-Evidenzmanifest.
2. Fokuscommits mit DCO pushen; vor Integration Remote-SHA erneut vergleichen.
3. Lokale/self-hosted Gates verwenden. GitHub-hosted Jobs, die wegen Billing
   null Schritte ausführen, nicht wiederholen oder als grün bezeichnen.
4. Öffentliche Store-Einreichung/Veröffentlichung nur nach ausdrücklicher
   Releasefreigabe; danach Store-Build installieren und kritischen Smoke erneut
   belegen.

## Verbindliche End-to-End-Matrix

### Repository und Build

- `git diff --check`;
- `python3 tools/source_line_budget.py`;
- `./scripts/test-repository.sh`;
- DCO- und Secret-Scan;
- `xcodegen generate --spec apps/AhoiMobile/project.yml` aus geeignetem
  Arbeitsverzeichnis und deterministischer No-op-Zweitlauf;
- Swift 6 Strict-Concurrency-Compile ohne neue Warnungen;
- DebugLocal-, CloudKitDevelopment-, TestFlightBootstrap-,
  DefaultBrowserDevelopment- und ReleasePostGrant-Preflights jeweils mit
  positiven und absichtlich negativen Fixtures;
- provider-freier Standardlauf darf entitlement-abhängige CloudKit-Tests nur
  explizit und dokumentiert skippen; der reale Transport läuft ausschließlich
  im Scheme `AhoiMobile-CloudKitE2E` mit exakter Signaturprüfung, explizitem
  Mutations-Opt-in, frischem UUID-Run-Token und ownership-geprüftem Cleanup;
- iPhone-/iPad-Simulator Debug und Release sowie `xcodebuild analyze`.

Fokussierte Referenzbefehle dürfen an den realen Workspace angepasst werden:

```bash
cd apps/AhoiMobile
xcodegen generate --spec project.yml
cd ../..
git diff --check
python3 tools/source_line_budget.py
./scripts/test-repository.sh
swift test --package-path spikes/cloudkit --jobs 1 --disable-index-store
xcodebuild -project apps/AhoiMobile/AhoiMobile.xcodeproj \
  -scheme AhoiMobile -configuration DebugLocal \
  -destination 'platform=iOS Simulator,id=<CURRENT_SIMULATOR_UDID>' \
  -derivedDataPath /private/tmp/ahoi-mobile-core-gate-derived \
  -resultBundlePath /private/tmp/ahoi-mobile-core-gate.xcresult \
  -parallel-testing-enabled NO -only-testing:AhoiMobileCoreTests test
xcrun simctl list devices available -j
xcrun devicectl list devices
```

Führe `xcodebuild test` und `xcodebuild analyze` mit explizitem Scheme,
Configuration, Destination/UDID und abgeleitetem Datenpfad aus. Speichere
Exitcode, Toolchain, Zielgerät und `.xcresult`; ein generisches Beispiel ist
keine ausgeführte Evidenz.

### Sichtbare Browserjourneys

- `MOB-USER-01`: AhoiBrowser Mobile kalt starten, URL eingeben, HTTPS-
  Navigation und sichtbare Origin-Zuordnung im aktiven WebKit-Tab prüfen;
- `MOB-USER-02`: Suchbegriff eingeben, konfigurierten Suchanbieter öffnen und
  Ergebnisnavigation mit Zurück, Vor, Neu laden und Stop ohne Doppelload
  bedienen;
- `MOB-USER-03`: normale Tabs anlegen, wechseln, umordnen, umbenennen, schließen,
  per Undo wiederherstellen und die Session nach Beenden/Kaltstart konsistent
  restaurieren;
- `MOB-USER-04`: aktive Seite in Workspace speichern, zwischen Workspaces
  verschieben und aus Baum, Suche und Browser erneut öffnen;
- `MOB-USER-05`: private Tabs teilen genau einen flüchtigen WebKit-Datenspeicher,
  bleiben vom normalen persistenten Store getrennt und erscheinen weder in
  Session, Verlauf, Suche, Sync noch Geräte-Tabs; Ausschluss auch nach
  Prozess-Tod belegen;
- `MOB-USER-06`: externe HTTP(S)-Links über die registrierte Default-Browser-
  Rolle sicher routen: einen leeren normalen aktiven Tab wiederverwenden,
  andernfalls genau einen neuen normalen Tab erzeugen, niemals eine aktive
  geladene Seite oder einen privaten Tab überschreiben und mehrfach
  zugestellte Callbacks deduplizieren;
- `MOB-USER-07`: echten File-Provider-Upload, normale/private authentifizierte
  Downloads, Fortschritt, Abbruch, Quick Look, Teilen und `target=_blank` mit
  korrekter Origin-, Tab- und Datenschutz-Zuordnung bedienen;
- `MOB-USER-08`: Kamera-/Mikrofonberechtigung originbezogen erlauben, ablehnen
  und abbrechen,
  JavaScript-/Dateiauswahl-Dialoge dem auslösenden WebKit-Tab zuordnen und
  externe App-Schemes erst nach Bestätigung öffnen;
- `MOB-USER-09`: Portrait/Landscape, Dynamic Type, VoiceOver, hoher Kontrast
  sowie reduzierte Bewegung/Transparenz auf echtem Gerät durchgängig bedienen;
- `MOB-USER-10`: iPad-Sidebar, Multitasking, Rotation, Hardwaretastatur,
  Pointer, Tab-Reorder und Workspace-Wechsel auf echtem iPad prüfen;
- `MOB-USER-11`: Offline-, TLS-, Renderer-/WebContent-, Speicher- und
  Prozessabbruchfälle sowie Background/Termination und unvollständigen Download
  auslösen; lokale Daten, Session-Restore und deterministische verständliche
  Wiederaufnahme prüfen;
- `MOB-USER-12`: normale Tabs und Workspace-Änderungen zwischen signiertem Mac
  und echtem iPhone/iPad über den finalen CloudKit-/Keychain-Vertrag abgleichen;
  private Daten bleiben ausgeschlossen;
- `MOB-USER-13`: unsichere, lokale, skriptbasierte, credentialtragende und
  unbekannte Schemes ablehnen; erlaubte externe Schemes zeigen vor Übergabe
  Origin und Ziel;
- `MOB-USER-14`: mit 1, 5 und 20 normalen sowie privaten Tabs Navigation,
  Wechsel, Umordnung, Discard/Speicherabbau, Session-Speicherung und
  Wiederherstellung ohne Phantomtabs prüfen;
- `MOB-USER-15`: visuelle/interaktive Konsistenz auf iPhone/iPad,
  normal/privat, Hell/Dunkel, Website-Tint/Fallback und allen unterstützten
  Barrierefreiheitsmodi prüfen.

### Bestehende Companion-/Remote-Journeys

- `IOS-01`: Workspaces, Baum, Tabs und Verlauf auf realem iPhone/iPad
  durchsuchen;
- `IOS-02`: gespeicherte Seite und Ordner anlegen;
- `IOS-03`: Baumknoten verschieben, umbenennen und löschen; am Mac gegenprüfen;
- `IOS-04`: Link im ausgewählten System-Standardbrowser öffnen;
- `IOS-05`: Link an bestimmten Mac und Workspace senden;
- `IOS-06`: normalen Mac-Tab remote öffnen;
- `IOS-07`: normalen Mac-Tab remote fokussieren;
- `IOS-08`: normalen Mac-Tab nach Bestätigung schließen;
- `IOS-09`: Offline-Command und TTL prüfen;
- `IOS-10`: `queued/delivered/executed/failed` prüfen;
- `IOS-11`: Replay über Neustart hinweg abweisen;
- `IOS-12`: falsches Zielgerät und ungültige Signatur abweisen;
- `IOS-13`: Gerätefreigabe und Geräteentzug;
- `IOS-14`: private Tabs bleiben unsichtbar und unsteuerbar;
- `IOS-15`: beliebige Custom Schemes, Shellbefehle und Massenaktionen abweisen.

### Geräte, Sync und Distribution

- exakter Development-Candidate auf physischem iPhone;
- exakter Development-Candidate auf kompatiblem physischem iPad iPadOS 26+;
- vollständige bestehende `IOS-01` bis `IOS-15` Matrix;
- vollständige `SYNC-01` bis `SYNC-27` Matrix mit zwei signierten Macs und
  mindestens einem realen iPhone/iPad;
- Mac–iPhone–iPad CloudKit Development mit Apple-Development-signiertem Mac und
  passendem Development-Profil;
- Developer-ID-signierter, passend provisionierter und notarisiert geprüfter
  Mac-Production-Build; installierte Runtime erneut gegen Entitlements,
  Container und tatsächlichen Sync prüfen;
- Privacy-Negativprüfung der CloudKit-Records und Logs;
- CloudKit Production über verarbeiteten TestFlightBootstrap-Build;
- lokales Archive gebunden an Source SHA, Buildnummer, Signatur, Profil,
  Entitlements und eigenen Hash;
- intern installierter TestFlightBootstrap-Build sowie externer Build mit
  aktivem öffentlichem TestFlight-Link;
- Default-Browser-Antrag und tatsächlicher Grant als getrennte Zustände;
- post-grant TestFlight-Installation und Default-Browser-E2E auf iPhone/iPad;
- Upgrade von der letzten unterstützten Companion-/Mobile-Version mit Backup,
  Migration, Readback und No-op-Zweitlauf;
- öffentlicher Release-Smoke nur falls separat freigegeben.

## Evidenzstufen

- `SOURCE_COMPLETE`: Code/Dokumentation vorhanden;
- `LOCAL_BUILD_PASS`: exakt benannter Compile-/Link-Pass;
- `UNIT_PASS` / `INTEGRATION_PASS`: benannte Suite mit Exit 0;
- `SIMULATOR_VISIBLE`: frisch installierter Simulatorbuild sichtbar geprüft;
- `DEVICE_VISIBLE`: frisch installierter, buildgebundener physischer Pass;
- `ASSISTED_E2E_PASS`: reale Accounts/CloudKit/physische Aktion plus technische
  Readback-Evidenz;
- `ARCHIVE_PASS`: `.xcarchive`/Export mit Signatur, Profil, Entitlements, Hash
  und Commitbindung;
- `TESTFLIGHT_INTERNAL_PASS`: verarbeiteter interner Build installiert;
- `TESTFLIGHT_PUBLIC_PASS`: externe Beta freigegeben und öffentlicher Link aktiv;
- `DEFAULT_BROWSER_REQUESTED`: Antrag eingereicht;
- `DEFAULT_BROWSER_GRANTED`: Apple-Grant nachgewiesen;
- `DEFAULT_BROWSER_E2E_PASS`: post-grant TestFlight-Build als Systemdefault auf
  physischem iPhone und iPad vollständig geprüft;
- `RELEASE_PASS`: alle releasekritischen Gates inklusive Upgrade, Datenschutz,
  Production Sync und gegebenenfalls Store-Installation erfüllt.

Keine Stufe impliziert automatisch die nächste. Jede Evidenz enthält Datum,
Commit, Buildnummer, Konfiguration, Gerät/OS, Installationsquelle, relevante
Hashes, erwartetes Ergebnis, tatsächliches Ergebnis und Artefaktpfad. Screenshots
allein beweisen weder Persistenz noch Sync, Security oder Binärprovenienz.
TestFlight kann thinnen, verschlüsseln und neu signieren; verlange daher keine
Bytegleichheit mit dem lokalen Archive. Binde die Installation stattdessen über
App-Store-Connect-Build-ID, Version/Build, Bundle ID, Team, Receipt,
Installationsquelle und Processing-Metadaten an den Upload.

Implementiere dafür `tools/verify_mobile_release_evidence.py` als Mobile-
spezifischen maschinenlesbaren Evidenzvalidator, der
mindestens `.xcresult`, Gerät/OS, Source SHA, Archive, lokale Hashes,
App-Store-Connect-Build-ID, Processing und TestFlight-Installation
kreuzvalidiert. Der bestehende Desktop-Validator für
`/Applications/AhoiBrowser.app` ist dafür nicht ausreichend.

## Zu prüfende und aktualisierende Artefakte

Mindestens folgende Dateien müssen am Ende konsistent sein:

- `outputs/AhoiBrowser-Mobile-Zielprompt.md`;
- `config/product.json`;
- `config/external-gates.json`;
- `config/test-registry.json` als unveränderter Anforderungskatalog;
- `artifacts/e2e/<candidate>/manifest.json` plus referenzierte Logs, xcresults,
  Screenshots/Videos und maschinenlesbare Resultate;
- `docs/IOS_BROWSER.md`;
- `docs/IOS_BROWSER_E2E_EVIDENCE.md`;
- `docs/RELEASING.md`;
- `docs/SYNC.md`;
- `docs/PRIVACY.md`;
- `docs/THREAT_MODEL.md`;
- `config/macos-entitlements.json`;
- `tools/verify_macos_entitlements.py`;
- `tools/verify_mobile_release_evidence.py`;
- `overlay/chromium/src/ahoi/browser/sync/AhoiBrowserCloudKit.xcconfig.template`;
- `overlay/chromium/src/ahoi/browser/sync/AhoiBrowserCloudKit.entitlements.template`;
- `apps/AhoiMobile/project.yml`;
- passende xcconfigs, Entitlements, ExportOptions und
  `apps/AhoiMobile/scripts/release-preflight.sh`;
- versionierte Design-/ImageGen-Referenzen und Testartefakte.

## Definition of Done

AhoiBrowser Mobile ist erst abgeschlossen, wenn alle folgenden Aussagen durch
frische Evidenz für denselben finalen Kandidaten wahr sind:

1. Harbor Deck, Focus Voyage und Workspace Canvas bilden eine kohärente,
   eigenständige Ahoi-Oberfläche auf iPhone und iPad.
2. Browsing, Tabs, Workspaces, Verlauf, Downloads, Permissions, Privatmodus,
   Restore, Offline und Accessibility funktionieren als zusammenhängende
   Alltagsjourney.
3. Jeder Ahoi-Quelltext hält das 800-Zeilen-Limit ein; Repository-, Unit-,
   Integration-, UI-, Analyse-, Concurrency-, DCO- und Secret-Gates sind grün.
4. App ID, dedizierter CloudKit-Container, Profile und die fünf Signing-Modi sind
   korrekt und ohne fremde Container/Schlüssel konfiguriert.
5. Mac–iPhone–iPad-Sync ist in Development und Production real geprüft,
   verschlüsselt, konflikt-/recovery-fest und schließt private beziehungsweise
   verbotene Daten nachweisbar aus.
6. Ein verarbeiteter öffentlicher TestFlightBootstrap-Build und Link existieren.
7. Apple hat das Managed Default Browser Entitlement tatsächlich erteilt; ein
   neuer post-grant Build ist über TestFlight installiert und als Systemdefault
   auf physischem iPhone und kompatiblem iPad end to end geprüft.
8. Lokales Archive, Commit, Buildnummer, Signatur, Profile, Entitlements und
   Hash sind belegt; die installierte TestFlight-App ist ohne falsche
   Bytegleichheitsannahme über App-Store-Build-ID, Receipt und Processing-
   Metadaten nachvollziehbar mit dem Upload verbunden.
9. Dokumentation, Privacy, Threat Model, Product Config und externe Gate-
   Registry beschreiben ohne Widerspruch genau den realen Stand; der
   Test-Registry-Katalog bleibt `NOT_RUN`, während das kandidatgebundene
   E2E-Manifest die tatsächlichen Ergebnisse enthält.
10. Eine öffentliche App-Store-Veröffentlichung wird nur dann als erledigt
    bezeichnet, wenn sie separat freigegeben, von Apple verarbeitet, aus dem
    Store installiert und erneut sichtbar geprüft wurde.

Liefere zum Abschluss eine knappe Wahrheitsmatrix mit: umgesetzt, lokal gebaut,
Simulator, physisches iPhone, physisches iPad, CloudKit Development, CloudKit
Production, Archive, interner TestFlight, öffentlicher TestFlight,
Default-Browser-Antrag, Grant, post-grant E2E, öffentlicher Release sowie jedem
noch offenen externen oder menschlichen Gate. Keine Absichtserklärung darf als
Pass markiert werden.
