# AhoiBrowser – vollständiger Master-Zielprompt

**Geltungsstand: 28. August 2026.** Diese fortgeschriebene Datei ist die autoritative Zielvorgabe für das aktive AhoiBrowser-Goal. Spätere, ausdrücklich vom Nutzer ergänzte Anforderungen werden hier konsistent in Funktionsumfang, Phasen, Abnahmematrix, Release-Gates und Definition of Done eingearbeitet; ein erneutes Einsetzen als separates Goal ist dafür nicht erforderlich.

**Verbindliche Ausführungsabgrenzung vom 27. August 2026:** Der native iOS-/iPadOS-Companion wird ab sofort von einem anderen Agenten verantwortet. Dieser macOS-/Chromium-Arbeitsstrom verändert, baut, testet oder installiert keine iOS-Dateien oder -Targets und wartet nicht auf deren Abschluss. Die Companion-Anforderungen bleiben Produktziel und Integrationsvertrag, sind aber ausdrücklich kein Schreib- oder Build-Scope dieses Arbeitsstroms.

## Rolle und Gesamtauftrag

Du bist leitender Browser-, Chromium-, C++-, Objective-C++-, AppKit-, Swift-, SwiftUI-, Security-, Privacy-, Release- und QA-Engineer. Baue im aktuell bereitgestellten Workspace das Open-Source-Projekt **AhoiBrowser** vollständig end to end auf.

AhoiBrowser wird ein nativer, schlanker, performanter und optisch hochwertiger Chromium-Browser für macOS 26 auf Apple Silicon. Er übernimmt die wirklich guten Bedienkonzepte von Arc – insbesondere den vertikalen, persistenten Seitenbaum, Workspaces, schnelle Gesten, eine zentrale Command Bar, schwebende Browserflächen und echte Split Views mit zwei bis vier simultan sichtbaren Seiten – ohne dessen Ballast. Es gibt keine AI-Plattform, kein Wallet, keinen Screenshot-Editor, keine Boost-Plattform, kein soziales Sharing-System und keinen eingebauten Werbeblocker.

Das Ziel ist ausdrücklich:

- kein UI-Mockup;
- kein Proof of Concept als Endergebnis;
- kein Electron-Projekt;
- kein CEF-Wrapper;
- kein `WKWebView`-Browser;
- kein eigener WebView-Tab-Host;
- keine Web-App, die einen Browser simuliert;
- sondern ein echter Chromium-`//chrome`-Fork mit nativer Oberfläche, Chromiums vollständiger Mehrprozessarchitektur, Sandbox, Extensions, DevTools, Downloads, Medien, Berechtigungen, Passwortfunktionen, HTTP-Authentifizierung, verschlüsseltem Sync und einem signierten, unter `/Applications/AhoiBrowser.app` installierten Daily-Driver-Build.

Arbeite selbstständig und persistent durch die in diesem Prompt festgelegten Phasen. Beginne nach einer kurzen Bestandsaufnahme unmittelbar mit der Machbarkeitsphase. Frage nur dann nach, wenn eine Entscheidung nicht aus dem Repository, der Umgebung oder diesem Prompt ableitbar ist oder wenn tatsächlich eine externe Freigabe, ein Account, ein Vertrag, echte Hardware, biometrische Bestätigung oder eine andere Nutzeraktion erforderlich ist.

Halte kontrollierbare Arbeit nicht wegen externer Blocker an. Schließe alle unabhängigen Arbeiten ab, dokumentiere den Blocker exakt und fahre mit dem nächsten möglichen Arbeitspaket fort.

## Aktiver technischer Ausgangspunkt

- Chromium-Basis ist der vollständig ausgerollte Mac-Stable-Pin `152.0.7977.65` am exakten Commit `fc4d67f1788019a27e32511137ceccbd2fafdaaa`. Dies ist weder Nightly noch Canary; die eigenen Ahoi-Kanalnamen `nightly`, `beta` und `stable` sind davon getrennte Produkt-/Updatekanäle.
- Die aktive Quellkomposition besteht aus dem getrackten Overlay und genau drei geordneten Patches: `0001-ahoi-m152-integration-seams.patch`, `0002-ahoi-deterministic-platform-tests.patch` und `0003-ahoi-upstream-page-load-tracing-test-isolation.patch`.
- Alle M152-Profile pinnen dieselbe Xcode-Installation 26.6/17F113 mit macOS SDK 26.5/25F70 und iOS SDK 26.5/23F81a. `pinned-reference` für Upstream/Release und `compatible-development` für Development bleiben getrennte Provenienzlabels und Abnahmewege, obwohl die Toolchain-Bits identisch sind.
- Die gesicherte M151-Quellfreeze, Recovery-Bundle und frühere grüne Testmatrix bleiben historische Recovery-/Regressionsbelege. Sie sind keine M152-Pässe. Ein M152-Build-, Installations- oder sichtbarer Runtime-Pass darf erst nach dem jeweils real durchgelaufenen Gate behauptet werden.

## Arbeitsweise und Wahrheitsstandard

Unterscheide bei jeder Funktion und jedem Meilenstein strikt zwischen folgenden Zuständen:

1. `IMPLEMENTED`: Der Quellcode ist vorhanden.
2. `PROGRAMMATIC_PASS`: Unit-, Integration-, Browser- und relevante Security-Tests sind grün.
3. `INSTALLED_PASS`: Die Funktion wurde im tatsächlich installierten Release-Bundle geprüft.
4. `CU_E2E_PASS`: Die Funktion wurde über Computer Use wie von einem echten Nutzer sichtbar bedient und bestätigt.
5. `ASSISTED_E2E_PASS`: Zusätzlich erforderliche Hardware-, Account-, Touch-ID- oder physische Nutzeraktion wurde real ausgeführt und das Ergebnis danach verifiziert.
6. `BLOCKED_EXTERNAL`: Eine konkret benannte externe Voraussetzung verhindert ausschließlich den betroffenen Nachweis.

„Build erfolgreich“, „Code ist vorhanden“, „Unit-Test grün“ oder „funktioniert im Debug-Build“ ist kein Fertignachweis.

Verbindliche Regeln:

- Teste sichtbare Funktionen im signierten, nach `/Applications` installierten Bundle.
- Verwende für User-Flows Computer Use und bediene die App sichtbar über Maus, Tastatur, Menüs, Dialoge, Finder und Systemeinstellungen.
- Führe den sichtbaren echten Computer-Use-/User-E2E-Pfad jeder betroffenen Funktion vor den zugehörigen programmatischen Unit-, Browser-, Integrations- und Repository-Tests aus.
- Findet ein nachgelagerter programmatischer Test einen Fehler, sichere die Diagnose, behebe ihn, baue und installiere den Kandidaten neu und wiederhole zuerst den exakt betroffenen sichtbaren E2E-Pfad; erst danach darf die Programmatik erneut laufen.
- Terminal, Datenbank-Readback, interne APIs und Logs dürfen den sichtbaren User-Flow verifizieren, aber nicht ersetzen.
- Kontrollierte lokale Testseiten sind Pflicht, ersetzen aber keine realen Tests von Chrome Web Store, 1Password, Bitwarden, uBlock Origin, YouTube, WebRTC/Meet, CloudKit, dem echten Updater und Widevine-Diensten.
- Sichere bei einem sichtbaren Fehler zuerst Evidenz, diagnostiziere dann, behebe ihn und wiederhole exakt denselben User-Flow.
- Ein einmaliger Screenshot reicht bei zustandsabhängigen Funktionen nicht; Reload, Fensterwechsel und App-Neustart gehören je nach Funktion zur Abnahme.
- Keine Passwörter, Tokens, Cookies, Vault-Inhalte, Authorization-Header oder privaten Browserdaten in Screenshots, Videos, Logs oder Commits.
- Keine Funktion als fertig melden, wenn der höchste für sie vorgeschriebene Testlevel nicht bestanden wurde.
- Keine P0-/P1-Probleme als „bekannte Einschränkung“ in eine öffentliche v1 verschieben.
- Antworte nicht nur mit weiteren Plänen. Plane knapp, implementiere danach und liefere überprüfbare Ergebnisse.

Wenn ein externer Blocker auftritt:

- benenne die genaue Test-ID;
- benenne die fehlende Voraussetzung;
- dokumentiere die bereits bestandenen Teilprüfungen;
- fordere höchstens die eine konkret notwendige Nutzeraktion an;
- führe danach die Verifikation selbst fort;
- wiederhole nicht endlos denselben unveränderten Versuch;
- behaupte niemals öffentliche Releasebereitschaft, solange ein kritisches Gate blockiert ist.

## Feste Produktentscheidungen

- Produkt- und Arbeitsname: **AhoiBrowser**.
- Desktopziel für v1: macOS 26, ausschließlich Apple Silicon/ARM64.
- Mobile Ergänzung zum ersten öffentlichen Release: nativer iOS-/iPadOS-26-Browser auf Basis des systemgelieferten WebKit und des vorhandenen Companion-Cores.
- Browserbasis: vollständiger Chromium-`//chrome`-Fork.
- UI: Chromium Views plus Objective-C++/AppKit für macOS-spezifische Integration und Liquid Glass.
- Ein gemeinsames normales Chromium-Profil für alle Workspaces.
- Workspaces trennen Seitenbaum, temporäre Fenstersitzungen, aktive Auswahl und Darstellung, nicht Cookies, Logins, Verlauf, Downloads, Site Permissions oder Extensions.
- Echte Inkognito-Fenster verwenden Chromiums `OffTheRecordProfile`.
- Split View verwendet zwei bis vier echte Chromium-Tabs und `WebContents`; Tab-auf-Tab-Drag-and-drop ist Kernfunktion und eine Vierergruppe besitzt insbesondere ein echtes persistierbares 2×2-Layout.
- Eigener CloudKit-first-Sync; kein Chrome Sync und kein Google-Konto zur Browsersynchronisation.
- Deutsch und Englisch sind vollständige Release-Sprachen.
- Erscheinungsbild: System/Hell/Dunkel, globale Hauptfarbe, optionaler Workspace-Akzent und natives, zurückhaltendes Liquid Glass.
- Eigener Browsercode: GPL-3.0-or-later, vorbehaltlich juristischer Prüfung notwendiger Distributionsausnahmen.
- Allgemeine Erweiterungskompatibilität bleibt erhalten.
- Klassisches uBlock Origin wird optional als externe Erweiterung unterstützt, nicht in den Browserkern integriert.
- Kein öffentliches Release, bevor die vollständige Daily-Driver-, Sync-, Extension-, Media-, DRM-, Update-, Security- und Computer-Use-Abnahme bestanden ist.

Kapsle Produktname, Bundle-Identifier, URL-Schemes, CloudKit-Container, Updatekanäle, Signaturkonfiguration und Assets zentral. Verwende für Development konsistente AhoiBrowser-Identifier, aber lege irreversible Production-CloudKit- oder öffentliche Bundle-IDs erst fest, nachdem Eigentümerschaft und Verfügbarkeit geprüft wurden.

## Unverhandelbare technische Architektur

### Chromium statt Electron-Falle

Verwende Chromiums echte Browserarchitektur und mindestens folgende Upstream-Komponenten:

- `Browser` und `BrowserList`;
- `Profile`, `BrowserContext` und `OffTheRecordProfile`;
- `WebContents`;
- `TabStripModel`;
- Network Service;
- Renderer-, GPU-, Utility- und Crash-Prozesse;
- Site Isolation und Sandbox;
- Extension Service;
- Download Service;
- History Service;
- Password Manager und HTTP Auth Manager;
- Permission Controller;
- Media, WebRTC und Picture-in-Picture;
- DevTools;
- Session Restore;
- Chromium PrefService und `sql::Database`.

Verboten sind:

- Electron;
- CEF;
- `WKWebView` als Desktop-Engine;
- `content_shell` als Produktbasis;
- ein eigener Prozess, der mehrere WebViews als Tabs verwaltet;
- ein dauerhaft laufender Node-Prozess;
- React oder eine andere Web-App als Orchestrator des Browser-Chrome;
- Abschalten von Sandbox oder Site Isolation als Implementierungsabkürzung.

Lasse Blink, V8, `net`, GPU, Media, Renderer-Sandbox und Site Isolation möglichst unverändert upstream. Eigene Produktlogik gehört primär in getrennte AhoiBrowser-Komponenten auf Browser-Ebene. Änderungen in Upstream-Dateien müssen kleine, nachvollziehbare Integrationspatches bleiben.

Tiefe Integration bedeutet die Nutzung der richtigen Chromium-Schnittstellen, nicht eine maximal große Fork-Differenz.

### Zentrale Dienste und Schnittstellen

Implementiere klar abgegrenzte Services:

- `WorkspaceService`: Workspaces, Reihenfolge, aktiver Zustand und Appearance.
- `TabTreeService`: persistente Ordner und gespeicherte Seiten.
- `SessionBridge`: Zuordnung von Baumknoten, temporären Tabs, `WebContents`, Fenstern und `TabStripModel`.
- `SplitViewService`: Split-Mitgliedschaft, Zwei-/Drei-/Vier-Pane-Layout, Fokus, Divider, Drag-and-drop-Policy und lokale Session-Persistenz auf Chromiums `TabStripModel`-/Split-Collection-Infrastruktur.
- `PopupOverlayService`: sichere Zuordnung von Web-Popups zu ihrem Opener, Overlay-Lebenszyklus, Promotion in einen normalen Tab und Übergabe an `SplitViewService`, ohne einen zweiten WebView-Host einzuführen.
- `MediaMiniPlayerService`: sichtbare Medienzustände, MiniPlayer-Platzierung, Quellenwechsel und Übergabe an Chromiums Picture-in-Picture- und Media-Session-Infrastruktur.
- `DeviceTabsService`: lokale, CloudKit-basierte Geräte-/Tab-Sicht auf Grundlage von `SyncProvider`, ohne Google-Konto oder Chrome Sync.
- `ResourcePolicyService`: Tab-Discarding, Memory-Saver-Policy, Ausnahmen und sichtbare Schlaf-/Aufweckzustände auf Chromiums Lifecycle- und Performance-Manager-Infrastruktur.
- `ArcImportService`: sichere Discovery, unveränderlicher Snapshot, Vorschau, deterministische Abbildung und atomarer Commit von Arc-Profil- und Sidebar-Daten in das Ahoi-Domänenmodell.
- `CommandService`: lokale Suche, Befehle, URL- und Suchparser.
- `ThemeService`: System/Hell/Dunkel, Hauptfarbe, Workspace-Akzent und Glass-Fallback.
- `DeveloperToolkitService`: Injection, Cache, Site Data, Cookies, Header und Diagnosefunktionen.
- `PrivacyModeService`: globaler und per Origin geltender Modus.
- `SyncProvider`: austauschbare Sync-Schnittstelle.
- `CloudKitSyncProvider`: v1-Syncimplementierung.
- `RemoteCommandService`: sichere Mac-/iOS-Gerätebefehle.
- `HttpAuthCredentialService`: UI- und Verwaltungslogik auf Chromiums Password Manager, `HttpAuthManager`, `LoginHandler` und Network Service.

Neue Mojo-Schnittstellen dürfen nur dort entstehen, wo eine Prozessgrenze dies erfordert. Sie müssen minimal, typisiert, profile-scoped und sicherheitsgeprüft sein. Normale Websites erhalten niemals privilegierten Zugriff auf diese Dienste.

### Datenmodell

Verwende versionierte, migrationsfähige Kerntypen:

- `Workspace`: UUID, Name, Icon, Sortierschlüssel, optionaler Akzent, Zeitstempel.
- `TreeNode`: UUID, Workspace-ID, Parent-ID, Typ `folder` oder `savedPage`, Titel, URL, Sortierschlüssel, bei Ordnern optionales Icon und optionaler semantischer Farbakzent, Zeitstempel, Tombstone.
- `RuntimeTab`: Geräte-, Fenster- und Tab-ID, optionale TreeNode-ID, URL, Titel, Aktivitätszeit, Ladezustand.
- `SplitGroup`: stabile UUID, Chromium-Split-ID, Fenster- und Workspace-Session, zwei bis vier geordnete Tab-Handles, kanonischer Layoutbaum, primäre/sekundäre Divider-Ratios, fokussiertes Pane und Zeitstempel.
- `HistoryVisit`: Visit-ID, Geräte-ID, URL, Titel, Zeitpunkt und Transition-Typ.
- `DeveloperAsset`: Typ CSS/LESS/SASS/JavaScript/Headerprofil, Scope, Aktivierung, Sync-Opt-in.
- `RemoteCommand`: Zielgerät, Befehlstyp, Payload, Nonce, Ablaufzeit, Status und Signatur.
- `HttpAuthCredentialMetadata`: server/proxy, Scheme, Origin, Port, Realm, Benutzername, bevorzugter Status und letzter erfolgreicher Einsatz; das Passwort selbst bleibt ausschließlich im Password Store.
- `ImportPlan`: Quellprofil, Snapshot-ID, Quellschema, ausgewählte Kategorien, deterministische Quell-/Ziel-IDs, Konfliktentscheidungen, Warnungen und erwartete Mutationen.
- `ImportJournal`: Import-ID, Snapshot-Hash, Status, atomare Commit-Grenze, Rollback-Metadaten und Idempotenzschlüssel; keine Klartext-Secrets oder unnötigen URLs/Titel in Logs oder Evidenz.

Der Seitenbaum liegt lokal in SQLite über Chromiums Datenbankhelfer. Verwende vorhandene Chromium-Services für Verlauf, Downloads, Permissions, Password Store, HTTP Auth Cache und Session Restore.

## Repository-, Build- und Upstream-Modell

### Repository

Lege ein kleines öffentliches AhoiBrowser-Repository an, keinen vollständigen Chromium-Verlauf als primäres GitHub-Repository. Es enthält:

- maschinenlesbaren Chromium-Pin;
- gepinnte kompatible `depot_tools`- und Xcode-Stände;
- eigenen Browsercode und GN-Targets;
- Branding;
- Overlay-/Integrationsmechanismus;
- nummerierte Patchserie;
- Build-, Test-, Packaging-, Installations- und Roll-Automation;
- lokale E2E-Fixtures;
- Dokumentation;
- Lizenzen und Third-Party Notices;
- Evidenzmanifeste.

Ein Bootstrap-Prozess muss auf einer frischen geeigneten Apple-Silicon-Maschine:

1. Voraussetzungen und freien Speicher prüfen.
2. Gepinnte `depot_tools` beziehen.
3. Chromium in exakt gepinnter Revision beziehen.
4. AhoiBrowser-Komponenten einbinden.
5. nummerierte Patches deterministisch anwenden.
6. GN-Konfiguration erzeugen.
7. ARM64-Build erstellen.
8. Tests ausführen.
9. App und DMG paketieren.
10. den installierten Build verifizieren.

Stelle idempotente Befehle für mindestens folgende Abläufe bereit:

- Bootstrap;
- Development-Build;
- Release-Build;
- fokussierte Tests;
- vollständige Tests;
- lokale E2E-Fixtures;
- Packaging;
- Installation nach `/Applications`;
- isoliertes Testprofil;
- Upstream-Roll;
- Release-Smoke;
- Evidenzsammlung.

Prüfe vor dem Chromium-Fetch Hardware, Xcode, freien Speicher und Buildkapazität. Empfohlen sind mindestens 64 GB RAM und 2 TB SSD; der aktuell konfigurierte Checkout-/Buildbetrieb empfiehlt mindestens 150 GiB freien Arbeitsbereich. Nur bei ausdrücklich überwachtem Lauf darf `AHOI_ALLOW_LOW_DISK=1` diese Empfehlung unterschreiten; unter der unveränderlichen harten 120-GiB-Sicherheitsgrenze müssen Checkout und Build weiterhin abbrechen. Der Override ist kein Release-Pass. Ein Ressourcenproblem ist früh und konkret zu melden, darf aber Dokumentation, Repository-Scaffolding und andere kontrollierbare Arbeiten nicht blockieren.

### Patchstrategie

Für jeden Patch müssen dokumentiert sein:

- Zweck;
- betroffene Upstream-Dateien;
- Sicherheits- und Privacy-Auswirkung;
- zugehörige Tests;
- erwartete Konfliktfläche;
- Rückbauoption.

Verwende eigene GN-Targets und klar getrennte Komponenten statt dauerhafter Shell-Kopiertricks. Jeder Chromium-Roll muss Patchkonflikte einzeln sichtbar machen.

Für den aktiven M152-Pin ist die wartbare Serie auf drei Einträge konsolidiert: den Ahoi-Integrationspatch, deterministische Plattformtests und die isolierte Upstream-Tracing-Testkorrektur. Die frühere 21-Patch-M151-Serie bleibt über Recovery-Artefakte nachvollziehbar, ist aber keine parallel zu pflegende aktive Patchserie.

Entschlackung muss updatesicher erfolgen: Deaktiviere nicht benötigte Produktflächen bevorzugt über zentrale Branding-/Feature-Konfiguration, Dependency- und Build-Flags oder kleine dokumentierte Integrationspunkte. Entferne keine gemeinsam genutzten Chromium-Kernpfade nur für eine kleinere sichtbare Oberfläche. Jeder deaktivierte Upstream-Dienst besitzt Begründung, Privacy-/Security-Auswirkung, Abhängigkeitstest und Roll-Check; ein Chromium-Update darf keine Funktion still reaktivieren oder einen sicherheitsrelevanten Dienst versehentlich abschalten.

### Lean Chromium: kleiner Lieferumfang statt kosmetischem Verstecken

Führe einen eigenen, messbaren **Lean-Chromium-Track**. Ziel ist kein tiefgreifender Umbau von Blink, V8 oder Chromiums Sicherheitsarchitektur, sondern ein kleineres, schnelleres und ruhigeres macOS-Produkt mit möglichst wenig ungenutztem Code, Ressourcenpaketen und Hintergrundaktivität. Brave darf als Vergleich für wartbare Feature-Gates und die Entkopplung fremder Produktdienste dienen, ist aber weder Quellvorlage noch automatischer Beleg für geringeren Ressourcenverbrauch; AhoiBrowser benötigt eine eigene, reproduzierbar gemessene Entscheidungsmatrix.

Erstelle eine versionierte maschinenlesbare Komponentenmatrix, die jede geprüfte Upstream-Produktfläche genau einer Entscheidung zuordnet: `keep`, `replace-with-ahoi`, `runtime-disable`, `exclude-from-build` oder `defer`. Jeder Eintrag nennt GN-Target beziehungsweise Feature-Flag, direkte und transitive Abhängigkeiten, Nutzerwert, Security-/Privacy-Auswirkung, erwartete Größen- und Laufzeitwirkung, Rollbackweg sowie den Test, der eine stille Reaktivierung beim nächsten Chromium-Roll verhindert. Ungeprüfte oder sicherheitsrelevante Komponenten bleiben standardmäßig erhalten.

Prüfe als Kandidaten insbesondere Google-/Chrome-Konto- und Chrome-Sync-Flächen, Chrome-spezifische Promotions und Onboarding, Commerce/Shopping/Preisbeobachtung, Feed/Discover, Lens, Glic/AI/Actor-/On-Device-Model-Funktionen, ungenutzte gebündelte Apps sowie ausschließlich von ausgeschlossenen Funktionen benötigte Background-Services, Ressourcen und Component-Updater. Diese Liste ist ein Auditauftrag, keine pauschale Löschfreigabe. Entferne eine Abhängigkeit erst, wenn alle Verbraucher, Startpfade, Einstellungen, Strings, Policies, Migrationen und Updatepfade nachweislich behandelt sind.

Unverändert erhalten bleiben insbesondere Sandbox, Site Isolation, Prozessisolation, Zertifikats- und Netzwerksicherheit, Safe Browsing und sicherheitsnotwendige Component-Updates, Blink/V8 und standardkonforme Web-APIs, Extensions und Native Messaging, DevTools, Downloads, Uploads, PDF/Druck, Passwortmanager, Autofill, Passkeys/WebAuthn, HTTP Auth, Permissions, Medien/WebRTC/PiP, Accessibility, Lokalisierung, Übersetzung, Crash-/Session-Recovery und alle in diesem Zielprompt zugesagten Ahoi-Funktionen. Webkompatibilität oder Sicherheitsupdates dürfen niemals für wenige Megabyte geopfert werden.

Bevor etwas entfällt, erfasse mit unverändertem Chromium derselben Revision und einem Ahoi-Vollbuild auf derselben Maschine mindestens: installiertes App-/DMG-Volumen, Mach-O- und Resource-Pack-Anteile, geladene Libraries, Prozesszahl, Cold-/Warm-Start, Idle-CPU und Wakeups, Hintergrundnetzwerk, GPU-Nutzung sowie RAM bei 1, 20 und 100 Tabs. Wiederhole die Messung nach jeder Entschlackungswelle. Die Releaseziele sind:

- keine ungenutzte ausgeschlossene Produktfläche bleibt nur verborgen, aber weiterhin verlinkt, paketiert oder im Hintergrund aktiv;
- das installierte Ahoi-Bundle überschreitet das gleich konfigurierte unveränderte Chromium-Bundle trotz Ahoi-Funktionen um höchstens 3 Prozent;
- gegenüber einem ansonsten identischen Ahoi-Vollbuild mit allen auditierten optionalen Produktmodulen wird mindestens 10 Prozent Bundle-Footprint eingespart, sofern die vorab dokumentierte Kandidatenmenge dies ohne Funktions- oder Sicherheitsverlust zulässt;
- deaktivierte Komponenten erzeugen null periodische Tasks, Wakeups oder Netzwerkzugriffe und werden nicht lazy nachgeladen;
- vorhandene Startzeit-, Speicher-, Idle-CPU- und Web-Benchmark-Budgets bleiben zusätzlich bindend.

Kann das 10-Prozent-Ziel nachweislich nur durch Entfernung zugesagter Browserfähigkeit, Webkompatibilität oder Security erreicht werden, entscheide nicht still. Dokumentiere die vollständige Größenbilanz und behandle ausschließlich dieses Prozentziel als explizites Produktentscheidungs-Gate; alle Null-Aktivitäts-, Größenaccounting- und Sicherheitsregeln bleiben hart. Jede Entschlackungswelle muss separat rückbaubar und über einen Chromium-Roll reproduzierbar sein.

### Upstream und Security-SLA

Automatisiere Roll-Vorschläge für relevante Chromium-Stable- und Security-Releases:

Bei einem vorhandenen sauberen Partial-Clone ist vor dem absichtlichen Wechsel des Checkouts `./scripts/fetch-chromium.sh --prehydrate-target` zu verwenden. Der Ablauf inventarisiert den exakten Ziel-Pin ohne Lazy Fetch, lädt nur fehlende eindeutige Blobs in kleinen, wiederaufnehmbaren HTTP/1.1-Batches und muss Worktree, Index, `HEAD`, Refs, `FETCH_HEAD` und Shallow-Grenze unverändert lassen. Erst danach darf der normale `gclient`-Sync den Checkout umstellen; ein Erst-Checkout verwendet diesen Opt-in-Modus nicht.

1. neue Upstream-Revision ohne eigene Patches bauen;
2. Patchserie einzeln anwenden;
3. vollständigen AhoiBrowser bauen;
4. Unit-, Browser-, Extension-, uBO-, Media-, Security- und Release-Smokes ausführen;
5. installierten Release-Smoke durchführen;
6. eigene Abweichungen gegen den vorherigen Roll inventarisieren.

Ziele:

- kritische Chromium-Sicherheitsupdates innerhalb von 48 Stunden;
- normale Stable-Rolls innerhalb von sieben Tagen.

Ein fehlgeschlagener Roll erzeugt sichtbaren Blocking-Status. Öffentliche Releases nennen Chromium-Commit, Patchset-Version, SBOM, Checksums, Third-Party Notices und bekannte Abweichungen.

### Erforderliche Dokumentation

Pflege mindestens:

- `PRODUCT_PRINCIPLES`;
- `ARCHITECTURE`;
- `UPSTREAM`;
- `BUILDING`;
- `SECURITY`;
- `THREAT_MODEL`;
- `PRIVACY`;
- `NETWORK`;
- `EXTENSION_COMPATIBILITY`;
- `SYNC`;
- `SPLIT_VIEW`;
- `TESTING`;
- `RELEASING`;
- `CONTRIBUTING`;
- Lizenz- und Third-Party-Dokumentation;
- laufenden Status mit den definierten Wahrheitsstufen.

Keine Apple-, Google-, CloudKit-, Widevine-, Update-, Notarisierungs- oder Signierschlüssel dürfen ins Repository gelangen.

## Native Oberfläche und Design

### Browser-Chrome

Baue eine hochwertige native Oberfläche:

- einheitliche vertikale Sidebar;
- keine horizontale Tab-Leiste;
- keine separate klassische Bookmark-Leiste;
- minimaler Browser-Chrome mit Zurück, Vor, Reload, Security-/Site-Status, Downloads und Extension-Actions;
- eine über dem Webseitenbereich schwebende, nicht layoutverändernde Navigationszeile statt eines dauerhaft WebView-Höhe verbrauchenden Toolbars;
- die gesamte Navigationszeile einschließlich Adressleiste, Navigation, Site-Status, Extension-Actions und konfigurierbarer Entwickleraktionen blendet sich nach einer sicheren Verzögerung automatisch aus;
- eine kleine, ruhige Notch beziehungsweise Reveal-Zone am oberen Rand blendet die Zeile per Hover oder Klick wieder ein; `⌘L`, Tastaturfokus, Permission-/Security-Zustände und relevante Browsermeldungen öffnen sie ebenfalls zuverlässig;
- Auto-Hide, Verzögerung und Notch sind konfigurierbar; fokussierte Eingaben, offene Menüs, Downloads, Berechtigungs- und Auth-Dialoge werden niemals mitten in der Bedienung ausgeblendet;
- der WebContents-Bereich liegt im normalen Fenstermodus in einem abgerundeten, leicht schwebenden Container mit semantischem Schatten und sauberem Clipping; Vollbild, Video-Vollbild, Drucken, Screen Capture und Accessibility erhalten definierte radius- beziehungsweise schattenfreie Modi;
- Sidebar ein- und ausblendbar sowie zusätzlich zwischen angedocktem und schwebendem Overlay-Modus umschaltbar;
- die angedockte Sidebar verkleinert den realen Content-Viewport korrekt; die schwebende Sidebar überlagert ihn bewusst, besitzt Scrim/Shadow und verändert die von der Website gemessene Viewportbreite nicht;
- im vollständig verborgenen Modus reserviert die Sidebar exakt null Layoutbreite. Eine maximal 5 Pixel breite, transparente Hot-Zone direkt am linken Fensterrand blendet dieselbe Sidebar als Overlay ein, ohne Renderer-Resize oder Website-Reflow. Verlassen blendet sie nach kurzer Verzögerung aus; Fokus, offenes Menü/Modal, laufender Sidebar-Drag, Resize und Tastaturbedienung halten sie zuverlässig offen. `Escape`, Pin/Dock und der Sidebar-Shortcut besitzen deterministische Semantik;
- Edge-Reveal, Floating und Docked verwenden dieselbe Komponenteninstanz, denselben Baumzustand und dieselben Fokusobjekte; es existieren keine konkurrierenden Sidebar-Kopien;
- für Fenstergrößen, macOS Split View, AhoiBrowser Multi-Pane Split View und Vollbild geeignet;
- native Fokus-, Tastatur-, VoiceOver- und Accessibility-Semantik;
- flüssige, zurückhaltende Animationen;
- keine unnötig dauerhaft sichtbaren Entwicklerwerkzeuge;
- kein technischer Baukasten-Look.

Die Sidebar folgt dabei einer ruhigen Arc-artigen Informationshierarchie,
ohne Arc pixelgenau zu kopieren:

- oben steht ausschließlich die kompakte Workspace-Identität mit Icon und
  Name, keine redundante Produktüberschrift;
- darunter folgen gespeicherte Gruppen und Seiten in flachen, luftigen Zeilen;
  ein eigener Kartenhintergrund erscheint nicht dauerhaft pro Eintrag;
- zusammengehörige Split-Panes werden innerhalb ihrer Gruppe kompakt
  nebeneinander dargestellt;
- eine feine Trennlinie mit einer dezenten Aktion zum Leeren trennt den
  gespeicherten Bereich von den temporären Tabs;
- ein neuer, noch leerer temporärer Tab erscheint in dieser Liste als normale
  Zeile mit Plus-Icon und lokalisiertem Titel `Neuer Tab`, nicht als visuell
  fremdes Sonderpanel;
- am unteren Sidebar-Rand bleibt unabhängig vom Scrollinhalt eine kompakte,
  gleichwertige Viererleiste für `Neuer Tab`, `Downloads`, `Verlauf` und
  `Einstellungen` erreichbar; kein einzelner übergroßer New-Tab-Button;
- oberhalb dieser Fußleiste schwebt bei aktiver, vom Nutzer gewählter
  Medienwiedergabe ein kompakter MiniPlayer; die
  Tab-Liste scrollt optisch dahinter weiter, erhält aber dynamisches Bottom-
  Padding in Höhe des Players, sodass jeder Tab erreichbar bleibt;
- nur Hover, Auswahl, Drag-Ziel, Split-Zugehörigkeit oder eine ausdrücklich
  konfigurierte Gruppenfarbe erzeugen abgerundete Hintergründe; der
  Grundzustand bleibt flach und ruhig.

Verwende Chromium Views für Browsernavigation, Sidebar, Workspaces und Command Bar. Objective-C++/AppKit dient macOS-Integration, Fenstern, Menüs, Gesten, Systemappearance und Liquid Glass. Interne WebUI ist nur für komplexe Settings oder Editoren erlaubt, nicht als Browser-Orchestrator.

### Web-Popups als sichere Overlays

Stelle von Webseiten oder `window.open` erzeugte, geeignete Popup-Inhalte Arc-artig als browserkontrolliertes Overlay über dem auslösenden normalen `WebContents` dar. Das Popup bleibt ein echtes Chromium-`WebContents` mit normaler Site Isolation, Sandbox, Origin-Anzeige, Permission-Zuordnung, Extension-Kompatibilität und eigenem Navigationszustand; es ist kein DOM-Overlay und kein Ersatz-WebView.

- Das Overlay ist mittig, responsiv, visuell vom Hintergrund getrennt und verwendet Scrim, abgerundeten Container, Schatten und optional Glass nur im Browser-Chrome.
- Rechts außerhalb des Popup-Containers liegen vertikal angeordnete, tastatur- und VoiceOver-bedienbare Browseraktionen: `Schließen`, `Als eigenen Tab öffnen/maximieren` und `Mit Ausgangstab splitten`.
- `Als eigenen Tab öffnen` verschiebt dasselbe `WebContents` ohne Reload, Verlust von Formularzustand, Navigation History oder laufendem Login.
- `Mit Ausgangstab splitten` bildet eine echte Split-Gruppe mit dem Opener beziehungsweise fügt das Popup nach expliziter Vorschau in dessen bestehende Split-Gruppe ein; ein voller Vierer-Split lehnt ein fünftes Pane verständlich ab.
- Fokus, Escape-Verhalten, Before-Unload, Downloads, Datei- und Permission-Dialoge, HTTP Auth, DevTools, Audio/Video und Vollbild bleiben eindeutig dem Popup zugeordnet.
- Popup-Blocker, Sicherheitsregeln, Größen-/Featurewünsche und Nutzerentscheidung bleiben Chromium-kompatibel. Inhalte, die wegen Plattform-, Sicherheits-, OAuth-, Passkey-, Payment- oder Vollbildanforderungen nicht sicher eingebettet werden können, öffnen kontrolliert als separates Fenster und erklären diesen Übergang nicht irreführend.
- Die visuelle Produktreferenz ist der vom Nutzer bereitgestellte [Arc-Popup-Referenzfall auf Reddit](https://www.reddit.com/r/browsers/s/UKkIoe0o4y); kopiere keine fremden Assets oder Marken pixelgenau.

### Liquid Glass und Accessibility

Verwende `NSGlassEffectView` gezielt für:

- Sidebar-Hintergrund;
- Command Bar;
- Popover und Popup-Overlay-Chrome;
- schwebende Navigationszeile, Reveal-Notch und MiniPlayer;
- kompakte Werkzeugflächen;
- passende native Buttons.

Verwende Glass nicht flächendeckend über dem Webseiteninhalt. Respektiere automatisch:

- Transparenz reduzieren;
- Bewegung reduzieren;
- erhöhten Kontrast;
- systemweite Appearance.

Bei reduzierter Transparenz muss eine vollständig lesbare opake Darstellung entstehen. Glass darf weder Textkontrast noch Performance verschlechtern.

### Theme

Implementiere:

- `System`;
- `Hell`;
- `Dunkel`;
- globale Hauptfarbe;
- optionalen Akzent pro Workspace;
- Glass an/aus beziehungsweise automatischen Accessibility-Fallback;
- Glass als normal aktivierbare Produktoption für Sidebar, schwebende Browserflächen, Popups und MiniPlayer mit einem einheitlichen Materialsystem;
- auf unterstütztem macOS 26 ist Glass im Systemmodus standardmäßig aktiv, solange Accessibility-, Energie- oder Performancebedingungen keinen dokumentierten Fallback erfordern;
- semantische Farben und Zustände;
- optional aktivierbare, standardmäßig ausgeschaltete Seitenfarb-Tönung der Sidebar: bevorzugt wird die bereits geladene deklarierte `theme-color` des aktiven Panes, sonst eine lokal und auf feste Pixelzahl begrenzte Analyse des bereits geladenen Favicons; daraus entsteht nur ein dezenter Pastell-Overlay auf der semantischen Hell-/Dunkel-/Glass-Fläche;
- die Seitenfarb-Tönung löst niemals Netzwerkzugriffe, Seitenscreenshots oder unbeschränkte Bildanalyse aus, folgt dem aktiven Split-Pane ohne Flackern und ist bei hohem Kontrast vollständig deaktiviert.

Nicht Bestandteil von v1 sind importierbare Theme-Pakete, frei programmierbare CSS-Themes für das Browser-Chrome oder ein Theme-Marktplatz.

### Mehrsprachigkeit

- Englisch ist Quellsprache.
- Deutsch ist vollständig gepflegte Release-Sprache.
- Desktoptexte verwenden GRIT/ICU.
- iOS/iPadOS verwendet String Catalogs.
- keine hart codierten sichtbaren UI-Texte.
- CI prüft fehlende Strings, Pluralformen, Rohschlüssel und Überläufe.
- Pseudolokalisierung gehört in CI.
- vollständige Kernreise wird in Deutsch und Englisch per Computer Use geprüft.

## Workspaces, nested Tabs und Sitzungen

### Persistenter Seitenbaum

Baue einen vertikalen Baum als Zusammenführung aus Tabs und Bookmarks:

- beliebig tiefe logische Ordnerstruktur;
- gespeicherte Seiten als dauerhaft organisierte Tab-Knoten;
- vollständiges Drag-and-drop mit eindeutigem `vorher`-, `nachher`-, `in Ordner`- und `mit Seite splitten`-Ziel;
- manuelle Reihenfolge;
- Umbenennen;
- Verschieben innerhalb und zwischen Workspaces;
- Duplizieren;
- Papierkorb;
- Undo;
- Suche;
- Virtualisierung für mindestens 10.000 Knoten;
- Zyklenschutz und atomare Schreibvorgänge.

Ordner beziehungsweise Gruppen besitzen zusätzlich eine schlanke, persistente
Darstellungskonfiguration:

- frei änderbarer Name, optionales Icon und optionaler Farbakzent;
- eine zugewiesene Farbe färbt eine kontrastgeprüfte, zurückhaltende
  Gruppen-Bubble und die zugehörigen Gruppen-Akzente, niemals unkontrolliert
  Text oder Favicons;
- alle sichtbaren direkten Kinder liegen innerhalb derselben
  zusammenhängenden Bubble statt in voneinander getrennten Karten;
- Unterordner bleiben darin als eigene verschachtelte Bubble und eindeutige
  Hierarchie erkennbar;
- Ein-/Ausklappen, Drag-Vorschau, Drop-Zonen, Auswahl, Hover und Split-Segmente
  dürfen die optische Gruppenzugehörigkeit nicht aufbrechen;
- ohne eigene Farbe verwendet eine Gruppe ausschließlich semantische
  Theme-Farben; Hell, Dunkel, hoher Kontrast und reduzierte Transparenz bleiben
  vollständig unterstützt.

### Tab-Lebenszyklus

- Neue Seiten starten als temporäre Tabs.
- `⌘D` oder Drag-and-drop in einen Ordner macht eine Seite dauerhaft.
- Das Schließen eines gespeicherten Knotens entlädt dessen `WebContents`, löscht aber nicht den Knoten.
- Löschen ist eine separate, klar erkennbare Aktion.
- Geschlossene temporäre Tabs verschwinden, bleiben kurzfristig über Undo beziehungsweise Chromiums Tab Restore verfügbar.
- Es gibt keine automatische Archivierung in v1.
- „Temporär“ bedeutet nicht, dass die Seite bei normalem App-Ende zwingend verloren geht.
- Beim Browserstart wird standardmäßig gefragt: „Letzte Sitzung fortsetzen“ oder „Leer starten“.
- In den Einstellungen kann dauerhaft `fragen`, `fortsetzen` oder `leer starten` gewählt werden; Default ist `fragen`.
- Crash Recovery bietet normale temporäre Tabs und Fenster zur Wiederherstellung an.
- Inkognito wird nie wiederhergestellt.
- Geladene Tabs zeigen ihren Lifecycle verständlich an. Automatisch verworfene beziehungsweise schlafende Tabs bleiben in Baum und Sitzung erhalten, werden beim Aktivieren transparent wiederhergestellt und dürfen nicht wie geschlossene oder gelöschte Tabs wirken.
- Ein laufender Audio-/Videotab zeigt statt eines beliebigen Statuszeichens ein konsistentes Lautsprecher-Icon; stummgeschaltete Wiedergabe beziehungsweise ein stummgeschaltetes Video zeigt ein eindeutig unterscheidbares stummes Lautsprecher-Icon. Hover-Aktionen dürfen den Mediazustand nicht verdecken.
- Das Schließen des letzten Tabs schließt ein normales AhoiBrowser-Fenster nicht und erzeugt weder automatisch ein Ersatz-`WebContents` noch eine künstliche New-Tab-Seite. Ein Workspace und ein Fenster dürfen einen echten Zustand mit null Tabs und null aktivem Tab besitzen.
- Im Null-Tab-Zustand bleibt die komplette native Browserhülle bedienbar: Sidebar, Workspaces, Command Bar, `⌘T`, Downloads, Verlauf, Einstellungen, Extension-/Browseraktionen und Fenstersteuerung funktionieren. Die Content-Fläche zeigt ausschließlich eine ruhige, native, theme- und appearancefähige Leerdarstellung ohne Renderer, Netzwerkzugriff oder Fake-Tab.
- Nur das explizite Schließen des Fensters beziehungsweise Beenden der App zerstört das leere Fenster. `⌘W` schließt bei vorhandenem Tab zunächst den Tab; bei bereits leerem Fenster folgt es der dokumentierten macOS-Fenstersemantik. Session Restore, Downloads, Before-Unload, Split-Auflösung und Crash Recovery sind null-tab-sicher.

### Verbindlicher Drag-and-drop-Vertrag der Sidebar

- Die obere und untere Zone einer Seitenzeile ordnet den Knoten `vorher` beziehungsweise `nachher` ein.
- Die Mitte eines Ordners verschiebt die Auswahl `in` diesen Ordner.
- Die Mitte einer Seitenzeile bedeutet `mit dieser Seite splitten`.
- Vor dem Loslassen ist genau eine Operation mit Ziel, Einfügeposition und gegebenenfalls Split-Layout sichtbar.
- Der Drag-Lifecycle besitzt genau eine zentrale Quelle für den aktiven Payload. Das `Neue Gruppe`-Ziel, Saved-/Temporary-Zonen, Split-Ziele und Auto-Expand erscheinen aus demselben Zustand und werden bei Drop, `Escape`, nativem Abbruch, Quell-View-Zerstörung, Widget-Abschluss und Fensterwechsel garantiert zurückgesetzt.
- Die native Drag-Vorschau zeigt Favicon, Titel und bei Split-Gruppen die Pane-Anordnung; sie beginnt rechts vom Cursor und darf in den WebContents-Bereich hineinragen, überdeckt aber keine Sidebar-Drop-Zone. Sie bleibt an Fenster-/Bildschirmrändern sichtbar und besitzt einen zugänglichen Textfallback.
- `Escape`, Pointer-Abbruch, ungültiges Ziel, fehlgeschlagener Tab-Detach oder verweigerter Drop ändern weder Baum noch Split-Topologie.
- Ordner, Mehrfachauswahl, Dateien und andere Nicht-Tab-Payloads dürfen nicht versehentlich einen Split erzeugen.
- Drag-and-drop funktioniert innerhalb tief verschachtelter Ordner, zwischen Ordnern und Workspaces sowie zwischen normalen Browserfenstern.
- Beim Cross-Window-Drag wird der echte Chromium-Tab mit `WebContents`, Navigation History und laufendem Zustand über Chromiums Detach-/Insert-Pfad verschoben; es wird kein Ersatz-WebView erzeugt.
- Datei-Drag-and-drop behält seine Upload-/Navigationsbedeutung und wird nie als Tab-Split fehlinterpretiert.
- Für alle Drag-Aktionen existieren gleichwertige Kontextmenü-, Command-Bar- und Tastaturaktionen.

### Echte Split Views mit zwei bis vier Live-Panes

Nutze und generalisiere Chromiums vorhandene Split-Tab-Infrastruktur auf dem aktiven Stable-M152-Pin. `TabStripModel`, `SplitTabCollection`, `TabInterface` und die normalen `WebContents` bleiben die Runtime-Quelle der Wahrheit. `SplitViewService` koordiniert Ahoi-spezifische Policy und Persistenz, besitzt aber weder einen zweiten Tab-Store noch einen parallelen WebView-Host.

Verbindliche Grenzen und Layout-IDs liegen in `config/split-view.json`; die implementierungsnahe Spezifikation liegt in `docs/SPLIT_VIEW.md`.

Unterstütze:

- exakt zwei, drei oder vier gleichzeitig sichtbare, interaktive und live laufende Chromium-Seiten pro Split-Gruppe;
- zwei Spalten und zwei Zeilen;
- drei Spalten und drei Zeilen;
- großes Pane links oder rechts plus zwei übereinanderliegende Panes;
- großes Pane oben oder unten plus zwei nebeneinanderliegende Panes;
- vier Panes insbesondere als echtes 2×2-Raster; zusätzliche sinnvolle Vierer-Anordnungen sind erlaubt, sofern Mindestgrößen, Fokus und Bedienbarkeit gewahrt bleiben;
- Wechsel der Anordnung ohne Reload oder Neuerzeugung eines `WebContents`;
- Reordering der Panes;
- Pointer-, Trackpad- und tastaturbedienbare Divider mit Snap Points, zugänglichen Werten und sicheren Mindestgrößen; bei 2×2 werden horizontales und vertikales Verhältnis unabhängig gespeichert;
- genau ein fokussiertes/aktives Pane bei gleichzeitig sichtbaren und laufenden übrigen Panes.

Die Sidebar repräsentiert eine Split-Gruppe als eine zusammengehörige visuelle Einheit. Segmentanzahl, Reihen-/Spaltenanordnung und Reihenfolge spiegeln das aktuelle Content-Layout wider. Ein 2×2-Split erscheint daher als kompaktes 2×2-Segmentraster statt als vier unverbundene Zeilen. Die Zeile darf für Lesbarkeit kontrolliert höher werden und Schrift, Favicons sowie Statusicons adaptiv skalieren; sie darf aber keine unnötige Kartenfläche erzeugen.

Drag-Verhalten:

- Tab auf normalen Tab erzeugt standardmäßig zwei Spalten und bietet im Preview direkt zwei Zeilen an.
- Tab auf ein Pane einer Zweiergruppe zeigt die genaue Einfügeposition und erzeugt nach Drop eine gewählte Dreier-Anordnung.
- Tab auf ein Pane einer Dreiergruppe zeigt die genaue Einfügeposition und erzeugt nach Drop eine gewählte Vierer-Anordnung, standardmäßig das nachvollziehbare 2×2-Raster.
- Tab innerhalb derselben Split-Gruppe kann Pane-Reihenfolge und Layout ändern.
- Jedes sichtbare Pane besitzt in seiner Mini-Toolbar einen klar erkennbaren nativen Drag-Griff. Das Ziehen dieses Griffs über obere, rechte, untere oder linke Drop-Zonen eines anderen sichtbaren Panes ordnet die bestehenden `WebContents` atomar neu an und kann zwischen Spalten- und Zeilenlayout wechseln; es handelt sich ausdrücklich nicht um das Ziehen eines Sidebar-Tabs.
- Während dieses Content-Drags zeigt ein browserkontrolliertes Overlay die exakte Zielzone und resultierende Pane-Reihenfolge. Abbruch oder jeder Fehler stellt Tab-Reihenfolge, Split-ID, Mitgliedschaft, Layout, Arrangement, Ratios und aktives Pane vollständig wieder her.
- Sidebar- und Content-Drop verwenden denselben transaktionalen
  WebContents-Rebind-Lebenszyklus: Alle geänderten alten Pane-Bindungen werden
  zuerst gelöst und erst danach in Zielreihenfolge neu gebunden. Zu keinem
  Zeitpunkt dürfen zwei `ContentsWebView`-, Accessibility-, Read-Anything-,
  Permission- oder Dialog-Overlays dasselbe `WebContents` gleichzeitig
  beobachten.
- Tab aus der Split-Gruppe auf ein normales Sidebar-Ziel entfernt ihn aus dem Split, schließt ihn aber nicht.
- Eine Vierergruppe wird nach Entfernen oder Schließen eines Panes zur passenden Dreiergruppe, eine Dreiergruppe zur passenden Zweiergruppe und eine Zweiergruppe mit einem verbleibenden Pane zu einem normalen Tab.
- Ein fünfter externer Tab wird sichtbar und verständlich abgewiesen; kein vorhandenes Pane wird still ersetzt, versteckt oder geschlossen.
- Normale und Inkognito-Tabs dürfen niemals in derselben Split-Gruppe liegen.
- Quick Window hostet keine Multi-Pane-Oberfläche, darf aber `In Browser verschieben und splitten` anbieten.

Fokus- und Security-Regeln:

- Klick, Tastaturfokus oder expliziter Pane-Wechsel aktiviert dessen vorhandenes `TabInterface`, ohne Reload.
- Adressleiste, Zurück/Vor, Reload, Page Info, Berechtigungen, Extension-Actions, Developer Toolkit und Downloads beziehen sich eindeutig auf das aktive Pane.
- Die Mini-Toolbar jedes Panes zeigt dessen gekürzte URL dauerhaft; ein zusätzlicher nicht nur farblicher Aktivindikator macht dort den Omnibox-Zielzustand eindeutig. Klick oder Drag-Start aktiviert genau dieses Pane, und die nächste Adressleisten-Navigation verändert ausschließlich dessen URL.
- Jedes Pane zeigt browserkontrollierte Origin-, Security-, Audio-, Kamera-, Mikrofon- und Sharing-Zustände.
- Ein dauerhafter, nicht nur farblicher Fokusrahmen markiert das aktive Pane und wird bei Omnibox, Page Info, Permission Prompt, Device Chooser und anderen sicherheitskritischen Browserflächen verstärkt.
- Tabmodale Dialoge und Scrims bleiben beim auslösenden Pane.
- Inaktive Panes dürfen keinen neuen Permission Prompt oder System-Dateipicker öffnen; nach Fokuswechsel darf die wartende Anfrage eindeutig zugeordnet erscheinen.
- Same-Origin Policy, Site Isolation, Sandbox, Storage Partition und Renderer-Prozessgrenzen bleiben unverändert aktiv.

Lebenszyklus und Persistenz:

- Das Erzeugen eines Splits ändert weder gespeicherten/temporären Zustand noch Parent, Reihenfolge oder Persistenz eines Tree-Knotens.
- Normale Split-Mitgliedschaft, Layoutbaum, Divider-Ratios und fokussiertes Pane gehören zur Fenster-/Workspace-Sitzung und werden lokal atomar über Session Restore gespeichert.
- Split-Topologie wird nicht über CloudKit synchronisiert.
- Inkognito-Splits funktionieren innerhalb eines `OffTheRecordProfile`, werden aber nie serialisiert, wiederhergestellt, synchronisiert oder in der Companion-App angezeigt.
- Bei nicht wiederherstellbarem Leaf degradiert eine Vierergruppe ohne Phantom-Tab zur passenden Dreiergruppe, eine Dreiergruppe zur passenden Zweiergruppe und eine Zweiergruppe zu einem normalen Tab.
- Renderercrash betrifft nur das jeweilige Pane; andere Panes bleiben bedienbar.
- Browsercrash und Tab Restore erhalten bei normalen Tabs die maximal wiederherstellbare Mitgliedschaft, Anordnung, Ratios und den Fokus.

Browserfähigkeit im Split:

- Audio und Video laufen in allen sichtbaren Panes weiter; Fokuswechsel pausiert nichts automatisch.
- Mute und Media-/Capture-Indikatoren sind pro Tab/Panes sichtbar; Chromium Media Session und Audio Focus bleiben maßgeblich.
- Picture in Picture startet aus dem auslösenden `WebContents` und überlebt Fokus-, Layout-, Divider-, Workspace-, Fenster-Minimize- und Sidebar-Wechsel gemäß normalem Chromium-PiP-Lebenszyklus.
- Bereits freigegebene Kamera-, Mikrofon-, WebRTC- und Bildschirmfreigaben dürfen in einem inaktiven sichtbaren Pane weiterlaufen und bleiben immer angezeigt.
- Downloads, Uploads, Auth- und Berechtigungsdialoge bleiben dem auslösenden Pane und Origin zugeordnet.
- DevTools öffnet für das fokussierte Pane und bleibt an genau dessen `WebContents` gebunden. Docked DevTools liegt im zugehörigen `ContentsContainerView`; bei zu wenig Platz wird Undocking oder Resize angeboten, nicht still geschlossen oder umgehängt.
- Browser-Vollbild behält alle Panes. Tab-/Content-Vollbild zeigt vorübergehend nur das anfordernde Pane und stellt danach exakte Gruppe, Anordnung, Ratios und Fokus wieder her.

Accessibility und Lokalisierung:

- Split-Gruppe, Pane-Anzahl, Layout, geordnete Mitglieder und aktives Pane werden über macOS Accessibility exponiert.
- Jedes Pane wird lokalisiert als `Pane X von Y, Titel, Origin` angesagt.
- Divider exponieren Orientierung, Prozentwert, Minimum, Maximum und Tastaturänderung.
- Drag-Preview, akzeptierter Drop, Reordering, Abbruch und Ablehnung werden sichtbar sowie über VoiceOver angesagt.
- Alle Layoutnamen, Aktionen, Status- und Fehlermeldungen werden über GRIT/ICU vollständig auf Deutsch und Englisch gepflegt.

### Workspaces

- Alle Workspaces verwenden dasselbe normale Browserprofil.
- Cookies, Logins, Verlauf, Extensions, Downloads, Passwortspeicher und Site Permissions sind gemeinsam.
- Seitenbaum, temporäre Fenstersitzungen, aktive Auswahl und Akzent sind Workspace-bezogen.
- Wechsel über Sidebar, Tastatur und horizontale Zwei-Finger-/Magic-Mouse-Geste innerhalb der Sidebar.
- Richtung, Empfindlichkeit und Deaktivierung der Geste sind konfigurierbar.
- Im kompakten Workspace-Kopf ist der aktive Workspace mit Icon und Name sichtbar; jeder inaktive Workspace wird daneben als Dot in stabiler Reihenfolge dargestellt.
- Hover oder Tastaturfokus auf einem Dot zeigt ohne sofortigen Wechsel einen kompakten aktiven Vorschauzustand mit Icon, Name und optionalem Akzent, damit erkennbar ist, welcher Space dahinterliegt. Klick beziehungsweise bestätigter Fokus wechselt den Workspace.
- Die Workspace-Geste animiert den Übergang direkt und abbrechbar; Dots, aktive Identität, Baum und WebContents-Fokus bleiben synchron und springen nicht erst nach Abschluss in einen widersprüchlichen Zustand.
- Die Workspace-Geste startet nur, wenn ihr Pointer in der sichtbaren beziehungsweise per Edge-Reveal geöffneten Sidebar liegt. Im WebContents bleiben Chromiums Zurück-/Vor-Geste und horizontaler Website-Scroll unverändert; eine Eingabe darf niemals beide Aktionen auslösen.
- Mehrere Fenster werden unterstützt.
- Der gespeicherte Baum ist fensterübergreifend gemeinsam.
- Temporäre Tabs und aktive Auswahl bleiben fensterbezogen.
- Echte getrennte Browserprofile sind nicht Bestandteil von v1, das Datenmodell darf eine spätere Erweiterung aber nicht unnötig verhindern.

## Navigation, Command Bar und Fensterarten

### Command Bar

Implementiere eine native, latenzarme Command Bar:

- `⌘L`: Navigation/Suche im aktuellen Tab.
- `⌘T`: neuer temporärer Tab plus Command Bar.
- Suche über offene Tabs, gespeicherte Seiten, Ordner, Workspaces, Verlauf und Browserbefehle.
- URL-Erkennung.
- Fuzzy Ranking.
- vollständige Tastaturbedienung.
- lokale Resultate ohne Netzwerk.
- Remote-Suchvorschläge standardmäßig aus.
- Google als initiale, konfigurierbare Standardsuchmaschine.
- `g Suchbegriff` als direkter Google-Befehl.
- erweiterbares Suchkürzelmodell.
- Klick auf die sichtbare beziehungsweise per Notch eingeblendete Adressleiste öffnet dieselbe Command Bar im Kontext des aktiven Panes; es existiert kein zweiter abweichender URL-Editor.

### Quick Window im Stil von Little Arc

- konfigurierbarer globaler Default-Shortcut `⌥Space`;
- kleines fokussiertes Fenster;
- verwendet das normale Profil einschließlich Cookies, Logins und Extensions;
- kann eine Seite in normalen Tab oder Baum überführen;
- kann optional Ziel externer Links sein;
- ist kein Inkognito-Modus.

### Echtes Inkognito

- `⌘⇧N`;
- echtes `OffTheRecordProfile`;
- keine Speicherung in Verlauf, Baum, Sync oder Session Restore;
- keine Anzeige in der iOS-App;
- Extensions nur nach expliziter Inkognito-Freigabe;
- vollständige Trennung von normalen Cookies und Website-Speichern.

### Navigation und Gesten

- Chromiums auf macOS bereits vorhandene Zwei-Finger-/Magic-Mouse-Geste für Zurück beziehungsweise Vor bleibt unverändert erhalten und wird nur als Regression geprüft; AhoiBrowser implementiert dafür keinen zweiten Gestenpfad.
- Der eigene Workspace-Wechsel muss sich eindeutig von Chromiums vorhandener Seitennavigation und horizontalem Website-Scroll unterscheiden. Die Gesture-Arena entscheidet erst nach Schwelle eindeutig und löst niemals zwei Aktionen aus.
- `⌘` plus Scroll beziehungsweise Trackpad-Scroll wechselt mit Delta-Schwelle, Rate-Limit und sichtbarer Vorschau zyklisch zwischen den aktuell laufenden/aktiven Tabs des Workspaces; die Funktion ist konfigurierbar und kollidiert nicht mit Webseitenzoom, horizontalem Seiten-Scroll oder Systemgesten.
- Mittelklick in einer scrollbaren Webseite aktiviert Firefox-artiges Auto-Scrolling: Entfernung und Richtung des Cursors bestimmen kontinuierlich Richtung und Geschwindigkeit. Erneuter Mittelklick, primärer Klick, Escape, Tab-/Workspace-Wechsel oder Fokusverlust beendet es sofort.
- Auto-Scrolling bleibt im Renderer-/Input-Pfad sicher, respektiert nicht scrollbare Flächen, verschachtelte Scroller, Zoom, reduzierte Bewegung und Pointer-Lock und darf keinen Browser-Chrome-Drag auslösen.
- Workspace-Swipe, Tab-Cycling und Auto-Scroll sind jeweils abschaltbar und besitzen vollständige Tastatur-/Menüalternativen; Zurück/Vor folgt weiterhin Chromiums vorhandener macOS-Konfiguration und Tastatursteuerung.

## Vollwertige Browserfunktionen

Erhalte beziehungsweise integriere vollständig:

- Zurück, Vor, Reload und Hard Reload;
- Downloads mit Fortschritt, Pause/Resume, Abbruch, Historie und Finder-Integration;
- Datei- und Ordner-Uploads;
- Drag-and-drop;
- Drucken;
- PDF-Anzeige;
- Vollbild;
- Zwei-/Drei-/Vier-Pane-Split View mit simultan lebenden Seiten, vollständigem Sidebar-Drag-and-drop, Resize, Session Restore und Tab Restore;
- Standardbrowserregistrierung;
- HTTP-/HTTPS-URL-Handler;
- externe Links;
- sichere Custom-Protocol-Prompts;
- OAuth und SSO;
- Passkeys/WebAuthn;
- Autofill;
- lokalen Chromium-Passwortmanager;
- macOS-Keychain-Schutz;
- Anzeige gespeicherter Passwörter nur nach Touch ID/Systemauthentifizierung;
- Crash- und Session-Recovery;
- Chromiums Lifecycle-/Performance-Manager, Memory Saver und sicheres Tab-Discarding mit klaren Ausnahmen für aktive Tabs, Audio, Video, PiP, Capture/WebRTC, Downloads, nicht abgesendete Formulare, DevTools und andere kritische Zustände;
- DevTools und Entwicklermodus;
- PWA-Funktionen soweit upstream vorhanden, ohne prominenten v1-Schwerpunkt.

## Medien, DRM und Berechtigungen

Implementiere und verifiziere:

- HTML5 Audio/Video;
- MSE;
- Hardwaredecoding;
- VP9 und AV1 entsprechend Chromium und Hardware;
- H.264/AAC über einen rechtlich geklärten Build- und Distributionsweg;
- Vollbild;
- Picture-in-Picture;
- einen schlanken integrierten MiniPlayer in der Sidebar mit Play/Pause, Seek soweit von Media Session angeboten, Titel/Origin, Lautstärke/Mute und einem direkten Toggle zwischen MiniPlayer und Chromium-Picture-in-Picture;
- nachvollziehbaren Quellenwechsel bei mehreren abspielenden Tabs; der Benutzer wählt die Media Session, und der Player springt nicht ohne erklärbaren Grund zwischen Tabs oder Panes;
- MiniPlayer-Layout am unteren Sidebar-Ende als Overlay über dem scrollenden Tabinhalt mit dynamischem Scroll-Inset, sauberem Verhalten bei eingeklappter oder schwebender Sidebar und vollständiger Persistenz der gewählten Darstellungsart;
- Media Session;
- macOS-Medientasten;
- WebRTC;
- Kamera;
- Mikrofon;
- Bildschirm-, Fenster- und Tabfreigabe;
- Standort;
- Benachrichtigungen;
- Zwischenablage;
- Site Permissions und deren Rücksetzung.

### Widevine

- Widevine niemals aus einer Chrome-Installation kopieren.
- Kein proprietäres CDM ungeklärt in Repository oder Artefakten verteilen.
- Offiziellen Partner-/Lizenzprozess früh starten.
- CDM als separat lizenziertes und sicher aktualisierbares Modul behandeln.
- Signierung, Packaging, Version und Updatepfad dokumentieren.
- Ein privater Dogfood-Build darf ohne Widevine weiterentwickelt werden und muss den fehlenden Status sichtbar ausweisen.
- Der öffentliche Release bleibt blockiert, bis Netflix und mindestens ein zweiter Widevine-Dienst im offiziellen signierten Build funktionieren.

## Erweiterungen und Passwortmanager

Erhalte allgemeine Chromium-/Chrome-Extension-Kompatibilität:

- Chrome Web Store;
- MV3;
- Extension Service Worker;
- Content Scripts;
- Browser Actions;
- Popups;
- Optionsseiten;
- Commands;
- Storage;
- Native Messaging;
- entpackter Entwicklermodus;
- Installation, Update, Neustartpersistenz und Deinstallation.

Gepinnte Extension-Actions erscheinen kompakt im Sidebar-Kopf; weitere Actions liegen in einem Overflow-Menü.

Bei eingeblendeter schwebender Navigationszeile erscheinen gepinnte Extension-Actions innerhalb derselben Overlay-Zeile rechts von der Adresse. Das Ein-/Ausblenden verändert weder die Höhe noch die Viewportmetriken des WebContents. Popup, Badge, Kontextmenü und Tastaturbefehl einer Extension bleiben vollständig funktionsfähig; Extensions dürfen weiterhin nicht AhoiBrowsers eigenes Chrome verändern.

Pflichtkompatibilität:

- 1Password;
- Bitwarden;
- React DevTools oder vergleichbare Entwicklererweiterung;
- eine normale MV3-Produktivitätserweiterung;
- IBM Equal Access Accessibility Checker als externe Erweiterung.

Der lokale Chromium-Passwortmanager bleibt nicht nur technisch vorhanden, sondern als schlanke, vollständige Produktfläche erreichbar: Passwörter speichern, mehrere Konten pro Origin auswählen, suchen, bearbeiten, löschen, importieren/exportieren im sicheren Chromium-Rahmen, Passwortprüfung soweit upstream verfügbar und Passwörter erst nach Touch ID beziehungsweise Systemauthentifizierung im Klartext anzeigen. Autofill, Passkeys und HTTP-Auth-Credentials bleiben korrekt getrennte Datendomänen. AhoiBrowser bevorzugt keinen externen Passwortanbieter. 1Password, Bitwarden und andere Chromium-Passwortmanager erhalten denselben Extension- und Native-Messaging-Pfad. Deren Tresore oder Extension Storage werden niemals durch AhoiBrowser Sync übertragen.

1Password muss mit einer signierten App in `/Applications`, einmaliger Freigabe als zusätzlicher vertrauenswürdiger Browser, Desktop-App-Verbindung, Touch ID und einem künstlichen Test-Vault real geprüft werden.

Für die konkrete Dogfood-Abnahme werden Erweiterungen ausschließlich im AhoiBrowser-Profil installiert, eingerichtet und geprüft. Google Chrome und Arc sind dabei nur mögliche, strikt read-only Inventarquellen und dürfen weder verändert noch als Laufzeitnachweis für AhoiBrowser gewertet werden. Pflichtkandidaten sind uBlock Origin Classic (`cjpalhdlnbpafiamejdnhcphjbkeiagm`) über den signierten Ahoi-Sonderpfad, AnyChat (`khpefodpgnkegiohbolbaaeabnfdegln`) und 1Password Stable (`aeblfdkhhhdcdjpifhhbdiojplfjncoa`) über den normalen Chrome-Web-Store-/Chromium-Pfad. Jede Installation zeigt Quelle, Identität und Berechtigungen sichtbar an und benötigt eine bewusste Bestätigung; es gibt keine stille Installation, keine pauschale Allowlist und kein Übertragen von Extension Storage oder Kontositzungen.

1Password-Native-Messaging wird nicht aus einem fremden Browserprofil kopiert. Die 1Password-Desktop-App muss die signierte Ahoi-App über ihren offiziellen Additional-Browsers-Prozess als vertrauenswürdig aufnehmen und das Manifest für Ahois eigenes Profil beziehungsweise den unterstützten systemweiten Hostpfad selbst provisionieren. Login, Entsperren und Touch ID bleiben nutzerassistiert; AhoiBrowser oder die Testautomation lesen, speichern oder protokollieren keine realen Tresorgeheimnisse.

## Migration aus Arc

Implementiere einen erstklassigen, wiederholbaren Assistenten `Aus Arc importieren`. Er migriert Arc-Seitenleisten- und Browserdaten in das vorhandene Ahoi-Domänenmodell; er kopiert niemals ein komplettes fremdes Chromium-Profil und führt Arc-Code nicht aus.

### Discovery und sicherer Snapshot

- Erkenne die installierte Arc-App, alle auswählbaren Arc-Profile und mindestens `~/Library/Application Support/Arc/StorableSidebar.json` sowie `~/Library/Application Support/Arc/User Data/<Profil>`.
- Prüfe anhand realer Prozesse und geöffneter Datenbanken, ob Arc noch läuft. Verwaiste `Singleton*`-Symlinks sind kein ausreichender Laufzeitbeleg.
- Fordere zum Schließen von Arc auf, bevor ein mutierbarer Profilstand übernommen wird.
- Erzeuge vor Parser oder Import einen unveränderlichen Snapshot mit Manifest, Quellpfaden, Größen, Zeitstempeln und SHA-256. SQLite-Datenbanken werden zusammen mit passenden WAL-/SHM-Dateien konsistent gesichert.
- Weise Symlinks, Pfadtraversal, Gerätepfade, übergroße Dateien und unerwartete Dateitypen fail-closed ab. Temporäre Snapshot- und Journaldateien erhalten mindestens Modus `0600` und werden nach erfolgreicher Evidenzbildung sicher entfernt.
- Schreibe niemals in Arc-Dateien. Ein Abbruch oder Fehler lässt sowohl Arc als auch das bestehende Ahoi-Profil unverändert.

### Unterstützte Abbildung

- Arc Spaces werden zu Ahoi-Workspaces.
- Arc Lists und verschachtelte Gruppen werden zu Ahoi-Ordnern; Reihenfolge und Hierarchie bleiben soweit valide erhalten.
- angeheftete und gespeicherte Arc-Tabs werden zu gespeicherten Seiten; temporäre offene Tabs werden nur nach expliziter Kategorieauswahl als normale Ahoi-Tabs geöffnet.
- valide Arc Split Views werden nach Erzeugung echter Chromium-Tabs über `SplitViewService`, `TabStripModel` und Chromiums Split-Collection-Infrastruktur rekonstruiert. Orientierung, Mitglieder, Reihenfolge, Fokus und normalisierbare Divider-Ratios bleiben erhalten.
- Nicht rekonstruierbare oder unvollständige Splits werden verlustarm in einen klar benannten Ordner mit ihren sicheren Mitglieds-URLs degradiert; es entstehen keine Phantom-Tabs und kein separater WebView-/Split-State.
- Lesezeichen, Verlauf, Favicons, Suchmaschinen und kompatible Autofill-Metadaten dürfen über Chromiums Importer-Seams als getrennt auswählbare Kategorien importiert werden.

Passwörter, Cookies, Login Data, Web Sessions, Tokens, `Secure Preferences`, Extension Storage, Service-Worker-/Site-Storage, Native-Messaging-Manifeste, Keychain-Geheimnisse und Inkognito-Daten werden niemals direkt aus Arc kopiert. Für Passwörter ist ausschließlich ein ausdrücklich vom Nutzer erzeugter, von Chromium sicher unterstützter Export-/Importweg zulässig.

### Parser, Vorschau und Commit

- Der `StorableSidebar.json`-Parser ist versionsgebunden. Für Schema 1 verarbeitet er typisiert Container, Spaces, Lists, Tabs und Split Views; unbekannte Schema-Versionen oder Varianten werden nicht geraten.
- Setze harte Grenzen für Dateigröße, Objektanzahl, Stringlängen, Verschachtelung und Split-Mitglieder. Erkenne doppelte IDs, Zyklen, Waisen, ungültige URLs, interne Arc-/Extension-URLs und beschädigte Referenzen.
- Der Assistent zeigt vor jeder Mutation Kategorien, Objektzahlen, Ziel-Workspaces, Konflikte, Deduplizierungen, Degradierungen und ausgeschlossene Datentypen. Titel und URLs dürfen in dieser lokalen Nutzervorschau erscheinen, nicht aber unredigiert in Logs, Crash Reports oder veröffentlichter Evidenz.
- Erzeuge deterministische Ziel-IDs und einen `ImportPlan`, sodass derselbe Snapshot bei Wiederholung keine Duplikate erzeugt.
- Führe den Plan als additive, atomare Mehr-Workspace-Transaktion aus. Bei Fehler oder Prozessabsturz muss das `ImportJournal` vollständig zurückrollen oder beim Neustart deterministisch fortsetzen können; ein halb importierter Baum ist unzulässig.
- Überschreibe bestehende Ahoi-Inhalte nie still. Gleichnamige Workspaces und Ordner erhalten eine sichtbare Merge-, Umbenennen- oder Überspringen-Entscheidung.
- Der Ergebnisbericht nennt importierte, übersprungene, deduplizierte und degradierte Objekte sowie sicher ausgeschlossene Kategorien, ohne Geheimnisse offenzulegen.

Für die reale Dogfood-Migration wird der vorhandene lokale Arc-Datenstand zuerst unveränderlich gesichert, dann als Vorschau ohne Mutation geprüft, anschließend nach Bestätigung in das installierte AhoiBrowser-Profil importiert und direkt danach mit demselben Snapshot erneut ausgeführt. Der zweite Lauf muss nachweislich idempotent beziehungsweise ein erklärter No-op sein. Workspace-, Ordner-, Tab- und rekonstruierte Split-Ergebnisse werden sichtbar im installierten AhoiBrowser geprüft.

## Erstklassige HTTP-Authentifizierung für `.htaccess`

Implementiere eine eigenständige, hochwertige Verwaltung für HTTP-Authentifizierung, insbesondere Apache-`.htaccess`-/HTTP-Basic-Auth-Zugänge.

### Unterstützte Verfahren

- HTTP Basic Auth ist der primäre Anwendungsfall.
- HTTP Digest Auth verwendet denselben Credential-Chooser, soweit Chromium es unterstützt.
- Proxy-Authentifizierung bleibt getrennt von Server-Authentifizierung.
- NTLM und Negotiate verbleiben in Chromiums bestehenden Mechanismen und werden nicht mit Basic-/Digest-Zugängen vermischt.

### Korrekte Schutzbereiche

Zugangsdaten dürfen niemals nur anhand eines Hostnamens ausgewählt werden. Berücksichtige mindestens:

- Zieltyp `server` oder `proxy`;
- URL-Schema `http` oder `https`;
- Host;
- Port;
- Auth-Schema;
- Realm;
- Chromiums Protection-Space- und Pfadregeln;
- Profile-/Network-Kontext und Network Anonymization Key, soweit für Cache-Reuse relevant.

Keine Wiederverwendung zwischen unterschiedlichen Origins, Ports, Realms, Profilen oder unzulässigen Pfadbereichen.

### Nativer HTTP-Auth-Dialog

Ersetze den primitiven Dialog durch eine native AhoiBrowser-Oberfläche mit:

- deutlich sichtbarem Host und Port;
- Realm;
- Auth-Schema;
- HTTPS-/Unsicher-Status;
- Benutzername als durchsuchbarem Kombinationsfeld;
- Auswahl aller für exakt diesen Schutzbereich gespeicherten Konten;
- Tastatursteuerung und Autocomplete;
- zuletzt erfolgreich verwendetem Konto als Vorauswahl;
- maskiertem Passwortfeld;
- Möglichkeit, neue Daten einzugeben;
- Aktionen `Speichern`, `Aktualisieren`, `Nicht jetzt` und `Für dieses Realm nie speichern`;
- verständlichem Fehlerzustand nach erneutem `401`;
- Wechsel auf ein anderes Konto nach fehlgeschlagenem Versuch;
- keinem automatischen Löschen eines gespeicherten Eintrags nach einem einzelnen Fehler.

Bei genau einem gespeicherten Konto wird es vorausgewählt und ausgefüllt, aber standardmäßig erst nach Bestätigung gesendet. Bei mehreren gespeicherten Konten muss die Auswahl immer sichtbar erreichbar sein.

Optional kann der Nutzer für ein konkretes HTTPS-Realm `Automatisch verwenden` aktivieren. Persistente Daten dürfen dabei erst nach einer passenden Server-Challenge eingesetzt und nie ungefragt an einen anderen Schutzbereich gesendet werden.

### Speicherung

- Verwende Chromiums Password Store, `HttpAuthManager` und `LoginHandler`.
- Erstelle keine zweite unverschlüsselte Credential-Datenbank.
- Schütze Secrets über Chromiums macOS-/Keychain-Integration.
- Speichere erst nach nachweislich erfolgreicher Authentifizierung.
- Biete bei geändertem Passwort `Gespeicherten Zugang aktualisieren` an.
- Erlaube mehrere Benutzernamen für dasselbe Realm.
- Erlaube ein bevorzugtes Konto pro Realm.
- Synchronisiere HTTP-Auth-Passwörter niemals über AhoiBrowser CloudKit.
- Schreibe weder Passwort noch vollständigen Authorization-Header in Logs, Crash Reports, NetLog-Evidenz oder Screenshots.

### Verwaltung

Ergänze unter `Passwörter und Authentifizierung` einen Bereich `HTTP-Zugänge`:

- Suche nach Host, Realm oder Benutzername;
- Gruppierung nach Origin und Realm;
- Anzeige von Auth-Schema, Port und letztem erfolgreichen Einsatz;
- bevorzugtes Konto ändern;
- Benutzername bearbeiten;
- Passwort aktualisieren;
- einzelnen Zugang löschen;
- alle Zugänge eines Realms löschen;
- Passwortanzeige nur nach Touch ID/Systemauthentifizierung;
- kein ungeschützter Klartext-Export.

Webformular-Passwörter und HTTP-Auth-Zugänge müssen klar unterscheidbar bleiben.

### Konto wechseln und abmelden

Ergänze im Site-Panel und in der Command Bar:

- `HTTP-Anmeldung wechseln`;
- `HTTP-Anmeldung für diese Website vergessen`;
- `Gespeicherte HTTP-Zugänge verwalten`.

`HTTP-Anmeldung wechseln` muss:

1. den flüchtigen HTTP-Auth-Cache originbezogen leeren;
2. betroffene Verbindungen sicher schließen;
3. die Seite neu laden;
4. den Credential-Chooser erneut anzeigen.

Verwende dafür den Network-Service-Pfad `ClearHttpAuthCache` mit engem Origin-Filter. Das Leeren einer aktiven Auth-Sitzung darf gespeicherte Zugangsdaten nicht löschen. `Gespeicherten Zugang löschen` und `Aktuelle HTTP-Auth-Sitzung beenden` sind getrennte Aktionen.

### Unsicheres HTTP

HTTP Basic Auth über unverschlüsseltes HTTP überträgt Zugangsdaten nicht vertraulich. Deshalb:

- deutliche Warnung im Dialog;
- kein automatisches Login über HTTP;
- Speichern nur nach expliziter Bestätigung;
- HTTP bleibt für lokale Entwicklungs- und Legacy-Systeme möglich;
- kein falscher Eindruck, Keychain-Speicherung sichere die Übertragung;
- HTTPS-Zugangsdaten niemals für dieselbe Domain über HTTP wiederverwenden.

### Inkognito und Erweiterungen

- Inkognito darf vorhandene HTTP-Zugänge nur nach expliziter Auswahl verwenden.
- Keine automatische Auswahl in Inkognito.
- Neue oder geänderte HTTP-Zugänge werden in Inkognito nie gespeichert.
- Auth-Cache wird beim Schließen des letzten Inkognito-Fensters verworfen.
- Keine HTTP-Auth-Nutzung aus Inkognito erscheint in Verlauf, Sync oder Companion.
- 1Password und Bitwarden bleiben für normale Webformulare unterstützt.
- Erfinde keine unsichere DOM-Injection oder proprietäre Extension-API für den nativen Auth-Dialog.
- Eine spätere Credential-Provider-Schnittstelle ist nur zulässig, wenn ein sicherer Upstream-Standard und reale Anbieterunterstützung existieren.

## Klassisches uBlock Origin ohne eingebauten Adblocker

Baue keinen eigenen Adblocker, keine eigene Filterlisten-Engine und keine Brave-Shields-Kopie.

Erhalte ausschließlich für **uBlock Origin Classic** die kleinstmögliche, wartbare MV2-Kompatibilität:

- Allgemeines Manifest V2 bleibt im Release deaktiviert.
- Nur feste, browserseitig erlaubte Extension-IDs erhalten die Ausnahme.
- Die Ausnahme prüft zusätzlich signierten Katalog, erlaubte Herkunft, Version und Hash.
- Erhalte nur die tatsächlich von uBO benötigten MV2-Background-Page-, Lifecycle- und blocking-`webRequest`-Funktionen.
- Gewöhnliche MV3-Erweiterungen bleiben unverändert.
- Webseiten oder beliebige Extension-Pakete dürfen die Ausnahme nicht beanspruchen.

Distribution:

- uBO ist nicht vorinstalliert und nicht automatisch aktiviert.
- Eine optionale Ein-Klick-Installation wird in AhoiBrowser angeboten.
- Vor Installation zeigt die UI Version, Upstream-Quelle, Extension-ID und GPL-Lizenz.
- Das Paket wird reproduzierbar und unverändert aus einem festen offiziellen Upstream-Tag erzeugt.
- Ein signierter AhoiBrowser-Katalog liefert Metadaten und erwartete Hashes.
- Ein eigener sicherer Updatepfad aktualisiert das Extension-Paket, weil der Chrome Web Store uBO Classic nicht mehr verteilt.
- Filterlistenupdates bleiben uBOs eigener Mechanismus.

Pflichttests:

- Installation per Nutzerklick;
- Netzwerkfilterung;
- kosmetische Filter;
- eigene Filterregel;
- Dashboard;
- Neustartpersistenz;
- Filterlistenupdate;
- Extension-Update;
- Deinstallation;
- Ablehnung eines nicht allowlisteten MV2-Pakets;
- unveränderte Funktion gewöhnlicher MV3-Erweiterungen.

Wenn die Funktion nur durch eine allgemeine MV2-Freigabe oder eine große, unwartbare Extension-Forkfläche möglich wäre, dokumentiere ein Architektur-No-Go. Implementiere keinen unsicheren Workaround.

## Integriertes Developer Toolkit

Das Toolkit ist eine native Browserfunktion und keine Extension. Es ist für normale Nutzer standardmäßig verborgen und wird einmalig in den Einstellungen oder über einen Befehl aktiviert.

Wenn es deaktiviert ist:

- keine zusätzlichen Compilerprozesse;
- keine dauerhaften Renderer;
- keine DOM-Beobachter;
- keine messbare eigene Idle-CPU-Last;
- keine unnötige Speicherbelegung.

### Live-Editor

- CSS, LESS und SASS;
- Sofortvorschau;
- Syntaxfehleranzeige;
- einmalige oder persistente Regeln;
- Scope für aktuellen Tab, Origin, Domain oder Pfad;
- Reload- und Neustartpersistenz entsprechend Einstellung;
- lazy-loaded JS-/WASM-Compiler in einem sandboxed Utility-Kontext;
- kein Compiler im Browserprozess.

### JavaScript-Injection

- einmalige oder persistente Ausführung;
- Isolated World als Default;
- Main World nur nach sichtbarer Warnung und expliziter Auswahl;
- dieselben URL-Scopes wie der Style-Editor;
- klar sichtbarer Aktivzustand;
- definierte Rücksetzung bei Navigation, Tab-Schließung und Scope-Wechsel.

### Cache und Site Data

- normales Reload;
- Hard Reload;
- Cache für aktiven Tab deaktivieren;
- Cache der aktuellen Site leeren;
- globalen Cache nur nach Bestätigung leeren;
- Cookies;
- Local Storage;
- Session Storage;
- IndexedDB;
- Cache Storage;
- Service Worker;
- einzelne oder sämtliche Site-Daten gezielt zurücksetzen.

### Cookie Manager

- Suche;
- Erstellen, Bearbeiten und Löschen;
- Domain und Path;
- Ablaufzeit;
- Secure;
- HttpOnly;
- SameSite;
- Partitioned/CHIPS;
- korrekte Trennung normaler und Inkognito-Kontexte;
- Import/Export nur, wenn dies sicher und ohne unnötigen Ballast umgesetzt werden kann.

### Headerregeln

- Request- und Response-Header;
- benannte Profile;
- URL-Scope;
- temporär oder persistent;
- CSP-/CORS-Manipulation ausschließlich im sichtbar gekennzeichneten Advanced-Modus;
- Warnung vor möglichen Sicherheits- und Kompatibilitätsfolgen;
- als geheim markierte Werte ausschließlich im macOS-Keychain;
- keine Secrets in Sync, Logs, Crash Reports oder Evidenz.

### Schlanke Web-Developer-Funktionen

- JavaScript pro Seite deaktivieren;
- CSS pro Seite deaktivieren;
- Bilder pro Seite deaktivieren;
- Elemente, Überschriften und ARIA-Landmarks umranden;
- Alt- und Title-Texte einblenden;
- Meta-Tags, Canonical, OpenGraph und strukturierte Daten anzeigen;
- Dokumentinformationen und Seitenquelltext öffnen;
- Passwortfelder der aktuellen Website sichtbar beziehungsweise wieder maskiert schalten;
- Passwortfelder bei Navigation und Tab-Schließung automatisch wieder maskieren;
- gespeicherte Browserpasswörter ausschließlich nach Touch ID/Systemauthentifizierung anzeigen.

Zeige aktive Zustände kompakt als Chips, beispielsweise:

- `CSS`;
- `JS`;
- `HDR`;
- `CACHE OFF`;
- `PRIVACY OPEN`.

Biete eine einzelne sichere Aktion `Alle Seitenmodifikationen zurücksetzen`.

Developer Assets bleiben standardmäßig lokal. Jedes Snippet und Headerprofil kann einzeln für verschlüsselten Sync freigegeben werden. Keychain-Geheimnisse bleiben immer lokal.

Nicht nachbauen:

- vollständigen Accessibility-Scanner;
- Network Inspector;
- Performance Profiler;
- Responsive Mode;
- Grid-/Flex-Inspektoren;
- umfangreiche Formularanalyse.

Diese Funktionen bleiben in Chrome DevTools oder spezialisierten externen Erweiterungen wie IBM Equal Access Accessibility Checker.

### Unveränderliche Bestätigung für Datenlöschung

Jede Cache-/Cookie-/Website-Daten-Löschung erzeugt vor der Bestätigung ein unveränderliches Request-Objekt aus Scope (`aktuelle Website` oder `alle Websites`), Datentypen und Zeitraum. Die Bestätigung führt exakt dieses Snapshot-Objekt höchstens einmal aus; nachträgliche UI-Änderungen, Doppelklick oder doppelte Callback-Zustellung dürfen Scope, Typen oder Text nicht verändern und keine zweite Löschung auslösen. Erfolg, Teilerfolg und Fehler nennen exakt den tatsächlich ausgeführten Scope, Zeitraum und die gelöschten Typen. Eine globale Löschung darf niemals als `für diese Seite gelöscht` gemeldet werden.

## Privacy ohne Funktionsverlust

Implementiere zwei klare Datenschutzmodi.

### `Mehr Schutz` – explizit aktivierbarer Zusatzschutz

`Mehr Schutz` soll Entwickler nicht unnötig einschränken und normale Websites weitgehend funktionsfähig halten. Der Nutzer aktiviert diesen Modus bewusst global oder pro Website. Die UI erklärt in einem Satz, dass zusätzliche Ahoi-Schutzmaßnahmen aktiv sind und einzelne eingebettete Drittanbieter-Inhalte gegebenenfalls eine Website-Ausnahme benötigen:

- First-Party-Cookies und normale Logins funktionieren vollständig.
- Unpartitionierte Third-Party-Cookies werden blockiert.
- CHIPS/partitionierte Cookies und Storage Access werden unterstützt.
- HTTPS-First ist aktiv.
- Global Privacy Control ist aktiv.
- bekannte Trackingparameter werden konservativ entfernt.
- Cross-Site-Referrer werden reduziert.
- Topics, Protected Audience und Attribution Reporting sind deaktiviert.
- Fingerprinting-Schutz greift zunächst gezielt in Drittanbieter-Kontexten.
- keine globale User-Agent- oder Web-API-Fälschung, die Entwicklerseiten unnötig bricht.
- keine Produkttelemetrie.
- keine Usage-Pings.
- keine automatischen Crash-Uploads.
- keine ungefragten Experimente.
- Remote-Suchvorschläge standardmäßig aus.
- unnötige Google-Hintergrunddienste und spekulative Verbindungen aus.

### `Maximale Website-Kompatibilität`

Dieser Modus ist bei einem frischen Profil der globale Default und kann zusätzlich pro Origin aktiviert werden. Die UI erklärt statt eines internen Technikbegriffs, dass Ahois zusätzliche Eingriffe für diese Website ausgesetzt werden, Chromiums Sicherheitsgrenzen jedoch aktiv bleiben. Interne Enum-/Policy-Namen dürfen aus Migrationsgründen unverändert bleiben, werden aber nicht ungeklärt als Nutzertext angezeigt:

- deaktiviert AhoiBrowsers zusätzliche Cookie-, URL-, Referrer- und Fingerprinting-Eingriffe für die betreffende Website;
- verändert nicht uBlock oder andere Erweiterungen;
- deaktiviert niemals Sandbox, Site Isolation, Zertifikatsprüfung, Safe Browsing oder Downloadwarnungen;
- wird sichtbar im Site-Panel und über `PRIVACY OPEN` angezeigt;
- ist in DevTools beziehungsweise Diagnoseinformationen erkennbar;
- kann vollständig zurückgesetzt werden.

Keiner der beiden Modi ist ein Werbe- oder Contentblocker. Ahoi blockiert keine
Script-, Bild-, Frame-, Analyse- oder Werbehosts durch eine zweite interne
Filterengine. Umfangreiche Request- und kosmetische Filterung bleibt der separat
installierten Erweiterung uBlock Origin vorbehalten; entsprechend muss eine
normale Website wie `winfuture.de` einschließlich benötigter Drittressourcen wie
`html-load.com` bei deaktiviertem uBlock im Defaultmodus funktionieren.

### Safe Browsing

- Standard Protection aktiv.
- Enhanced Protection aus.
- lokale Bedrohungslisten und privacy-preserving Lookups.
- Falls ein unabhängiger Browser einen Proxy benötigt, implementiere einen quelloffenen, zustandslosen, nicht protokollierenden Projekt-Proxy.
- Kein stiller Rückfall auf direkte Übertragung vollständiger URLs.
- dokumentiere alle verwendeten Endpunkte und Datenfelder.

### Fresh-Profile-Netzwerkaudit

Ein frisches Profil darf ohne Nutzeraktion nur dokumentierte Verbindungen aufbauen, beispielsweise für:

- Browserupdate;
- notwendige Chromium-Sicherheitskomponenten;
- Safe Browsing;
- CloudKit, sofern Sync aktiv ist.

Erstelle eine maschinenlesbare Endpoint-Allowlist und einen dynamischen Netzwerktest. Jeder neue Hintergrundendpoint ist ein Security-/Privacy-Review-Ereignis. Teste den Browserschutz zusätzlich mit deaktiviertem uBlock, damit Browser- und Extension-Schutz nicht verwechselt werden.

### Security-Regeln

- Keine privilegierten Bindings für normale Webseiten.
- Interne WebUIs nur auf dedizierten internen Origins.
- Mojo-Interfaces minimal und streng validiert.
- Keine Sandbox-Deaktivierung.
- Keine Secrets im Repository.
- Header-, Injection-, HTTP-Auth-, Sync-, Remote-Control- und Updatefunktionen benötigen Threat Model und Security Review.
- Remote Commands erlauben keine Shellbefehle und keine beliebigen Custom Schemes.
- Update-, Extension- und uBO-Kataloge werden signiert.
- manipulierte Artefakte werden sicher abgewiesen.

## CloudKit-Sync

### Grundprinzip

- local-first;
- Browser und Companion funktionieren ohne iCloud und ohne Netzwerk mit dem lokalen Datenstand;
- das lokale Ahoi-Domänenmodell, sein SQLite-Store und seine Outbox sind die kanonische Quelle; CloudKit ist Transport und niemals das einzige Datenlager;
- austauschbare `SyncProvider`-Schnittstelle;
- v1 implementiert `CloudKitSyncProvider`;
- CloudKit Private Database;
- eigene Custom Record Zone;
- `CKSyncEngine` auf unterstützten Apple-Systemen mit persistiertem opakem Engine-State, automatischen Fetch-/Send-Zyklen, Retry/Backoff und explizitem Sofort-Sync nach einer Nutzeraktion;
- `CKSyncEngine`-Accountwechsel, Zone-Reset, partielle Fehler und `serverRecordChanged` werden als explizite Zustände behandelt; Konflikte werden im Ahoi-Domänenmodell zusammengeführt und nicht blind durch CloudKit aufgelöst;
- native CloudKit-Schnittstellen der Zielsysteme;
- Objective-C++-Bridge auf macOS;
- Swift-/SwiftUI-Integration auf iOS/iPadOS.

Google-Web-Login, Google-Webseiten und Google als Suchmaschine bleiben normale Browserfunktionen. Chrome Account Sync ist davon strikt getrennt und kein unterstützter Distributionspfad für AhoiBrowser: Google beschränkt die privaten Chrome-Sync-APIs auf autorisierte Google-Produkte, und Chromium-API-Schlüssel beziehungsweise OAuth-Secrets dürfen weder eingebettet noch mit Forks geteilt werden. AhoiBrowser darf deshalb keine Google-Credentials mitliefern, keine private Chrome-Sync-API vortäuschen und keine UI anbieten, die ohne eine belastbare Konfiguration in einen Sign-in-Crash oder eine tote Synced-Tabs-Seite führt. Der zentrale Produkt-Policy-Schalter deaktiviert ausschließlich Chrome Account Sync; Ahoi Sync bleibt davon unabhängig aktivierbar.

Der native CloudKit-Adapter besitzt die notwendigen iCloud-/CloudKit- und Remote-Notification-Entitlements. Ohne iCloud-Konto darf er initialisieren, bleibt aber verständlich inaktiv und blockiert weder den Browserstart noch lokale Mutationen. Opaque `CKSyncEngine`-State, lokale Cursor, Outbox und Tombstones werden versionsfest persistiert. Sensible Nutzdaten liegen in `CKRecord.encryptedValues`; da solche Felder serverseitig nicht abfragbar sind, bleiben Suche, Filter und Sortierung konsequent in lokalen Indizes.

### Synchronisierte Daten

Synchronisiere:

- Workspaces;
- Ordner und gespeicherte Seiten;
- Reihenfolge und Tombstones;
- offene normale Tabs als gerätebezogene Sitzungen;
- Verlauf;
- Appearance und Workspace-Akzente;
- ausdrücklich freigegebene Einstellungen;
- Extension-Inventar;
- Developer Assets nur per einzelnem Opt-in.

Extension-Inventar darf auf einem neuen Mac nur Installationsvorschläge erzeugen. Installiere Erweiterungen niemals still.

Tabs anderer AhoiBrowser-Instanzen erscheinen direkt in der normalen Tab-Liste der Sidebar und niemals hinter einer separaten Geräteverwaltung, History-Unterseite oder zusätzlichen Geräte-Schaltfläche. Sie verwenden dieselbe kompakte Zeilenform, Typografie und Favicon-Darstellung wie lokale temporäre Tabs, tragen aber ein eindeutiges Smartphone-, Tablet- oder Desktop-Badge sowie zugänglichen Gerätetext. Gerätegrenzen dürfen durch eine dezente Zwischenüberschrift beziehungsweise Gruppierung sichtbar bleiben, ohne eine zweite Navigationshierarchie zu erzeugen. Die Darstellung umfasst Gerät, Workspace, Titel, URL-Favicon und letzte Aktivität; sie lässt sich innerhalb der Sidebar lokal filtern. Ein Klick öffnet den Remote-Tab lokal, Kontextaktionen übernehmen ihn in einen Workspace oder fokussieren ihn über den sicheren Remote-Control-Pfad. Inkognito erscheint dort niemals. Ist kein `SyncProvider` verfügbar oder sind keine fremden Tabs vorhanden, gibt es weder leere Platzhalter noch einen funktionslosen Geräte-Button.

Diese Geräte-Tabs-Funktion darf Chromiums vorhandene Foreign-Session-Datenmodelle als Integrationsvorbild wiederverwenden, darf den Nutzer aber nicht in Chromiums separate History-/Synced-Tabs-Verwaltungsseite schicken. In v1 verwendet sie ausschließlich AhoiBrowsers `SyncProvider`/CloudKit-Daten. Sie darf weder eine Google-Anmeldung verlangen noch Chrome Sync heimlich aktivieren. Eine spätere zusätzliche Sync-Provider-Implementierung bleibt architektonisch möglich, ist aber kein v1-Releaseblocker.

Synchronisiere niemals:

- Cookies;
- Webformular- oder HTTP-Auth-Passwörter;
- Autofill- oder Formulardaten;
- Site Storage;
- Cache;
- Site Permissions;
- Extension Storage;
- Inkognito-Daten;
- lokale Split-Topologie einschließlich Fenster-/Workspace-Zuordnung, Pane-Reihenfolge, Layout, Divider-Ratios und Fokus;
- Keychain-Werte;
- geheime Headerwerte.

### Verschlüsselung und lokale Suche

- Verwende CloudKit `encryptedValues` für URLs, Titel, Verlauf, Tabs, Baumdaten und freigegebene Developer Assets.
- Verwende die private Datenbank.
- Suche und Sortierung erfolgen über lokale Indizes, weil verschlüsselte Felder serverseitig nicht suchbar sind.
- Keine eigenen kryptographischen Primitive erfinden.
- Dokumentiere Datenklassifikation, Schlüsselabhängigkeiten und Recovery-Verhalten.

### Konflikte

- stabile UUIDs;
- Hybrid Logical Clock;
- Geräte-ID als deterministischer Tie-Breaker;
- verteilbare Ordnungsschlüssel;
- Last-writer-wins nur auf einzelnen skalaren Feldern;
- History append-only mit Deduplizierung über Visit-ID;
- Tombstones für 30 Tage;
- deterministische Moves und Deletes;
- Zyklenschutz;
- unauflösbare Baumkonflikte sichtbar in einen Ordner `Wiederhergestellt` retten;
- kein stilles Überschreiben oder Verwerfen.

Verlauf:

- Default-Aufbewahrung 90 Tage;
- Optionen 30, 90 und 365 Tage oder unbegrenzt;
- Löschen wird auf alle Geräte propagiert.

Gerätesitzungen bleiben getrennt und werden nicht zu einer einzigen Tab-Liste vermischt.

### Recovery und Open-Source-Boundary

Implementiere klare Zustände für:

- iCloud nicht angemeldet;
- Offlineänderungen;
- Quota;
- Accountwechsel;
- gelöschte Zone;
- Reset des verschlüsselten iCloud-Schlüsselmaterials;
- Schemaänderung;
- Geräteentzug.

Ein lokales Gerät kann nach bestätigtem Recovery seinen lokalen Datenstand neu hochladen. Keine automatische Datenvernichtung bei CloudKit-Key- oder Zone-Fehlern.

Offizielle Builds verwenden den Projekt-CloudKit-Container. Selbst gebaute Forks müssen eigene Apple-Team-, Bundle- und Container-Identifier konfigurieren können.

## Nativer iOS-/iPadOS-Browser

Baue eine native SwiftUI-Browser-App für iOS/iPadOS 26+. Sie verwendet Apples systemgeliefertes WebKit über `WebView`/`WebPage`, keinen Chromium-Port und keine alternative Browser-Engine. Der bisherige Companion wird als Workspace-, Sync- und Remote-Control-Core integriert und nicht als zweites App-Produkt fortgeführt. Der vollständige Vertrag steht in `outputs/AhoiBrowser-Mobile-Zielprompt.md`.

Funktionen:

- Volltextsuche über synchronisierte Workspaces, Baum, normale offene Tabs und Verlauf;
- Workspaces und Ordner anlegen, umbenennen, verschieben und löschen;
- gespeicherte Seiten anlegen, verschieben, umbenennen und löschen;
- Webseiten im eigenen normalen oder privaten Browserkontext öffnen;
- als verwalteter iOS-Standardbrowser HTTP-/HTTPS-Links annehmen;
- Adress-/Suchfeld, Back/Forward/Reload, Tabs, Undo Close und normalen Session Restore bereitstellen;
- Link an einen konkreten Mac oder Workspace senden;
- normalen Mac-Tab öffnen;
- normalen Mac-Tab fokussieren;
- normalen Mac-Tab nach Bestätigung schließen;
- Gerätestatus und Befehlsstatus anzeigen;
- Inkognito vollständig ausblenden.

### Remote Control

- Remote Control ist pro Mac abschaltbar.
- Ein neues iOS-Gerät muss am Mac bestätigt werden.
- Jedes Gerät besitzt einen Signierschlüssel im Keychain.
- Befehle enthalten Zielgerät, Befehlstyp, Nonce, Timestamp und Ablaufzeit.
- TTL ist fünf Minuten.
- Signaturprüfung, Replay-Schutz und Idempotenz sind Pflicht.
- Statuswerte: `queued`, `delivered`, `executed`, `failed`.
- Offlinebefehle dürfen innerhalb der TTL warten.
- Erlaubte Navigation beschränkt sich auf sichere HTTP-/HTTPS-URLs.
- Keine Shellbefehle.
- Keine beliebigen Custom Schemes.
- Kein `Alle Tabs schließen` in v1.
- Inkognito ist weder auffindbar noch adressierbar.

## Build, Signierung und Distribution

### Releasebuild

- echter optimierter ARM64-Build;
- `is_component_build=false`;
- Release-Sandbox und Site Isolation aktiv;
- Hardwarebeschleunigung aktiv;
- Symbole getrennt archivieren;
- eigenes Branding, kein Chrome-Branding und keine Google-Trademarks;
- proprietäre Codecflags nur zusammen mit rechtlich geklärter Distribution.

### App-Bundle und DMG

- eigenständiges `.app`-Bundle;
- Frameworks und Helper korrekt eingebettet;
- Renderer-, GPU-, Utility- und Crash-Prozesse korrekt signiert;
- minimale Entitlements;
- Hardened Runtime;
- Developer-ID-Signierung;
- Notarisierung;
- Stapling;
- DMG;
- reale Installation nach `/Applications/AhoiBrowser.app`.

### Updater

Verwende Sparkle 2 oder einen gleichwertigen nativen und auditierbaren Updater:

- Ed25519-signierter Appcast;
- Kanäle `nightly`, `beta`, `stable`;
- Delta- und Full-Update;
- atomarer Austausch;
- sauberer Neustart;
- Recovery bei Downloadabbruch und Netzverlust;
- sichere Ablehnung manipulierter Manifeste und Pakete;
- reale Updates von N−2 und N−1 auf aktuellen Release Candidate;
- keine Update-Telemetrie.

### Releaseartefakte

Veröffentliche je Release:

- AhoiBrowser-Version und Buildnummer;
- Git-SHA;
- Chromium-Commit;
- Patchset-Version;
- Checksums;
- SBOM;
- Third-Party Notices;
- Lizenztexte;
- bekannte Abweichungen;
- Status externer Release-Gates;
- signiertes Evidenzmanifest.

## Open Source, Lizenz und Marke

- Eigener Browser-, Sync- und Companion-Code: GPL-3.0-or-later.
- Chromium- und Drittanbieterdateien behalten ihre Originallizenzen.
- Beiträge erfolgen per Developer Certificate of Origin.
- Verwende kein CLA, das eine beliebige geschlossene Relizenzierung erlaubt.
- Name und Logo erhalten eine klare Trademark-Policy.
- Öffentliche Forks müssen rebranden.
- Versprich nicht, kommerzielle Nutzung verbieten zu können; GPL verlangt bei verteilten abgeleiteten Werken die Offenlegung des korrespondierenden GPL-Quellcodes.
- Prüfe iOS-App-Store-Distribution, GPL-§7-Zusatzfreigaben, Widevine, H.264/AAC und sonstige proprietäre Komponenten juristisch.
- Formuliere keine selbst entworfene Lizenzausnahme als endgültig rechtssicher.
- Proprietäre CDM-Binaries bleiben getrennt vom öffentlichen Quellcode.
- uBlock Origin bleibt eine separate GPL-lizenzierte Erweiterung.

## Implementierungsphasen

### Phase 0 – Machbarkeit, Umgebung und Recht

1. Inventarisiere Workspace, Hardware, RAM, freien Speicher, Xcode, SDKs, Signieridentitäten, verfügbare iOS-Geräte, Git/GitHub-Zugriff und vorhandene Chromium-Caches.
2. Initialisiere Repository, GPL, DCO, zentrale Branding-Konfiguration und Dokumentationsstruktur.
3. Pinne und baue unverändertes Chromium ARM64.
4. Erzeuge einen minimal gebrandeten AhoiBrowser mit echter Prozessarchitektur.
5. Verifiziere Sandbox, Site Isolation, Renderer, GPU, Network Service und DevTools.
6. Erzeuge den ersten installierbaren Dogfood-Build.
7. Verifiziere reale MV3-Extension-Installation.
8. Verifiziere Native Messaging mit 1Password oder dokumentiere exakt die noch fehlende Nutzerfreigabe.
9. Baue den minimalen selektiven uBO-MV2-Prototyp.
10. Verifiziere CloudKit-Encrypted-Fields-Roundtrip zwischen Mac und iOS.
11. Verifiziere AppKit-/Liquid-Glass-Integration.
12. Verifiziere einen ersten HTTP-Basic-Auth-Flow über Chromiums Password Store.
13. Starte Widevine-, Codec- und Lizenzklärung.

Erstelle am Ende nicht nur einen Bericht. Wenn die technische Grundrichtung tragfähig ist, setze Phase 1 unmittelbar fort. Ein No-Go ist nur bei einem konkret nachgewiesenen Architekturproblem zulässig.

### Phase 1 – Build-, Patch-, CI- und Releasegrundlage

- gepinnter Bootstrap;
- GN-Integration;
- Patchverwaltung;
- reproduzierbare Upstream-/Ahoi-Vollbuild-Baseline für App-/DMG-Größe, Mach-O-/Resource-Pack-Anteile, Start, Idle, Netzwerk, Prozesse, GPU und RAM;
- versionierte Lean-Chromium-Komponentenmatrix mit Abhängigkeitsgraph, Messwerten, Rollback und Roll-Checks;
- automatisierte Tests;
- Releasebuild;
- Packaging und Installation;
- Dogfood-Updater;
- Upstream-Roll;
- Security-/Privacy-Dokumentation;
- erster installierter Computer-Use-Smoke.

### Phase 2 – Browser-Chrome, Baum und Navigation

- native Hauptoberfläche;
- schwebende automatisch ausblendende Navigationszeile mit Reveal-Notch und Extension-Actions ohne WebContents-Reflow;
- abgerundeter, schattierter WebContents-Container, angedockte und schwebende Sidebar sowie vollständiges Glass-/Accessibility-Fallback-System;
- sichere Web-Popup-Overlays mit Schließen, Promotion in einen normalen Tab und Split-mit-Opener;
- Themes und Lokalisierung;
- persistenter Tree;
- vollständiges Sidebar-Drag-and-drop und echte Zwei-/Drei-/Vier-Pane-Split-Views einschließlich 2×2 und adaptiver Sidebar-Repräsentation;
- temporäre Tabs;
- Workspaces mit Swipe, aktiver Identität, inaktiven Dots und Hover-/Fokusvorschau;
- mehrere Fenster;
- Session Restore;
- Command Bar;
- Quick Window;
- Inkognito;
- eigene Magic-Mouse-/Trackpad-Geste für Workspaces, unveränderte Chromium-Zurück/Vor-Geste als Regression, `⌘`-Scroll-Tabwechsel und Mittelklick-Auto-Scrolling;
- Extension-Actions;
- Computer-Use-Abnahme aller sichtbaren Kernflows.

### Phase 3 – Browserfähigkeit, HTTP Auth und Extensions

- Downloads und Uploads;
- PDF/Drucken;
- Standardbrowser;
- Medien, Sidebar-MiniPlayer und PiP-Wechsel;
- WebRTC;
- Berechtigungen;
- lokaler Passwortmanager;
- Memory Saver, Tab-Discarding, sichtbare Schlafzustände und kritische Ausnahmen;
- erste updatesichere Entschlackungswelle ausschließlich für vollständig auditierte optionale Produktmodule; nach jedem Cluster erneut Größen-, Start-, Idle-, Netzwerk- und Kompatibilitätsmessung;
- vollständiger HTTP-Auth-Chooser und Verwaltung;
- Chrome Web Store;
- 1Password;
- AnyChat;
- Bitwarden;
- uBlock Origin Classic;
- sicherer Arc-Importassistent mit Vorschau, atomarem Commit, Rollback und idempotenter Wiederholung;
- realer Import des vorhandenen lokalen Arc-Datenstands nach unveränderlichem Backup;
- reale User-E2E-Abnahme.

### Phase 4 – Developer Toolkit und Privacy

- CSS/LESS/SASS/JavaScript;
- Cache und Site Data;
- Cookie Manager;
- Headerregeln;
- Diagnosewerkzeuge;
- Passwortfeldanzeige;
- nutzerverständlich benannter Modus `Mehr Schutz` beziehungsweise `Maximale Website-Kompatibilität`;
- Safe Browsing;
- Fresh-Profile-Netzwerkaudit;
- Computer-Use-Abnahme mit Fixtures und realen Seiten.

### Phase 5 – CloudKit und Companion

- Sync-Schema;
- Konfliktauflösung;
- Verlauf;
- gerätebezogene offene Tabs als native, gerätegekennzeichnete Zeilen direkt in der normalen Desktop-Sidebar;
- Developer-Asset-Opt-in;
- native iOS-/iPadOS-App;
- Remote Control;
- reale Zwei-Mac-plus-iOS-Abnahme.

### Phase 6 – Hardening und öffentlicher Release Candidate

- aktueller Chromium-Roll;
- Security Review;
- Privacy Review;
- Lizenzreview;
- Performance;
- finaler Lean-Chromium-Audit gegen identisches Upstream-Chromium und den dokumentierten Ahoi-Vollbuild einschließlich stiller Reaktivierungen nach dem aktuellen Roll;
- Accessibility;
- Crash- und Soak Tests;
- signierte N−2-/N−1-Updates;
- DRM;
- vollständige Computer-Use-Critical-Journey;
- Releaseevidenz;
- keine öffentlichen Stable-Binaries vor bestandenem Release-Gate.

## Verbindliches Testmodell

Jede Funktion erhält den höchsten erforderlichen Testlevel:

- `UNIT`: deterministische Logiktests.
- `INTEGRATION`: Chromium Browser Tests, lokale Testserver, Prozesse, Datenbanken und Services.
- `CU_E2E`: reale Bedienung des installierten Builds via Computer Use.
- `ASSISTED_E2E`: reale Hardware, Biometrie, Account oder physische Geste plus anschließende technische Verifikation.

`PASS` ist nur zulässig, wenn der höchste für den Testfall vorgesehene Level bestanden wurde.

### Computer-Use-Regeln

Vor dem ersten Computer-Use-Test muss die verfügbare Computer-Use-Skill-Anleitung vollständig gelesen und befolgt werden.

Computer Use bedient AhoiBrowser wie ein echter Nutzer:

- Maus und Klicks;
- Tastatur und Shortcuts;
- Menüs und Popover;
- Fenster und Workspaces;
- Finder;
- Systemeinstellungen;
- sichtbare Berechtigungsdialoge;
- installierte Extensions;
- echte Websites.

Terminal oder interne APIs dienen ausschließlich für Build, Prozess-, Netzwerk-, Dateisystem- und Datenbank-Readback. Sie dürfen keinen sichtbaren User-Flow umgehen.

Vor jedem CU-E2E-Lauf:

1. Git-SHA und Chromium-Revision festhalten.
2. Releaseartefakt bauen.
3. echten DMG-Installationsweg verwenden.
4. ausschließlich `/Applications/AhoiBrowser.app` starten.
5. ARM64 prüfen.
6. Bundle-ID, Version und Buildnummer prüfen.
7. Signatur, Hardened Runtime, Entitlements, Notarisierung, Stapling und Helper-Signaturen prüfen.
8. in `Über AhoiBrowser` dieselben Revisionen bestätigen.
9. macOS-Version, Gerät und Profiltyp dokumentieren.
10. einen Lauf mit frischem Profil und bei Migrationen einen Lauf mit vorhandenem Profil durchführen.

Release-E2E darf nicht mit folgenden Abkürzungen laufen:

- `--no-sandbox`;
- `--ignore-certificate-errors`;
- deaktivierter Site Isolation;
- entpacktem App-Bundle aus dem Buildverzeichnis;
- Debug-only Feature Flags, die im Release nicht existieren.

### Lokale HTTPS-E2E-Fixtures

Baue einen reproduzierbaren lokalen HTTPS-Testserver mit sauber vertrauenswürdiger Testzertifikatslösung. Er liefert:

- Downloads mit Range Requests, Pause/Resume und reproduzierbaren Hashes;
- Upload und Drag-and-drop;
- Split-DnD-Ziele, sichere URL-Drops und Drei-Pane-Seiten mit getrennten Audio-/Video-, Dialog-, Download- und Permission-Zuständen;
- Redirects und Popups;
- OAuth-Testfluss;
- Passkey/WebAuthn-Testfluss;
- H.264/AAC, MSE und PiP;
- WebRTC;
- Kamera und Mikrofon;
- Screen Capture;
- Location, Notifications und Clipboard;
- First- und Third-Party-Cookies;
- CHIPS;
- GPC, Referrer und Trackingparameter;
- Local Storage und Session Storage;
- IndexedDB;
- Cache Storage;
- Service Worker;
- versionierte Cache-Assets und Zugriffszähler;
- Request-/Response-Header-Echo;
- CSP- und CORS-Testfälle;
- CSS-/LESS-/SASS-/JavaScript-Injection;
- synthetische Loginformulare mit ausschließlich künstlichen Zugangsdaten;
- sichere Testfälle für gefährliche Downloadwarnungen ohne echte Malware.

HTTP-Auth-Fixtures enthalten zusätzlich:

- mindestens zwei Basic-Auth-Realms auf demselben Host;
- mehrere Benutzer für dasselbe Realm;
- verschiedene Pfade innerhalb eines Realms;
- gleichnamige Realms auf unterschiedlichen Ports;
- gleichnamige Realms auf HTTP und HTTPS;
- Digest Auth;
- Proxy Auth, sofern lokal reproduzierbar;
- falsche Zugangsdaten;
- Passwortwechsel;
- wiederholte `401`-Challenges;
- Same-Origin-Redirect im Schutzbereich;
- Redirect auf eine fremde Origin;
- Subresource-Auth-Challenge;
- Server-Receipts, die niemals das Passwort persistieren.

Fixtures ersetzen keine Live-Abnahme von YouTube, Meet beziehungsweise einem vergleichbaren WebRTC-Dienst, Chrome Web Store, 1Password, Bitwarden, CloudKit, dem echten Updater, Netflix und dem zweiten Widevine-Dienst.

## Vollständige Abnahmematrix

Führe jeden Test als eigenen dokumentierten Fall. Ergänze weitere Tests, wenn die Implementierung zusätzliche Risiken erzeugt.

### Packaging, Installation und erster Start

- `PKG-01`: DMG öffnen, App nach `/Applications` ziehen, DMG auswerfen und App ohne Gatekeeper-Fehler starten.
- `PKG-02`: ARM64, vollständige Codesign-Kette, Hardened Runtime, Entitlements, Notarisierung und Stapling prüfen.
- `PKG-03`: alle Renderer-, GPU-, Network-, Utility- und Crash-Helper auf korrekte Signatur prüfen.
- `PKG-04`: Erster Start auf Deutsch durchführen.
- `PKG-05`: Erster Start auf Englisch durchführen.
- `PKG-06`: Sprache, Appearance und Startverhalten wählen, App beenden und Persistenz nach Neustart prüfen.
- `PKG-07`: Clean Install unter einem frischen macOS-Testnutzer.
- `PKG-08`: Upgrade Install mit realistischem vorhandenen Profil.
- `PKG-09`: `Über AhoiBrowser` zeigt korrekte Produkt-, Build- und Chromium-Version.

### UI, Theme, Glass und Accessibility

- `UI-01`: mehrere reale Websites öffnen, Sidebar ein-/ausblenden und Fenster stufenlos skalieren.
- `UI-02`: Vollbild und macOS Split View verwenden; keine abgeschnittenen oder überlagerten Elemente.
- `UI-03`: System, Hell und Dunkel durchschalten und nach Neustart prüfen.
- `UI-04`: globale Hauptfarbe ändern.
- `UI-05`: pro Workspace unterschiedlichen Akzent setzen und Persistenz prüfen.
- `UI-06`: Glass aktivieren und deaktivieren.
- `UI-07`: macOS `Transparenz reduzieren` aktivieren; opaker, lesbarer Fallback.
- `UI-08`: `Bewegung reduzieren` und erhöhten Kontrast prüfen.
- `UI-09`: vollständige Kernreise auf Deutsch; keine Rohschlüssel, Überläufe oder falsche Pluralformen.
- `UI-10`: dieselbe Kernreise auf Englisch.
- `UI-11`: schwebende Navigationszeile einblenden, bedienen und automatisch ausblenden; Website misst vor, während und nach dem Vorgang identische Viewporthöhe und erhält keinen Layout-Shift.
- `UI-12`: Reveal-Notch per Hover, Klick, `⌘L` und Tastaturfokus bedienen; offene Eingabe, Extension-Popup, Auth-/Permission-Dialog und Downloadstatus werden nicht vorzeitig ausgeblendet.
- `UI-13`: WebContents-Container mit Radius, Clipping und Schatten auf hellen/dunklen realen Seiten, bei Resize, Browser-Vollbild, Video-Vollbild, Screen Capture und macOS Split View prüfen.
- `UI-14`: Sidebar zwischen angedockt, schwebend, eingeklappt und wieder sichtbar schalten; angedockt ändert den echten Viewport korrekt, schwebend überlagert ohne unerwarteten Reflow.
- `UI-15`: Glass für Sidebar, Navigationszeile, Notch, Popup und MiniPlayer aktivieren; bei `Transparenz reduzieren`, hohem Kontrast, Battery-/Performance-Druck und deaktiviertem Glass entsteht ein konsistenter opaker Fallback.
- `UI-16`: Sidebar vollständig verbergen, Pointer an den linken Fensterrand bewegen und dieselbe Sidebar als Overlay ein-/ausblenden; Website-Viewport und Renderergröße bleiben identisch, Drag/Fokus/Menu halten offen, Escape/Pin/Dock schließen beziehungsweise fixieren deterministisch.
- `UI-17`: normalen letzten Tab schließen; Fenster bleibt mit nativer themefähiger Leerdarstellung offen, besitzt null Tabs/null `WebContents`, erzeugt keinen Netzwerkzugriff und kann über Cmd+T, Command Bar sowie Sidebar-Aktionen wieder einen Tab öffnen.
- `A11Y-01`: Kernreise ausschließlich per Tastatur; keine Fokusfalle.
- `A11Y-02`: VoiceOver-Rollen, Labels, Reihenfolge und Zustandsansagen.
- `A11Y-03`: 200-Prozent-Zoom beziehungsweise große Systemschrift, soweit für Browser-Chrome relevant.

### Tree, Tabs und Workspaces

- `TREE-01`: fünf Workspaces und mindestens zehn verschachtelte Ordnerebenen anlegen.
- `TREE-02`: gespeicherte Seiten erstellen, umbenennen, sortieren und zwischen Ordnern verschieben.
- `TREE-03`: Seite zwischen Workspaces verschieben und Undo prüfen.
- `TREE-04`: temporären Tab öffnen, per `⌘D` speichern, schließen und erneut über den Knoten öffnen.
- `TREE-05`: temporären Tab schließen und über Tab Restore zurückholen.
- `TREE-06`: Browser mit temporären Tabs beenden; Standarddialog `Fortsetzen/Leer starten` prüfen.
- `TREE-07`: feste Startpräferenzen `fragen`, `fortsetzen` und `leer` jeweils nach Neustart prüfen.
- `TREE-08`: gespeicherten Unterbaum löschen, Undo verwenden, erneut löschen und neu starten.
- `TREE-09`: Papierkorb und Tombstones führen nicht zu Zyklen oder Duplikaten.
- `TREE-10`: 10.000-Knoten-Fixture laden, suchen, scrollen, öffnen und verschieben.
- `TREE-11`: kontrollierter Crash während Baumänderung; atomare, reparierbare Daten nach Neustart.
- `TREE-12`: Gruppenname, Icon und Farbe ändern; in Hell/Dunkel, nach Neustart und nach Workspace-Wechsel korrekt und kontrastreich wiederherstellen.
- `TREE-13`: direkte Kinder in einer durchgehenden Gruppen-Bubble sowie mindestens drei verschachtelte Untergruppen prüfen; Collapse, Drag-and-drop, Split und Virtualisierung dürfen Zugehörigkeit und Hierarchie nicht optisch zerreißen.
- `TREE-14`: leeren Workspace aktivieren, ohne automatisch einen Tab zu erzeugen; zwischen leerem und gefülltem Workspace wechseln und korrekten aktiven Nullzustand sowie Session Restore prüfen.
- `TREE-15`: `Alle temporären Tabs leeren` einschließlich des aktiven letzten Tabs ausführen; kein Ersatz-Tab, kein fremder Workspace-Tab und kein Fenster-Close.
- `WS-01`: Workspace-Wechsel per Sidebar.
- `WS-02`: Workspace-Wechsel per Tastatur.
- `WS-03`: horizontale Geste mit echter Magic Mouse, einschließlich langsamer, schneller und abgebrochener Geste.
- `WS-04`: Gestenrichtung, Empfindlichkeit und Deaktivierung prüfen.
- `WS-05`: zwei Fenster; Baumänderung in Fenster A erscheint in B.
- `WS-06`: temporäre Tabs und aktive Auswahl wandern nicht unerwartet zwischen Fenstern.
- `WS-07`: Cookies und Logins bleiben beim Workspace-Wechsel erhalten.
- `WS-08`: aktiven Workspace mit Icon/Name und alle inaktiven Workspaces als stabile Dots darstellen; Hover und Tastaturfokus zeigen jeweils korrekte Vorschau ohne Wechsel.
- `WS-09`: Workspace per Dot, Tastatur und echter Wischgeste wechseln; Animation abbrechen und sicherstellen, dass aktiver Dot, Baum, Tab und WebContents niemals auseinanderlaufen.

### Sidebar Drag-and-drop und Split View

- `SPLIT-01`: Integrationstest erzwingt exakt zwei, drei oder vier eindeutige Tabs pro Split-Collection, weist Mischprofile und ein fünftes Pane atomar ab und erhält genau ein aktives Pane.
- `SPLIT-02`: alle kanonischen Zwei-/Drei-/Vier-Pane-Layoutbäume einschließlich 2×2, Ratios, Snap Points, Mindestgrößen und Layoutwechsel deterministisch serialisieren, validieren und ohne `WebContents`-Neuerzeugung anwenden.
- `SPLIT-03`: Session Service und Tab Restore roundtrippen Zwei-/Drei-/Vier-Pane-Mitgliedschaft, Layout, Ratios und Fokus; alte Zweier-/Dreierdaten migrieren und beschädigte beziehungsweise fehlende Leaves sicher degradieren.
- `SPLIT-04`: Extension-`splitId` und Tab-Operationen für zwei, drei und vier Mitglieder prüfen; Move, Close und ungültige Teiloperationen erhalten einen gültigen Zustand oder lösen den Split kontrolliert auf.
- `SPLIT-05`: Seitenzeile in der linken Sidebar auf obere/untere Drop-Zonen ziehen; sichtbares Preview, Reihenfolge in tiefen Ordnern und Persistenz nach Neustart prüfen.
- `SPLIT-06`: Seitenzeile über die mittlere Ordnerzone innerhalb und zwischen Workspaces verschieben; Auto-Expand, exaktes Ziel und Undo prüfen.
- `SPLIT-06A`: temporären und gespeicherten Tab mehrfach ziehen; `Neue Gruppe`, Saved-/Temporary- und Split-Ziele erscheinen in jeder Lifecycle-Reihenfolge zuverlässig, verschwinden nach Drop/Abbruch vollständig und die Vorschau liegt rechts vom Cursor ohne Drop-Zonen zu verdecken.
- `SPLIT-07`: einen Tab mittig auf einen normalen Tab ziehen; echte Zwei-Spalten-Ansicht, zwei simultan interaktive Seiten und unveränderte Tree-Persistenz prüfen.
- `SPLIT-08`: Zwei-Zeilen-Layout im Drag-Preview beziehungsweise Layoutmenü wählen und ohne Reload zwischen Zeilen und Spalten wechseln.
- `SPLIT-09`: dritten Tab auf jedes Pane einer Zweiergruppe ziehen; Einfügeposition vor Drop eindeutig anzeigen und drei simultan laufende Seiten erhalten.
- `SPLIT-10`: drei Spalten, drei Zeilen, `main-left`, `main-right`, `main-top` und `main-bottom` sichtbar durchschalten.
- `SPLIT-11`: Panes per Drag-and-drop und Tastatur neu ordnen sowie Layout wechseln; URLs, Formzustand, Scrollposition und Navigation History bleiben erhalten.
- `SPLIT-12`: alle relevanten Divider per Maus/Trackpad ziehen; horizontales und vertikales Verhältnis, Snap Points, bevorzugte und eingeschränkte Mindestgrößen sowie Persistenz prüfen.
- `SPLIT-13`: komplette Split-Reise nur per Tastatur – erstellen, Pane fokussieren, neu ordnen, Layout wählen, Divider ändern und Split verlassen.
- `SPLIT-14`: Pane aus Split herausziehen, einzeln schließen und gesamten Split schließen; korrekte Übergänge `4 -> 3 -> 2 -> 1`, Before-Unload und Tab Restore prüfen.
- `SPLIT-15`: fünften externen Tab auf volle Vierergruppe ziehen; verständliche Ablehnung ohne Reload, Ersatz, Verstecken, Schließen oder Tree-Mutation.
- `SPLIT-16`: Drag mit `Escape`, Pointer-Abbruch, Mehrfachauswahl, Ordner, Datei, blockierter URL und fehlgeschlagenem Detach abbrechen; Baum und Split bleiben atomar unverändert.
- `SPLIT-17`: echten Tab zwischen zwei normalen Browserfenstern in einen Split ziehen; `WebContents`, Formzustand, Scrollposition und Navigation History bleiben erhalten.
- `SPLIT-18`: Pane anklicken und fokussieren; Adressleiste, Zurück/Vor, Reload, Page Info, Extension-Actions und Developer Toolkit wirken ausschließlich auf dieses aktive Pane.
- `SPLIT-19`: Origin-/Security-/Media-Indikatoren aller Panes und den nicht nur farblichen aktiven Fokusrahmen bei Omnibox, Page Info und Device Chooser prüfen.
- `SPLIT-20`: Permission Prompt, System-Dateipicker und tabmodalen Dialog aus aktivem und inaktivem Pane auslösen; Unterdrückung, Fokusübergabe, Scrim und Origin-Zuordnung prüfen.
- `SPLIT-21`: normale Zwei-, Drei- und Vier-Pane-Sitzung vollständig beenden und neu starten; Workspace/Fenster, Mitgliedschaft, Layout, beide 2×2-Ratios und Fokus exakt wiederherstellen.
- `SPLIT-22`: Split-Zustand bleibt fenster- und Workspace-sessionbezogen; Wechsel und zweites Fenster beschädigen ihn nicht, CloudKit und iOS enthalten keine Split-Topologie.
- `SPLIT-23`: Zwei-/Drei-/Vier-Pane-Split innerhalb Inkognito funktioniert; Normal-/Inkognito-Mischung wird abgewiesen und nach Schließen oder Crash wird nichts wiederhergestellt, synchronisiert oder historisiert.
- `SPLIT-24`: Audio und Video in allen Panes simultan abspielen; Fokuswechsel pausiert nichts, Mute und Media-/Capture-Indikatoren bleiben pro Pane korrekt.
- `SPLIT-25`: Picture in Picture aus jeder Pane-Position starten und über Fokus-, Layout-, Divider-, Workspace-, Minimize- und Sidebar-Wechsel prüfen.
- `SPLIT-26`: Kamera, Mikrofon, WebRTC sowie Tab-/Fenster-/Screen-Sharing in verschiedenen Panes verwenden; laufende Streams, neue Prompts und alle Indikatoren bleiben eindeutig zugeordnet.
- `SPLIT-27`: Download, Upload, HTTP Auth und sichere Dateiauswahl aus verschiedenen Panes starten; Origin, Dialog, Fortschritt und Ergebnis gehören jeweils zum auslösenden Pane.
- `SPLIT-28`: DevTools pro Pane öffnen, docken, abdocken und zwischen Panes fokussieren; jedes DevTools bleibt ohne stilles Retargeting am ursprünglichen `WebContents`.
- `SPLIT-29`: Browser-Vollbild mit allen Panes und Tab-/Content-Vollbild pro Pane prüfen; nach Exit werden Gruppe, Layout, Ratios, Fokus und PiP exakt wiederhergestellt.
- `SPLIT-30`: Renderer eines Panes und anschließend Browserprozess kontrolliert beenden; andere Panes bleiben beim Renderercrash bedienbar und Crash Recovery degradiert niemals zu Phantom-Tabs.
- `SPLIT-31`: VoiceOver-Rollen, Pane- und Dividerwerte, Fokusreihenfolge, Drag-Ansagen, Ablehnungen, RTL sowie vollständige deutsche und englische Split-Reise in allen Appearance-/Accessibility-Modi prüfen.
- `SPLIT-32`: vier reale komplexe Seiten mit Video, DevTools und Downloads neben 10.000 Sidebar-Knoten betreiben; Drag, Fokus, Resize und Layout bleiben flüssig und erzeugen keine eigene Idle-CPU-Regression.
- `SPLIT-33`: vierten Tab auf jedes Pane einer Dreiergruppe ziehen; 2×2-Vorschau, Einfügeposition und vier simultan laufende Seiten prüfen.
- `SPLIT-34`: Vierergruppe zwischen unterstützten Anordnungen einschließlich 2×2 wechseln; Sidebar-Segmentraster spiegelt Position und Reihenfolge korrekt und lesbar wider.
- `SPLIT-35`: den nativen Griff in der Mini-Toolbar eines sichtbaren Panes per echter Maus auf obere, rechte, untere und linke Zone eines anderen Panes ziehen; Overlay, endgültige Reihenfolge, Zeilen-/Spaltenlayout, aktives Pane und unveränderte `WebContents` prüfen.
- `SPLIT-36`: denselben Pane-Drag nach einer bereits erfolgreichen Teilreihenfolge künstlich fehlschlagen lassen; Tab-Reihenfolge, Split-IDs, Mitgliedschaft, Layout, Arrangement, Ratios und aktives Pane müssen byte- beziehungsweise modellgleich zurückgerollt werden.
- `SPLIT-37`: in jedem Pane eine unterschiedliche URL laden, anhand URL plus Aktivindikator unten rechts das aktive Pane auswählen und anschließend über die obere Adressleiste navigieren; nur das markierte Pane ändert seine URL, auch nach mehrfachen Fokus-, Layout- und Workspace-Wechseln.
- `SPLIT-38`: Split-Drop-Vorschautext mit langem Titel prüfen; Titel, Status- und Aktionsicons bleiben innerhalb des Zielsegments und ragen weder über die Segment- noch über die Gruppen-Trennlinie hinaus.
- `SPLIT-39`: Sidebar-Split-Trennlinien für zwei, drei und vier Panes in LTR/RTL, verschiedenen Skalen und Hover-/Drag-Zuständen prüfen; kein antialiasierter Endpunkt ragt oben, unten oder seitlich aus der Gruppen-Bubble heraus.

### Command Bar, Quick Window und Inkognito

- `CMD-01`: `⌘L` und Navigation zu einer URL.
- `CMD-02`: `⌘T` erzeugt temporären Tab und fokussiert die Eingabe.
- `CMD-03`: `g Suchbegriff` führt direkte Google-Suche aus.
- `CMD-04`: offene Tabs, Baum, Ordner, Workspaces, Verlauf und Befehle finden.
- `CMD-05`: gesamte Command-Bar-Reise nur per Tastatur.
- `CMD-06`: Ranking und Command-Bar-Latenz messen.
- `QUICK-01`: globalen Shortcut bei inaktiver App verwenden.
- `QUICK-02`: eingeloggte Website öffnen; Quick Window nutzt dasselbe normale Profil.
- `QUICK-03`: Seite in normalen Tab beziehungsweise Baum übernehmen.
- `QUICK-04`: Quick Window schließen, ohne normale Sitzung zu beschädigen.
- `POPUP-01`: Fixture und reale Website öffnen ein geeignetes Popup; es erscheint als fokussiertes Overlay über dem auslösenden Pane, mit korrekter Origin und ohne dessen Viewport dauerhaft umzubauen.
- `POPUP-02`: externe vertikale Aktionen ausschließlich per Maus und anschließend ausschließlich per Tastatur bedienen; Close, Labels, Fokusreihenfolge, Hover und VoiceOver sind eindeutig.
- `POPUP-03`: Popup `Als eigenen Tab öffnen`; dasselbe `WebContents`, Formzustand, Login, Scrollposition und Navigation History bleiben ohne Reload erhalten.
- `POPUP-04`: Popup `Mit Ausgangstab splitten`; Zwei- bis Vier-Pane-Ziel, Opener-Zuordnung, Tree-Zustand und Fokus bleiben korrekt, fünftes Pane wird atomar abgewiesen.
- `POPUP-05`: Download, Upload, HTTP Auth, Permission Prompt, OAuth, Passkey, Audio/Video, DevTools, Vollbild und Before-Unload aus Overlay prüfen.
- `POPUP-06`: nicht sicher overlayfähigen Flow kontrolliert als separates Fenster öffnen; keine Origin-Verwechslung, kein abgeschnittener Inhalt und verständliche Transition.
- `POPUP-07`: Opener, Popup, Split-Promotion und Browser jeweils kontrolliert crashen beziehungsweise schließen; keine Phantom-Tabs, Leaks oder verlorenen normalen Sessions.
- `INC-01`: `⌘⇧N` öffnet echtes Inkognito-Fenster.
- `INC-02`: Testcookie und Login in Inkognito setzen, Fenster schließen und normale Sitzung prüfen.
- `INC-03`: Inkognito erscheint nicht in Verlauf, Baum, Sync, iOS oder Session Restore.
- `INC-04`: Extension ist nur nach expliziter Inkognito-Freigabe aktiv.
- `INC-05`: Browsercrash mit normalem und Inkognito-Fenster; nur normale Sitzung wird angeboten.

### Navigation und Systemintegration

- `NAV-01`: Zurück, Vor, Reload, Hard Reload, Same-Origin- und Cross-Origin-Redirect.
- `NAV-02`: Popups zulassen, blockieren und pro Site konfigurieren.
- `NAV-03`: Formulare, Datei- und Ordnerupload.
- `NAV-04`: Drag-and-drop einer Datei in Website und Browser-Chrome.
- `NAV-05`: Drucken und PDF-Vorschau.
- `NAV-06`: OAuth-Testlogin.
- `NAV-07`: Passkey/WebAuthn-Test.
- `NAV-08`: sicherer Custom-Protocol-Prompt.
- `NAV-09`: Chromiums unveränderte Trackpad-/Magic-Mouse-Wischgeste über einer Seite für Zurück und Vor als Regression prüfen; langsame, schnelle und abgebrochene Bewegung mit sichtbarem Fortschritt, aber keine Ahoi-Parallelimplementierung.
- `NAV-10`: Seiten-, Workspace- und horizontale Website-Scrollgeste gegeneinander testen; genau eine erkannte Aktion, kein Doppelwechsel und konfigurierbare Deaktivierung.
- `NAV-11`: `⌘` plus Scroll wechselt mit Vorschau, Schwelle und Rate-Limit zwischen laufenden Tabs; Web-Zoom, normales Scrollen, Modalzustände und Split-Fokus bleiben korrekt.
- `NAV-12`: Mittelklick-Auto-Scrolling auf Hauptseite und verschachteltem Scroller in alle Richtungen und Geschwindigkeiten; Mittelklick, Klick, Escape, Tab-/Workspace-Wechsel und Fokusverlust beenden sofort.
- `NAV-13`: Auto-Scrolling bei nicht scrollbarer Seite, Pointer Lock, reduziertem Motion, Zoom und Browser-Chrome-Drag bleibt sicher und erzeugt keine hängende Eingabe.
- `DEFAULT-01`: AhoiBrowser als Standardbrowser setzen.
- `DEFAULT-02`: Links aus Mail, Finder und Terminal öffnen; korrekter Fensterfokus und URL.

### Downloads

- `DL-01`: PDF und große ZIP-Datei laden.
- `DL-02`: großen Download pausieren und fortsetzen.
- `DL-03`: Download abbrechen und sauber entfernen.
- `DL-04`: fertige Datei im Finder anzeigen und öffnen.
- `DL-05`: Dateihash mit Fixture-Server vergleichen.
- `DL-06`: Downloadhistorie nach Neustart.
- `DL-07`: harmloser Testfall für gefährlichen Download zeigt Warnung; Abbrechen und explizites Behalten prüfen.
- `DL-08`: Netzverlust und Wiederaufnahme.

### HTTP Basic Auth, Digest und `.htaccess`

- `AUTH-01`: neue Basic-Auth-Daten eingeben, erfolgreich anmelden und erst danach speichern.
- `AUTH-02`: Browser vollständig beenden; nach Neustart wird das gespeicherte Konto angeboten.
- `AUTH-03`: zwei Konten für dasselbe Realm speichern und sichtbar auswählen.
- `AUTH-04`: Benutzername über Autocomplete suchen und per Tastatur auswählen.
- `AUTH-05`: bevorzugtes beziehungsweise zuletzt erfolgreiches Konto wird korrekt vorausgewählt.
- `AUTH-06`: falsches Passwort verwenden, verständlichen Fehler sehen und auf anderes Konto wechseln.
- `AUTH-07`: einzelner Fehler löscht gespeicherte Daten nicht.
- `AUTH-08`: geändertes Passwort nach erfolgreichem Login aktualisieren.
- `AUTH-09`: zwei Realms desselben Hosts bleiben strikt getrennt.
- `AUTH-10`: gleichnamige Realms auf unterschiedlichen Ports bleiben getrennt.
- `AUTH-11`: Server- und Proxy-Credentials bleiben getrennt.
- `AUTH-12`: HTTPS-Zugang wird nie auf HTTP übertragen.
- `AUTH-13`: Cross-Origin-Redirect erhält keinen Authorization-Header.
- `AUTH-14`: Pfad- und Protection-Space-Regeln verhindern zu breite Wiederverwendung.
- `AUTH-15`: `HTTP-Anmeldung wechseln` leert den richtigen Auth-Cache, schließt betroffene Verbindungen, lädt neu und zeigt die Kontenauswahl.
- `AUTH-16`: Sitzungsabmeldung löscht das gespeicherte Konto nicht.
- `AUTH-17`: Löschen eines gespeicherten Kontos führt beim nächsten Besuch zu leerem Dialog.
- `AUTH-18`: `Für dieses Realm nie speichern` unterdrückt Angebote und ist in Settings rücksetzbar.
- `AUTH-19`: HTTP-Verbindung zeigt deutliche Warnung und bietet kein Auto-Login an.
- `AUTH-20`: Digest Auth funktioniert ohne Vermischung mit Basic-Auth-Einträgen.
- `AUTH-21`: Inkognito kann vorhandenes Konto nur explizit auswählen und speichert keine Änderungen.
- `AUTH-22`: Schließen des letzten Inkognito-Fensters verwirft dessen Auth-Cache.
- `AUTH-23`: HTTP-Auth-Credentials erscheinen weder auf Mac B noch in CloudKit oder iOS.
- `AUTH-24`: Passwortanzeige in der HTTP-Zugangsverwaltung erfordert Touch ID/Systemauthentifizierung.
- `AUTH-25`: Logs, NetLog, Crash Reports und Evidenz enthalten weder Passwort noch vollständigen Authorization-Header.
- `AUTH-26`: Subresource-Auth-Challenge kann keine unklare oder irreführende Credential-Abfrage erzeugen.
- `AUTH-27`: vollständige sichtbare Reise – Speichern, Neustart, Autocomplete, Kontowahl, Fehlerkorrektur, Wechsel und Abmeldung – via Computer Use im installierten Build.

### Medien und Berechtigungen

- `MEDIA-01`: lokale H.264/AAC-Fixture abspielen.
- `MEDIA-02`: YouTube abspielen, seeken und Auflösung wechseln.
- `MEDIA-03`: Vollbild und Picture-in-Picture.
- `MEDIA-04`: PiP bleibt bei Tab-, Workspace- und Fensterwechsel sowie minimierter App aktiv.
- `MEDIA-05`: Hardwaredecode über Media Internals und GPU-Status nachweisen.
- `MEDIA-06`: Audiofokus und Media Session.
- `MEDIA-07`: echte macOS-Medientasten.
- `MEDIA-08`: abspielender Tab zeigt Lautsprecher, gemuteter beziehungsweise stummgeschalteter Medientab zeigt stummes Lautsprecher-Icon; Hover-, Close- und Power-Aktionen verdecken den Zustand nicht dauerhaft.
- `MEDIA-09`: MiniPlayer erscheint am unteren Sidebar-Ende, steuert die richtige Media Session und lässt bei darüberliegendem Overlay jeden dahinter scrollenden Tab durch korrektes Bottom-Inset erreichen.
- `MEDIA-10`: MiniPlayer bei angedockter, schwebender, eingeklappter und wieder eingeblendeter Sidebar sowie nach Resize, Workspace-, Fenster- und App-Neustart prüfen.
- `MEDIA-11`: mit mehreren gleichzeitigen Media Sessions die Quelle explizit wechseln; Titel, Origin, Play/Pause, Seek, Mute und macOS-Medientasten bleiben dem gewählten Tab zugeordnet.
- `MEDIA-12`: wiederholt ohne Playback-Verlust oder falschen Fokus zwischen Sidebar-MiniPlayer und Chromium-PiP wechseln; aus jedem Split-Pane und nach Layoutwechsel prüfen.
- `DRM-01`: Netflix mit legalem Widevine-CDM und Testaccount.
- `DRM-02`: mindestens ein zweiter Widevine-Dienst.
- `PERM-01`: Kamera/Mikrofon zunächst ablehnen, erneut anfordern, erlauben und zurücksetzen.
- `PERM-02`: realer WebRTC-/Meet-Test.
- `PERM-03`: gesamten Bildschirm teilen.
- `PERM-04`: einzelnes Fenster teilen.
- `PERM-05`: einzelnen Tab teilen.
- `PERM-06`: Auswahl abbrechen und OS-Berechtigung entziehen.
- `PERM-07`: Standort erlauben, ablehnen und zurücksetzen.
- `PERM-08`: Notifications erlauben, ablehnen und zurücksetzen.
- `PERM-09`: Clipboard-Berechtigung und macOS-Verhalten prüfen.

### Extensions und uBlock Origin

- `EXT-01`: reale MV3-Erweiterung aus Chrome Web Store installieren.
- `EXT-02`: Popup, Optionsseite, Service Worker und Content Script bedienen.
- `EXT-03`: Browser neu starten, Extensionzustand prüfen, Update durchführen und Extension entfernen.
- `EXT-04`: Extension-Action pinnen, Overflow öffnen und Shortcut ändern.
- `EXT-05`: entpackte Entwicklererweiterung laden und nach Codeänderung neu laden.
- `EXT-06`: 1Password installieren und AhoiBrowser als vertrauenswürdigen Browser freigeben.
- `EXT-07`: 1Password per Touch ID entsperren und künstlichen Testlogin ausfüllen/speichern.
- `EXT-08`: Bitwarden mit Test-Vault entsperren, Login ausfüllen und speichern.
- `EXT-09`: React DevTools oder vergleichbare Entwicklererweiterung verwenden.
- `EXT-10`: IBM Equal Access Accessibility Checker installieren und auf Testseite ausführen.
- `EXT-11`: AnyChat mit exakt der erwarteten Store-ID im AhoiBrowser-Profil installieren, sichtbare Berechtigungen prüfen, Action beziehungsweise Side Panel öffnen und nach Browserneustart erneut bedienen.
- `EXT-12`: nachweisen, dass AnyChat, 1Password und uBlock ausschließlich im getesteten AhoiBrowser-Profil installiert beziehungsweise konfiguriert wurden; Google-Chrome- und Arc-Profile bleiben byte- beziehungsweise zustandsseitig unverändert.
- `EXT-13`: 1Password-Native-Messaging wird über den offiziellen Additional-Browsers-Prozess für die signierte Ahoi-App provisioniert; ein aus Arc oder Chrome kopiertes Manifest wird abgewiesen und zählt nicht als Pass.
- `UBO-01`: uBlock Origin Classic über AhoiBrowser-Ein-Klick-Flow installieren.
- `UBO-02`: Version, ID, Quelle und Hash gegen Katalog prüfen.
- `UBO-03`: Netzwerkrequest auf kontrollierter Testseite blockieren.
- `UBO-04`: kosmetischen Filter prüfen.
- `UBO-05`: eigene Filterregel anlegen.
- `UBO-06`: Dashboard und Einstellungen verwenden.
- `UBO-07`: Browserneustart und Persistenz.
- `UBO-08`: Filterlistenupdate.
- `UBO-09`: Extension-Update über signierten Katalog.
- `UBO-10`: Deinstallation.
- `UBO-11`: fremdes, nicht allowlistetes MV2-Paket wird abgewiesen.
- `UBO-12`: gewöhnliche MV3-Erweiterungen funktionieren parallel unverändert.

### Arc-Import

- `IMPORT-ARC-01`: Arc-App und mehrere Profile erkennen; verwaiste `Singleton*`-Symlinks nicht als laufende Instanz fehlinterpretieren.
- `IMPORT-ARC-02`: bei laufendem Arc verständlich stoppen, ohne Quellprofil oder Ahoi-Zielprofil zu verändern.
- `IMPORT-ARC-03`: Snapshot von `StorableSidebar.json` und ausgewählten Chromium-Datenbanken einschließlich konsistenter WAL-/SHM-Dateien erstellen; Manifest und Hashes verifizieren.
- `IMPORT-ARC-04`: Symlink-, Traversal-, Größenlimit-, unbekannte-Schema-, Malformed-JSON-/SQLite- und Lock-Fixtures fail-closed ablehnen.
- `IMPORT-ARC-05`: Schema-1-Fixture mit Spaces, verschachtelten Lists, Tabs, Containern und Split Views vollständig in einen deterministischen `ImportPlan` überführen.
- `IMPORT-ARC-06`: Vorschau und Kategorien einzeln an-/abwählen; Konflikte, Deduplizierungen, Degradierungen und ausgeschlossene Geheimdaten korrekt anzeigen.
- `IMPORT-ARC-07`: Spaces, Listen und Tabs atomar als Workspaces, Ordner und gespeicherte Seiten importieren; Reihenfolge, Hierarchie und sichere URLs prüfen.
- `IMPORT-ARC-08`: valide Zwei-Pane-Splits über echte Chromium-Tabs und `SplitViewService` mit Orientierung, Fokus und Ratio rekonstruieren; ungültige Splits verlustarm als Ordner degradieren.
- `IMPORT-ARC-09`: Crash beziehungsweise Fehler vor, während und nach der Commit-Grenze injizieren; vollständigen Rollback oder deterministische Recovery ohne halben Baum beweisen.
- `IMPORT-ARC-10`: denselben Snapshot zweimal importieren; der zweite Lauf ist ohne Duplikate ein nachvollziehbarer No-op.
- `IMPORT-ARC-11`: Cookies, Login Data, Passwörter, Tokens, Sessions, `Secure Preferences`, Extension Storage, Native-Messaging-Manifeste, Keychain- und Inkognito-Daten bleiben ausgeschlossen; Logs und Evidenz sind redigiert.
- `IMPORT-ARC-12`: vorhandenen realen lokalen Arc-Datenstand nach immutable Backup und Dry Run in das installierte AhoiBrowser-Profil importieren; Workspaces, Ordner, gespeicherte Seiten und rekonstruierbare Splits sichtbar prüfen und den No-op-Wiederholungslauf belegen.

### Lokaler Passwortmanager und Autofill

- `PASS-01`: neuen Webformular-Login speichern und nach komplettem Browserneustart korrekt anbieten und ausfüllen.
- `PASS-02`: mehrere Konten derselben Origin suchen, ausschließlich per Tastatur auswählen, aktualisieren und einzeln löschen.
- `PASS-03`: gespeichertes Passwort erst nach erfolgreicher Touch-ID-/Systemauthentifizierung anzeigen beziehungsweise kopieren; Abbruch und fehlgeschlagene Authentifizierung geben keinen Klartext preis.
- `PASS-04`: Passwortverwaltung über Einstellungen und kompakte Browseraktion öffnen; Suche, Bearbeiten, Löschen, Import/Export und upstream verfügbare Passwortprüfung sind zugänglich und lokalisiert.
- `PASS-05`: Autofill, Passkeys, Webformular-Passwörter, HTTP-Auth-Zugänge, 1Password und Bitwarden bleiben getrennt und überschreiben oder doppeln einander nicht unerwartet.
- `PASS-06`: Inkognito speichert keine neuen lokalen Passwörter; bestehende Nutzung folgt Chromiums sicherer OTR-Policy und hinterlässt keine Sessiondaten.
- `PASS-07`: Webformular- und HTTP-Auth-Passwörter, Autofill und Passkeys erscheinen weder in CloudKit, Geräte-Tabs, iOS noch in E2E-Evidenz oder Logs.

### Developer Toolkit

- `DEV-01`: CSS auf Fixture anwenden, Syntaxfehler korrigieren, Reload und Neustart.
- `DEV-02`: LESS kompilieren und sichtbar anwenden.
- `DEV-03`: SASS kompilieren und sichtbar anwenden.
- `DEV-04`: nachweisen, dass Compiler erst beim Öffnen geladen werden.
- `DEV-05`: JavaScript in Isolated World ausführen.
- `DEV-06`: Main World auswählen, Warnung bestätigen und Ausführung prüfen.
- `DEV-07`: Tab-, Origin-, Domain- und Pfad-Scope prüfen.
- `DEV-08`: versioniertes Asset laden und Hard Reload prüfen.
- `DEV-09`: Cache Off aktivieren und Serverzugriffszähler prüfen.
- `DEV-10`: Site Cache und globalen Cache getrennt leeren.
- `DEV-11`: Cookies, Local Storage, Session Storage, IndexedDB, Cache Storage und Service Worker einzeln löschen.
- `DEV-12`: Cookie suchen, erstellen, bearbeiten und löschen.
- `DEV-13`: SameSite, Secure, HttpOnly und Partitioned/CHIPS korrekt behandeln.
- `DEV-14`: Request-Header über Echo-Server bestätigen.
- `DEV-15`: Response-Header-Regel bestätigen.
- `DEV-16`: CSP/CORS-Advanced-Modus sichtbar aktivieren und vollständig zurücksetzen.
- `DEV-17`: Keychain-Secret verwenden; kein Klartext in UI-Evidenz, Logs oder Sync.
- `DEV-18`: JavaScript, CSS und Bilder jeweils deaktivieren und wieder aktivieren.
- `DEV-19`: Elemente, Überschriften und ARIA-Landmarks umranden.
- `DEV-20`: Alt-/Title-Texte einblenden.
- `DEV-21`: Meta, Canonical, OpenGraph und strukturierte Daten anzeigen.
- `DEV-22`: Passwortfeld sichtbar machen; Navigation maskiert es wieder.
- `DEV-23`: gespeichertes Passwort nur nach Touch ID/Systemauthentifizierung anzeigen.
- `DEV-24`: Aktivchips sind korrekt und verschwinden nach Reset.
- `DEV-25`: `Alle Seitenmodifikationen zurücksetzen` entfernt sämtliche aktiven Änderungen.
- `DEV-26`: bei deaktiviertem Toolkit kein zusätzlicher Dauerprozess, keine messbare Idle-CPU und kein unnötiger Compiler-Speicher.
- `DEV-27`: Datenlöschdialog auf `alle Websites`, mehrere Typen und einen Zeitraum konfigurieren, bestätigen und währenddessen UI ändern/doppelklicken; exakt ein unveränderlicher Request läuft und der Erfolg nennt globalen Scope, Zeitraum und Typen korrekt.
- `DEV-28`: denselben Ablauf für `aktuelle Website`, Einzeltypen, Teilerfolg und Fehler prüfen; kein Status darf einen anderen Scope behaupten und Buttons bleiben in allen Themes/Größen sichtbar, kompakt und tastaturbedienbar.
- `DEV-29`: Toolkit-Icon in der Adressleiste zweimal klicken; der erste Klick öffnet genau eine Bubble für das aktive Split-Pane, der zweite schließt sie zuverlässig ohne Release-Reopen, Flackern oder Retargeting.

### Appearance

- `APPEARANCE-01`: optionale Sidebar-Seitenfarb-Tönung in den Einstellungen ein- und ausschalten; Default und Neustartpersistenz prüfen.
- `APPEARANCE-02`: zwei Split-Panes mit unterschiedlichen `theme-color`-Werten aktivieren; Sidebar folgt ausschließlich dem aktiven Pane und behält lesbare semantische Oberflächen in Hell, Dunkel und Glass.
- `APPEARANCE-03`: ohne `theme-color` das bereits geladene Favicon als begrenzten lokalen Fallback verwenden; kein zusätzlicher Request und keine Seitenerfassung dürfen entstehen.
- `APPEARANCE-04`: bei hohem Kontrast bleibt die Seitenfarb-Tönung unabhängig vom gespeicherten Schalter vollständig aus; reduzierte Transparenz und fehlende beziehungsweise transparente Farben ergeben sichere Fallbacks.

### Privacy und Security

- `PRIV-01`: First-Party-Login funktioniert im Modus `Mehr Schutz`.
- `PRIV-02`: unpartitionierte Third-Party-Cookies werden blockiert.
- `PRIV-03`: CHIPS und Storage Access funktionieren gemäß Policy.
- `PRIV-04`: GPC wird korrekt gesendet.
- `PRIV-05`: Referrer und Trackingparameter verhalten sich wie dokumentiert.
- `PRIV-06`: Werbe-/Profiling-APIs sind deaktiviert.
- `PRIV-07`: absichtlich inkompatible Fixture auf `Maximale Website-Kompatibilität` umschalten; Wirkung und weiterhin aktive Chromium-Sicherheitsgrenzen sind ohne internes Fachvokabular verständlich.
- `PRIV-08`: Origin-Ausnahme bleibt nach Reload und Neustart sichtbar aktiv.
- `PRIV-09`: Ausnahme entfernen und Verhalten von `Mehr Schutz` wiederherstellen.
- `PRIV-10`: uBlock deaktivieren und Browserschutz isoliert testen.
- `PRIV-11`: frisches Profil fünf Minuten ohne Navigation mitschneiden.
- `PRIV-12`: anschließend normale Navigation durchführen und vollständige Endpoint-Liste erfassen.
- `PRIV-13`: jeder Hintergrundendpoint entspricht der maschinenlesbaren Allowlist.
- `PRIV-14`: Standard Safe Browsing warnt auf kontrolliertem Testfall.
- `PRIV-15`: Enhanced Protection ist nicht still aktiv.
- `PRIV-16`: kontrollierter Crash löst keinen automatischen Produkttelemetrie- oder Crashupload aus.
- `PRIV-17`: frisches Profil startet mit `Maximale Website-Kompatibilität`; `winfuture.de` lädt bei deaktiviertem uBlock inklusive `html-load.com`, ohne dass Ahoi einen Drittanbieterhost als Adblocker sperrt.
- `PRIV-18`: fehlende private Google-API-Schlüssel erzeugen keine Nutzerwarnung und beeinträchtigen normale Navigation nicht; Ahoi verlangt von Endnutzern keinen Google-API-Key und bietet kein vorgetäuschtes Chrome Sync an.
- `SEC-01`: Renderer-Sandbox, Site Isolation und Prozessgrenzen im Release nachweisen.
- `SEC-02`: normale Website kann keine internen AhoiBrowser-Mojo- oder WebUI-Funktionen erreichen.
- `SEC-03`: manipulierte Extension-, uBO- und Updateartefakte werden abgewiesen.
- `SEC-04`: Repository- und Buildartefakte auf Secrets prüfen.

### Sync zwischen zwei Macs

Diese Tests benötigen zwei reale, installierte AhoiBrowser-Builds und echte iCloud-/CloudKit-Umgebung:

- `SYNC-01`: Workspace und Baum von Mac A nach Mac B synchronisieren.
- `SYNC-02`: gespeicherte Seite von Mac B ändern und auf Mac A prüfen.
- `SYNC-03`: beide Macs offline ändern und anschließend deterministisch mergen.
- `SYNC-04`: gleichzeitige Moves desselben Knotens.
- `SYNC-05`: gleichzeitiger Move und Delete.
- `SYNC-06`: Konflikt darf keinen Zyklus erzeugen; Recovery-Ordner prüfen.
- `SYNC-07`: History von A auf B und iOS finden.
- `SYNC-08`: offene Tabs bleiben nach Gerät gruppiert.
- `SYNC-09`: Verlauf löschen und Propagation prüfen.
- `SYNC-10`: Retention 30/90/365/unbegrenzt prüfen; Default 90 Tage.
- `SYNC-11`: Cookies bleiben lokal.
- `SYNC-12`: Webformular- und HTTP-Auth-Passwörter bleiben lokal.
- `SYNC-13`: Site Storage, Permissions, Extension Storage und Keychain-Secrets bleiben lokal.
- `SYNC-14`: Inkognito wird nie serialisiert.
- `SYNC-15`: zwei Developer Assets anlegen; nur explizit freigegebenes Asset synchronisiert.
- `SYNC-16`: Extension-Inventar erzeugt nur Installationsvorschlag, keine stille Installation.
- `SYNC-17`: iCloud abmelden, offline ändern, wieder anmelden und Queue abarbeiten.
- `SYNC-18`: CloudKit-Quota-/temporären Fehler verständlich behandeln.
- `SYNC-19`: Zone-/Key-Reset und bestätigten Recovery-Upload prüfen.
- `SYNC-20`: Accountwechsel ohne stillen Datenverlust.
- `SYNC-21`: Gerät widerrufen und weiteren Zugriff verhindern.
- `SYNC-22`: Sync-Logs und Payload-Evidenz enthalten keine ausgeschlossenen Geheimdaten.
- `SYNC-23`: Tabs von Mac B und iOS erscheinen ohne Geräte-Sonderseite direkt zwischen den normalen Sidebar-Tabs, mit passendem Smartphone-/Tablet-/Desktop-Badge, Favicon, Workspace und letzter Aktivität; lokal filtern und öffnen.
- `SYNC-24`: Gerät offline, Tab geschlossen, Gerät umbenannt und Gerät entzogen; Geräte-Tabs-UI zeigt verständliche Aktualität, räumt Tombstones auf und bietet keine veraltete Remote-Aktion an.
- `SYNC-25`: Inkognito-, Passwort-, Cookie-, Site-Storage-, Permission- und Extension-Storage-Daten tauchen weder in Geräte-Tabs-Suche noch Vorschau oder Remote-Payload auf.
- `SYNC-26`: frisches Profil ohne Google-Anmeldung verwenden; Geräte-Tabs und kompletter Sync funktionieren über CloudKit, während Chrome Sync und Google-Browserkonto deaktiviert bleiben.
- `SYNC-27`: in einem Build ohne konfigurierte CloudKit-Capability den Hauptschalter aktivieren und lokale Sync-Datenbank, Outbox sowie Retention bedienen; Remote Control bleibt ehrlich gesperrt, es wird keine Verschlüsselung ruhender lokaler Daten behauptet und nach signierter CloudKit-Konfiguration wird die ausstehende Outbox kontrolliert transportiert.

### iOS-/iPadOS-Companion und Remote Control

- `IOS-01`: auf echtem iPhone/iPad Workspaces, Baum, Tabs und Verlauf durchsuchen.
- `IOS-02`: gespeicherte Seite und Ordner anlegen.
- `IOS-03`: Baumknoten verschieben, umbenennen und löschen; Mac-Gegenprüfung.
- `IOS-04`: Link im gewählten Standardbrowser öffnen.
- `IOS-05`: Link an bestimmten Mac und Workspace senden.
- `IOS-06`: normalen Mac-Tab remote öffnen.
- `IOS-07`: normalen Mac-Tab remote fokussieren.
- `IOS-08`: normalen Mac-Tab nach Bestätigung schließen.
- `IOS-09`: Offlinebefehl und TTL prüfen.
- `IOS-10`: Status `queued/delivered/executed/failed` prüfen.
- `IOS-11`: Replay wird abgewiesen.
- `IOS-12`: falsches Zielgerät und ungültige Signatur werden abgewiesen.
- `IOS-13`: Gerätefreigabe und Geräteentzug.
- `IOS-14`: Inkognito bleibt unsichtbar und nicht steuerbar.
- `IOS-15`: beliebige Custom Schemes, Shellbefehle und Massenaktionen werden abgewiesen.

### Updates, Crash und Recovery

- `UPDATE-01`: signierten N−2-Build installieren, realistisches Profil erzeugen und auf aktuellen RC aktualisieren.
- `UPDATE-02`: N−1 auf aktuellen RC.
- `UPDATE-03`: Delta-Update.
- `UPDATE-04`: Full-Fallback.
- `UPDATE-05`: Download während Update unterbrechen; alter Build bleibt startfähig.
- `UPDATE-06`: Netzverlust und Wiederaufnahme.
- `UPDATE-07`: manipulierten Appcast beziehungsweise Manifest ablehnen.
- `UPDATE-08`: Paket mit falschem Hash oder falscher Signatur ablehnen.
- `UPDATE-09`: Dogfood-, Beta- und Stable-Kanäle trennen.
- `UPDATE-10`: Datenmigration erhält Baum, Sessions, Einstellungen und lokale Credentials.
- `CRASH-01`: Renderer eines Tabs beenden; andere Tabs bleiben intakt.
- `CRASH-02`: GPU-Prozess beenden; UI und Video erholen sich.
- `CRASH-03`: Browserprozess mit gespeicherten und temporären Tabs hart beenden.
- `CRASH-04`: Recovery-Dialog bedienen und normale Sitzung wiederherstellen.
- `CRASH-05`: Crash während Tree-Schreibvorgang.
- `CRASH-06`: Crash während Sync-Commit beziehungsweise Migration.
- `CRASH-07`: keine Zyklen, Duplikate oder beschädigte Daten nach Recovery.
- `CRASH-08`: Inkognito wird nach Crash nie angeboten.

### Lean Chromium und Lieferumfang

- `LEAN-01`: Gegen unverändertes Chromium derselben Revision und einen Ahoi-Vollbuild mit aktivierten Auditkandidaten reproduzierbare Baselines für installiertes App-/DMG-Volumen, Mach-O-Segmente, Resource Packs, geladene Libraries, Prozesse, Start, Idle, Netzwerk, GPU und RAM erstellen.
- `LEAN-02`: Die maschinenlesbare Komponentenmatrix vollständig gegen GN-Graph, Feature-Defaults, Einstellungen, Policies, Strings, Migrationen, Component-Updater und Runtime-Registrierungen abgleichen; jeder Ausschluss besitzt Begründung, Messwert, Rollback und Test-ID.
- `LEAN-03`: Google-/Chrome-Konto und Chrome Sync, Promotions/Onboarding, Commerce/Shopping, Feed/Discover, Lens, Glic/AI/Actor/On-Device-Model, gebündelte Apps und funktionsgebundene Background-Services einzeln auditieren; keine Kategorie wird pauschal entfernt.
- `LEAN-04`: Für jede als `exclude-from-build` markierte Funktion nachweisen, dass Code, Ressourcen, Strings, Settings-Routen und alleinige transitive Abhängigkeiten nicht mehr im Releasebundle liegen; `runtime-disable` ist nur mit begründetem Roll-/Rollbackvorteil zulässig.
- `LEAN-05`: Mit frischem Profil und anschließendem Daily-Driver-Profil nachweisen, dass deaktivierte Komponenten keine periodischen Tasks, Wakeups, Netzwerkzugriffe, Downloads, Component-Registrierungen oder Lazy-Loads erzeugen.
- `LEAN-06`: Das installierte Ahoi-Releasebundle bleibt höchstens 3 Prozent größer als das identisch gebaute unveränderte Chromium-Bundle; gegenüber dem dokumentierten Ahoi-Vollbuild werden mindestens 10 Prozent Bundle-Footprint eingespart oder das ausdrücklich definierte Produktentscheidungs-Gate wird mit vollständiger Größenbilanz ausgelöst.
- `LEAN-07`: Vollständige Browserfähigkeit, Webplattform, Sandbox, Site Isolation, Safe Browsing, Zertifikats-/Netzwerksicherheit, Extensions/Native Messaging, DevTools, Medien, Downloads, PDF/Druck, Passwörter, Passkeys, HTTP Auth, Permissions, Accessibility, Übersetzung sowie Crash-/Session-Recovery nach jeder Entschlackungswelle regressionsfrei bestätigen.
- `LEAN-08`: AnyChat, 1Password, Bitwarden, uBlock Origin Classic, CloudKit-Sync, Arc-Import, Split Views und Developer Toolkit im installierten Ahoi-Profil nach der finalen Entschlackungswelle real bedienen; fremde Browserprofile bleiben unverändert.
- `LEAN-09`: Einen Chromium-Stable-/Security-Roll durchführen und beweisen, dass ausgeschlossene Funktionen weder still reaktiviert noch als verwaiste Buildabhängigkeit zurückgebracht werden und sicherheitsrelevante neue Upstream-Komponenten nicht versehentlich ausgeschlossen sind.
- `LEAN-10`: Jede Entschlackungswelle separat zurückrollen, erneut bauen und die erwartete Größen-/Runtime-Differenz reproduzieren; nicht eindeutig zurechenbare oder nicht sicher rückbaubare Eingriffe werden nicht übernommen.

### Performance und Daily Driver

Vergleiche immer gegen unverändertes Chromium derselben Revision, auf derselben Hardware, mit gleichen Flags, gleichen Profilbedingungen und derselben Sitzung. Verwende mehrere Läufe und dokumentiere Methodik und Streuung.

- `PERF-01`: Speedometer-Regression höchstens 3 Prozent.
- `PERF-02`: Startzeit höchstens 10 Prozent schlechter.
- `PERF-03`: Command Bar p95 unter 50 ms.
- `PERF-04`: sichtbarer Workspace-Wechsel unter 100 ms.
- `PERF-05`: flüssiges Scrollen und Interagieren mit 10.000 Baumknoten, Ziel 120-Hz-tauglich.
- `PERF-06`: eigener Memory-Overhead bei identischer 20-Tab-Sitzung höchstens 5 Prozent.
- `PERF-07`: keine messbare eigene Idle-CPU-Last.
- `PERF-08`: Developer Compiler und Editoren werden lazy geladen und wieder freigegeben.
- `PERF-09`: mindestens ein kompletter echter Arbeitstag mit Entwicklung, DevTools, mehreren Workspaces, Extensions, Video, PiP und Downloads.
- `PERF-10`: Ressourcenverlauf, Abstürze, Hänger und subjektiv störende Ruckler dokumentieren und vor Release beheben.
- `PERF-11`: 100-Tab-Sitzung mit Memory Saver messen; inaktive geeignete Tabs werden nach dokumentierter Policy verworfen, sichtbarer RAM sinkt nachvollziehbar und Reaktivierung erhält URL, Navigation History und Sessionzustand soweit Chromium dies garantiert.
- `PERF-12`: aktive Tabs sowie Audio, Video, MiniPlayer, PiP, Capture/WebRTC, Downloads, nicht abgesendete Formulare, HTTP-Auth-/Permission-Flows und DevTools werden während ihres kritischen Zustands nicht fälschlich verworfen.
- `PERF-13`: Schlafzustand in Sidebar/Tabstatus verständlich anzeigen, manuell aufwecken sowie Origin/Tab auf eine Never-Sleep-Liste setzen und wieder entfernen; Persistenz und Tastaturbedienung prüfen.
- `PERF-14`: schwebende Navigationszeile, Glass, Schatten, Popup-Overlay, MiniPlayer und Floating Sidebar einzeln und kombiniert profilieren; deaktivierte beziehungsweise verborgene Flächen erzeugen keine relevante Idle-CPU-, GPU- oder Memory-Regression.
- `PERF-15`: Hintergrundtimer-, Renderer- und Netzwerkdrosselung mit identischem Upstream-Chromium vergleichen; AhoiBrowser verschlechtert aktive Entwicklungs-, Media- oder Extension-Workloads nicht und dokumentiert jede absichtliche Abweichung.

## Evidenzpakete

Lege pro Testlauf ein Paket unter folgendem Schema an:

`artifacts/e2e/<version>/<test-id>/`

Jedes Paket enthält:

- Test-ID und Status;
- Datum und ausführende Person beziehungsweise Agent;
- Git-SHA;
- Chromium-Revision;
- App-Version und Buildnummer;
- installierten App-Pfad;
- Gerät und Architektur;
- macOS-/iOS-Version;
- Profiltyp und dokumentierten Startzustand;
- genaue sichtbare Benutzeraktionen;
- Soll- und Ist-Ergebnis;
- Screenshot oder sichere Bildschirmaufnahme;
- relevante, redigierte Logs;
- Fixture-Server-Receipt;
- Netzwerk-/HAR-/Proxy-Evidenz, wenn relevant;
- Datei-Hashes bei Downloads und Updates;
- Unit-/Integration-Testreport;
- verknüpfte Fehlernummer;
- Evidenz des bestandenen Wiederholungslaufs.

Große Bildschirmaufnahmen sollen Git nicht unnötig aufblähen. Speichere sie als CI-/Release-Artefakte oder über einen dokumentierten Artefaktspeicher; committe Manifest, Checksums, redigierte Screenshots und stabile Links.

Zulässige Teststatuswerte:

- `PASS`;
- `FAIL`;
- `BLOCKED_USER_ASSISTANCE`;
- `BLOCKED_CREDENTIAL`;
- `BLOCKED_ENTITLEMENT`;
- `BLOCKED_EXTERNAL_SERVICE`;
- `NOT_RUN`.

`NOT_RUN` und jeder `BLOCKED_*`-Status sind ausdrücklich kein Erfolg.

## Echte Nutzeraktionen und externe Voraussetzungen

Folgende Prüfungen dürfen nicht vorgetäuscht oder allein durch Programmatik ersetzt werden:

- physische Magic-Mouse-Geste;
- Touch ID;
- Kamera und Mikrofon;
- physische Medientasten;
- echte Bildschirm-/Fensterfreigabe;
- einmalige macOS-TCC- und Browserfreigaben;
- 1Password-/Bitwarden-Test-Vault;
- Netflix- und zweiter DRM-Testaccount;
- zwei reale Macs;
- echtes iPhone oder iPad;
- Apple Developer ID;
- Notarisierung;
- Production-CloudKit-Container;
- Widevine-Partnerschaft und CDM-Distributionsfreigabe;
- rechtliche H.264/AAC-Freigabe;
- gegebenenfalls notwendige Google-/Chrome-Web-Store-Zugänge;
- Update- und Extension-Katalog-Signing beziehungsweise Hosting;
- juristische Prüfung von GPL-§7, iOS-App-Store-Verteilung, Marke und proprietären Komponenten.

Bei einer erforderlichen Nutzeraktion:

1. Bereite alle automatisierbaren Schritte vor.
2. Sage präzise, welche eine Aktion benötigt wird und warum.
3. Lasse keine Geheimnisse im Chat wiedergeben.
4. Verifiziere nach der Aktion den tatsächlichen Systemzustand selbst.
5. Fahre anschließend mit der Testmatrix fort.

Bei externen Blockern dokumentiere:

- betroffene Test- und Gate-IDs;
- fehlende Voraussetzung;
- zuständige Stelle oder Owner, sofern bekannt;
- bereits durchgeführte Versuche;
- bestandene lokale Kontrolltests;
- genau nächste erforderliche Aktion.

## Harte Release-Gates

### Architecture Gate

- echte Chromium-Mehrprozessarchitektur;
- Sandbox und Site Isolation aktiv;
- kein Electron-/CEF-/WebView-Ersatzpfad;
- alle eigenen Chromium-Abweichungen inventarisiert und begrenzt.

### Bootstrap Gate

- frische geeignete ARM64-Maschine kann die gepinnte Revision anhand der Dokumentation beziehen und bauen;
- kein unveröffentlichtes lokales Overlay erforderlich;
- Buildprovenance und Pins sind nachvollziehbar.

### UI/Product Gate

- Sidebar, vollständiges Tree-Drag-and-drop, nested Tree, Zwei-/Drei-/Vier-Pane-Split View, Workspaces, Command Bar, Quick Window, Inkognito und Session Restore bestehen installierte Computer-Use-Tests;
- schwebende Auto-Hide-Navigationszeile samt Reveal-Notch und Extensions, abgerundeter WebContents-Container, angedockte/schwebende Sidebar, MiniPlayer und sichere Popup-Overlays bestehen die vollständigen installierten CU-E2E-Flows ohne unerwarteten Web-Viewport-Reflow;
- Deutsch und Englisch vollständig;
- Hell/Dunkel/System/Glass und Accessibility abgenommen;
- mehrere Fenster, Cross-Window-Tab-Drag, 10.000-Knoten-Baum, Workspace-Dots, echte Magic-Mouse-/Trackpad-Gesten, `⌘`-Scroll-Tabwechsel und Mittelklick-Auto-Scrolling funktionieren;
- alle `SPLIT-*`-Tests einschließlich Layouts, Divider, Fokus-/Origin-Zuordnung, Accessibility und normalem/Inkognito-Recovery sind `PASS`.

### Browser Capability Gate

- Downloads, Uploads, Drucken, PDF und Standardbrowser funktionieren;
- Video, PiP, WebRTC, Kamera, Mikrofon, Bildschirmfreigabe und weitere Berechtigungen funktionieren;
- dieselben Media-, MiniPlayer-, PiP-, WebRTC-, Permission-, Download-, Upload-, Auth- und DevTools-Funktionen funktionieren korrekt aus jedem Pane eines Vierer-Splits;
- H.264/AAC-Distribution rechtlich geklärt;
- Widevine legal integriert;
- Netflix und zweiter DRM-Dienst bestanden.

### Extension Gate

- echte Chrome-Web-Store-MV3-Extension installiert, aktualisiert und entfernt;
- AnyChat mit verifizierter Store-ID ausschließlich im AhoiBrowser-Profil installiert, berechtigungsgeprüft, geöffnet und neustartfest;
- 1Password inklusive Native Messaging und Touch ID bestanden;
- Bitwarden bestanden;
- lokaler Chromium-Passwortmanager einschließlich Mehrkontoauswahl, Bearbeitung, sicherer Klartextanzeige, Autofill-/Passkey-Abgrenzung und Inkognito-Policy bestanden;
- uBlock Origin Classic einschließlich Updates bestanden;
- nicht allowlistetes MV2 bleibt gesperrt.

### Arc-Migrations-Gate

- Arc-Discovery, immutable Snapshot, WAL-/SHM-Konsistenz, Parserlimits und sichere Ausschlussregeln bestanden;
- Spaces, Listen, Tabs und valide Splits werden deterministisch in das native Ahoi-Modell übernommen; beschädigte Splits degradieren ohne Datenverlust oder Phantom-Tabs;
- Vorschau, Konfliktauflösung, atomarer Commit, Crash-Rollback und idempotenter No-op-Wiederholungslauf bestanden;
- ein realer lokaler Arc-Datenstand wurde nach Backup und Dry Run in das installierte AhoiBrowser-Profil importiert und sichtbar geprüft;
- Arc, Google Chrome, fremde Extension Storage, Native-Messaging-Manifeste, Cookies, Sessions, Passwörter und Geheimnisse blieben unverändert beziehungsweise ausgeschlossen.

### HTTP-Auth Gate

- mehrere `.htaccess`-/Basic-Auth-Konten pro Realm speicher- und auswählbar;
- Autocomplete und Tastaturbedienung;
- korrekte Trennung nach Ziel, Scheme, Host, Port, Realm und Schutzbereich;
- Passwortupdate;
- gezielter Kontowechsel und Abmeldung ohne Browserneustart;
- kein Credential-Leak über Origin- oder HTTPS-Grenzen;
- Inkognito-Isolation;
- keine Synchronisation oder Log-Leaks;
- vollständiger `AUTH-27`-Computer-Use-Flow bestanden.

### Developer/Privacy Gate

- gesamtes schlankes Developer Toolkit bestanden;
- deaktiviertes Toolkit verursacht keine relevante Laufzeitlast;
- `Mehr Schutz` und `Maximale Website-Kompatibilität` bestanden; interne Policy-Begriffe erscheinen nicht ungeklärt in der UI;
- Safe Browsing aktiv;
- Fresh-Profile-Netzwerkaudit ohne unbekannte Hintergrundendpoints;
- keine Produkttelemetrie oder automatischen Crash-Uploads.

### Sync/Companion Gate

- zwei Macs plus iOS/iPadOS bestehen Online-, Offline-, Konflikt-, Lösch-, Recovery- und Geräteentzugstests;
- History, normale Tabs und Baum funktionieren;
- die normale Sidebar zeigt fremde Mac-/iOS-Tabs direkt, eindeutig nach Gerät gekennzeichnet und ohne separate Verwaltungsseite; sie funktioniert ohne Google-Konto oder Chrome Sync;
- Remote Control ist signiert und replay-sicher;
- Cookies, Passwörter, HTTP Auth, Site Storage, Permissions, Extension Storage, Inkognito und Keychain-Secrets bleiben lokal.

### Update/Recovery Gate

- signiertes N−2- und N−1-Update im installierten Bundle erfolgreich;
- Delta- und Full-Fallback funktionieren;
- manipulierte Updates werden abgewiesen;
- Renderer-, GPU-, Browser- und Schreibcrash-Recovery bestanden;
- Zwei-/Drei-/Vier-Pane-Mitgliedschaft, Layout, Ratios und Fokus über Neustart, N−2-/N−1-Migration, Tab Restore und Crash maximal verlustfrei wiederhergestellt beziehungsweise ohne Phantom-Tab sicher degradiert;
- Inkognito wird nie wiederhergestellt.

### Performance Gate

- alle definierten Budgets gegen identisches Upstream-Chromium eingehalten;
- Lean-Chromium-Komponentenmatrix, Größenbilanz und GN-/Runtime-Nachweis sind vollständig; ausgeschlossene Produktmodule sind nicht bloß versteckt und erzeugen weder paketierten Ballast noch Hintergrundaktivität;
- Bundle-Grenze von höchstens 3 Prozent über identischem Upstream sowie das 10-Prozent-Ziel gegenüber dem Ahoi-Vollbuild sind bestanden oder ausschließlich das vorab definierte, evidenzbasierte Produktentscheidungs-Gate für das 10-Prozent-Ziel ist offen;
- mindestens ein aktueller Chromium-Roll beweist, dass Entschlackung updatesicher bleibt und keine Security-Komponente still verloren geht;
- Memory Saver und Tab-Discarding senken den Ressourcenverbrauch nachvollziehbar, schützen alle kritischen Media-/Capture-/Download-/Formular-/DevTools-Zustände und erwachen ohne Phantom- oder Duplikat-Tabs;
- schwebende Browserflächen, Glass, Schatten, Popup-Overlay, MiniPlayer und Floating Sidebar verursachen weder relevante Idle-Last noch störende Animation-/Resize-Lags;
- kein reproduzierbarer Hänger oder auffälliges UI-Lag;
- kompletter Daily-Driver-Soak bestanden.

### Legal/Release Gate

- Developer-ID-Signierung, Notarisierung und Stapling bestanden;
- Codec-, Widevine-, GPL-/App-Store-, Third-Party- und Trademark-Prüfung abgeschlossen;
- SBOM, Checksums, Third-Party Notices und Evidenzmanifest vorhanden;
- keine offenen P0-/P1-Fehler;
- kein kritischer Test `NOT_RUN` oder `BLOCKED_*`.

## Definition of Done

AhoiBrowser ist erst öffentlich releasebereit, wenn gleichzeitig gilt:

1. Der vollständige gepinnte Bootstrap ist auf einer frischen geeigneten Apple-Silicon-Maschine nachvollziehbar.
2. Chromium-Prozessarchitektur, Sandbox und Site Isolation sind im Release aktiv.
3. Eigene Patches sind klein, dokumentiert, getestet und mit einem aktuellen Chromium-Stable-Roll kompatibel.
4. Das signierte und notarisierte Bundle läuft unter `/Applications/AhoiBrowser.app`.
5. Nested Tree, vollständiges Sidebar-Drag-and-drop, Zwei-/Drei-/Vier-Pane-Split Views einschließlich persistiertem 2×2, Workspaces samt Dots/Swipe, echter Null-Tab-/Empty-Workspace-Zustand, Command Bar, Quick Window, Inkognito, mehrere Fenster und Sitzungswiederherstellung bestehen reale CU-E2E-Tests.
6. Schwebende Auto-Hide-Navigationszeile mit Reveal-Notch und Extensions, abgerundeter WebContents-Container, Glass, Floating Sidebar und Web-Popup-Overlays funktionieren ohne falschen Viewport-Reflow und bestehen die vollständigen CU-E2E-Fälle.
7. Downloads, Uploads, PDF, Drucken, Medien, Sidebar-MiniPlayer, PiP, WebRTC und Permissions bestehen reale Tests.
8. Chrome-Web-Store-Extensions, AnyChat, lokaler Passwortmanager, 1Password, Bitwarden und uBlock Origin Classic funktionieren ausschließlich im installierten AhoiBrowser-Profil; fremde Browserprofile blieben unverändert.
9. HTTP Basic Auth/`.htaccess` bietet Speicherung, mehrere Konten, Auswahl, Autocomplete, Update, Wechsel und Abmeldung und besteht die vollständige Auth-Testgruppe.
10. Developer Toolkit und beide Privacy-Modi sind vollständig abgenommen.
11. Chromiums unveränderte Zurück-/Vor-Geste ist regressionsfrei; Ahois Workspace-Swipe, `⌘`-Scroll-Tabwechsel und Mittelklick-Auto-Scrolling sind konfliktfrei, konfigurierbar und real abgenommen.
12. Fresh-Profile-Netzwerk und Telemetriefreiheit sind nachgewiesen.
13. Mac–Mac–iOS-Sync, Geräte-Tabs ohne Google-Konto, Offlinekonflikte, Recovery und Remote Control funktionieren auf realen Geräten.
14. Keine ausgeschlossenen Secrets oder privaten Daten werden synchronisiert.
15. N−2-/N−1-Updates funktionieren; manipulierte Artefakte werden abgewiesen.
16. H.264/AAC sind rechtlich geklärt.
17. Widevine ist legal integriert und zwei reale DRM-Dienste funktionieren.
18. Performancebudgets, Memory Saver, sichere Tab-Sleeping-Ausnahmen und Wiederaufwecken gegen dieselbe Chromium-Revision sind eingehalten.
19. Ein vollständiger Daily-Driver-Arbeitstag ist ohne ungeklärten Crash, Hänger oder störendes Lag bestanden.
20. Keine P0-/P1-Fehler sind offen.
21. Security-, Privacy-, Lizenz- und Trademark-Reviews sind abgeschlossen.
22. Der Arc-Import besteht sichere Snapshot-, Vorschau-, Atomizitäts-, Rollback-, Idempotenz- und reale installierte UI-Abnahmen; der vorhandene lokale Arc-Datenstand ist mit redigierter Evidenz migriert.
23. Releaseartefakte, SBOM, Checksums, Lizenzen, Revisionen und E2E-Evidenz sind vollständig.
24. Lean-Chromium-Matrix, Bundle-/Runtime-Bilanz, Null-Aktivitätsnachweise und Roll-Regressionen sind vollständig; Ahoi ist innerhalb der definierten Größenbudgets schlanker, ohne zugesagte Browserfähigkeit, Extension-Kompatibilität, Webkompatibilität oder Security zu verlieren.

## Explizit nicht Bestandteil von v1

- Windows;
- Linux;
- Intel-Macs;
- getrennte Chromium-Profile pro Workspace;
- vollständiger iOS-Browser;
- eingebettete iOS-WebView als Browserersatz;
- Chrome Sync;
- Google-Browserkonto;
- Cookie-Sync;
- Passwort-Sync;
- HTTP-Auth-Credential-Sync;
- Extension-Storage-Sync;
- eigener Adblocker;
- eigene Filterlisten-Engine;
- allgemeine Manifest-V2-Unterstützung;
- AI-Assistent;
- Wallet;
- Screenshot-Editor;
- Boost-Plattform;
- eigenes soziales Sharing-System;
- importierbare Theme-Pakete;
- vollständiger Nachbau von Web Developer;
- integrierter vollständiger Accessibility-Scanner;
- Unterstützung von Extensions, die AhoiBrowsers eigene UI verändern wollen;
- mehr als vier gleichzeitig sichtbare Panes in einer Split-Gruppe.
- tiefgreifende Eigenforks von Blink, V8, `net`, Media, Sandbox oder Site Isolation nur zur kosmetischen Größenreduktion.

## Commit-, GitHub- und Berichtsregeln

- Erzeuge logisch getrennte, überprüfte Commits.
- Committe keine Buildartefakte, Secrets oder riesigen Chromium-Checkout.
- Pushe abgeschlossene Meilensteine auf den konfigurierten GitHub-Remote.
- Falls noch kein Remote oder GitHub-Owner festgelegt ist, blockiere die lokale Umsetzung nicht. Fordere diese Information erst an, wenn der erste sinnvoll veröffentlichbare Stand bereitsteht.
- Öffentliche GitHub-Dokumentation darf keinen nicht bestandenen Status als fertige Funktion darstellen.
- Private Dogfood- und Beta-Builds sind vor öffentlichem Stable erlaubt und ausdrücklich erwünscht.

Liefere bei jedem Meilenstein:

- konkretes Ergebnis;
- relevante Commits;
- Chromium-Revision;
- ausgeführte programmatische Tests;
- installierte Bundle-Version;
- ausgeführte Computer-Use-/Assisted-E2E-Tests;
- verlinkte Evidenz;
- Performanceauswirkung;
- bekannte Risiken;
- externe Blocker;
- nächsten konkreten Meilenstein.

Kurze Statusmeldungen während langer Builds und Tests sollen Ergebnis, Beweisniveau und offenen Rest nennen. Vermeide lange Wiederholungen bereits bestandener Testmatrizen; nach einer vollständigen grünen Suite genügen gezielte Regressionstests und Release-/Runtime-Nachweise.

## Startanweisung

Beginne jetzt mit Phase 0.

1. Lies alle lokalen Agenten- und Skill-Anweisungen.
2. Inventarisiere die reale Umgebung, ohne Annahmen über vorhandene Repositories oder Tools.
3. Prüfe Hardware, Speicher, Xcode und Chromium-Voraussetzungen.
4. Erstelle einen kurzen ausführbaren Arbeitsplan.
5. Initialisiere die lokale Projektgrundlage.
6. Starte den echten Chromium-ARM64-Bootstrap und Machbarkeitsspike.
7. Führe nach dem ersten installierbaren Build unmittelbar den ersten Computer-Use-Smoke auf `/Applications/AhoiBrowser.app` aus.
8. Fahre bei positivem Spike ohne erneute allgemeine Planungsrunde mit Phase 1 fort.

Antworte nicht lediglich mit einer weiteren Architekturübersicht und frage nicht pauschal, ob du beginnen sollst.

## Autoritative Ausgangsquellen

- [Chromium macOS build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md)
- [Chromium multi-process architecture](https://www.chromium.org/developers/design-documents/multi-process-architecture/)
- [Chromium Views](https://www.chromium.org/developers/design-documents/chromeviews/)
- [Chromium Split View Security FAQ](https://chromium.googlesource.com/chromium/src/+/fc4d67f1788019a27e32511137ceccbd2fafdaaa/chrome/browser/ui/tabs/docs/split_view_security_faq.md)
- [Chromium HTTP Auth Controller](https://chromium.googlesource.com/chromium/src/+/main/net/http/http_auth_controller.h)
- [Chromium LoginHandler and HTTP Auth Password Manager integration](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/ui/login/login_handler.cc)
- [Chromium NetworkContext and ClearHttpAuthCache](https://chromium.googlesource.com/chromium/src/+/main/services/network/network_context.cc)
- [Apple Liquid Glass](https://developer.apple.com/documentation/TechnologyOverviews/adopting-liquid-glass)
- [NSGlassEffectView](https://developer.apple.com/documentation/appkit/nsglasseffectview)
- [Chromium: Limiting Private API Availability in Chromium](https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html)
- [Chromium API Keys](https://www.chromium.org/developers/how-tos/api-keys/)
- [Apple CKSyncEngine](https://developer.apple.com/documentation/cloudkit/cksyncengine-5sie5)
- [Apple: Deciding whether CloudKit is right for your app](https://developer.apple.com/documentation/cloudkit/deciding-whether-cloudkit-is-right-for-your-app)
- [Apple CloudKit remote records](https://developer.apple.com/documentation/cloudkit/remote-records)
- [CloudKit encrypted user data](https://developer.apple.com/documentation/cloudkit/encrypting-user-data)
- [Chrome Manifest V2 support timeline](https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline)
- [uBlock Origin](https://github.com/gorhill/uBlock)
- [1Password additional browsers](https://support.1password.com/additional-browsers/)
- [Widevine partner access](https://developers.google.com/widevine/access)
- [GNU GPLv3](https://www.gnu.org/licenses/gpl.en.html)

Bei Abweichungen zwischen diesem Prompt und einer aktuellen autoritativen Plattformdokumentation prüfe die aktuelle Lage, dokumentiere die notwendige Anpassung und erhalte die Produktabsicht sowie die Sicherheits- und Release-Gates.
