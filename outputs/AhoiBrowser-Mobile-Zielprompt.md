# Zielprompt: AhoiBrowser Mobile end to end

## Auftrag

Entwickle den vorhandenen nativen iOS-/iPadOS-Companion zu **AhoiBrowser
Mobile**, einer eigenstaendigen, als Standardbrowser nutzbaren Browser-App fuer
iPhone und iPad. Die App verwendet Apples systemgeliefertes WebKit als Engine,
eine native SwiftUI-Oberflaeche und die bereits vorhandenen Ahoi-Modelle fuer
Workspaces, Baum, Suche, Verlauf, Geraete-Tabs, CloudKit und signierte
Remote-Befehle.

Das Vorbild ist die ruhige, such- und workspace-zentrierte Bedienidee moderner
Mobile-Browser wie Arc. Es werden weder fremde Marken, Texte, Assets, Animationen
noch pixelgenaue Oberflaechen kopiert. Visuell und sprachlich bleibt die App ein
eigenstaendiges Ahoi-Produkt und uebertraegt die Informationsarchitektur,
Farbtokens, Begriffe und Interaktionsprinzipien des AhoiBrowser-Desktops auf
Touch, kleine Displays, iPad, Tastatur und Pointer.

Arbeite bis zum real verifizierten Ergebnis. Ein Quellcode-, Simulator- oder
Unit-Test-Pass darf nie als Geraete-, CloudKit-, TestFlight- oder Release-Pass
bezeichnet werden.

## Ziel-Lock und Laufzeitwahrheit vom 30. August 2026

Dieser Zielprompt ist die autoritative Arbeitsgrundlage fuer die laufende
Mobile-Welle. Er ersetzt keine reale Evidenz durch Absichtserklaerungen. Der
aktuell gepruefte Arbeitsstand umfasst bereits einen nativen SwiftUI-/WebKit-
Browser-Kandidaten unter `apps/AhoiMobile/` mit normalem und privatem Browsing,
Adress-/Befehlssuche, Tab-Uebersicht, Workspaces/Mediathek, Verlauf, Downloads,
Dialog-/Permission-Grenzen, lokaler Persistenz und dem vorhandenen
Mac-Mobile-Sync-Core.

Aktuell in dieser Welle belegt:

- Xcode 26.6 / iOS-Simulator-SDK 26.5 kompiliert den Kandidaten in Debug auf
  iPhone und in Release als universelles `arm64`-/`x86_64`-Simulatorprodukt mit
  Exit 0 (`LOCAL_BUILD_PASS`);
- frisch installierte iPhone- und iPad-Simulatoren zeigen die lokale Browser-
  Fixture, Adress-/Befehlssuche, Tab-Uebersicht, privaten Modus, Browser-
  Control-Center, Workspace-/Geraete-Mediathek und adaptive Sidebar
  (`SIMULATOR_VISIBLE`, begrenzter Audit-Pass);
- der P0-Slice ersetzt das abgeschnittene Langmenue durch ein gruppiertes
  Control-Center, vergroessert die iPad-Suche auf eine ruhige Page-
  Praesentation und uebersetzt den privaten Startzustand auf Ink/Graphite;
- 56 Mobile-Core-Tests (davon zwei ehrlich wegen fehlendem entitlement
  uebersprungen), 3 UI-Journeys und 36 CloudKit-/Remote-Command-Pakettests sind
  ohne Fehler durchgelaufen;
- die aktuelle sichtbare Evidenz liegt unter
  `docs/audit-evidence/2026-08-30-ios-arc-alignment/`.

Ausdruecklich noch nicht belegt sind der vollstaendige Alltagsflow auf iPhone
und iPad, reale Websites unter variierenden Netz-/Medien-/Login-Bedingungen,
physische Geraete, Default-Browser-Entitlement, echter Mac-Mobile-CloudKit-
Roundtrip, signiertes Archive, TestFlight und Release. Die Dokumentation und
`config/test-registry.json` muessen diese Trennung jederzeit ehrlich
wiedergeben.

## Referenzvergleich: Arc Search, Stand 30. August 2026

Die Referenz ist Arc Search fuer iOS, nicht ein pixelgenauer Klon und nicht Arc
Desktop. Verglichen werden die aktuelle App-Store-Fassung 1.48.0 und die
offiziellen Produkt-/Hilfeseiten:

- `https://apps.apple.com/us/app/arc-search-find-it-faster/id6472513080`
- `https://arc.net/search`
- `https://resources.arc.net/hc/en-us/articles/20887042551831-Arc-for-iOS-Arc-Search`
- `https://resources.arc.net/hc/en-us/articles/20272860828823-Arc-Sync`
- `https://arc.net/blog/arc-search-hidden-features`
- `https://resources.arc.net/hc/en-us/articles/23528454620311-Arc-Search-for-iOS-Release-Notes`

### Uebernehmen: Prinzipien und mobile Interaktionen

- **Search first:** Ein leerer/neuer Tab bietet die Suche sofort und ohne
  Zwischenansicht an. Eine Einstellung steuert, ob die Tastatur beim Start oder
  beim letzten leeren Tab automatisch erscheint.
- **Thumb first:** Primaeraktionen bleiben unten und mit einer Hand erreichbar.
  Scrollen blendet Browser-Chrome kontrolliert ein und aus, ohne den Ursprung,
  Ladezustand oder private Nutzung unklar zu machen.
- **Direkte Gesten:** Horizontaler Wisch auf der unteren Leiste wechselt durch
  zuletzt verwendete Tabs. Pull-to-refresh startet erst beim Loslassen und ist
  vorher abbrechbar. Back/Forward bleibt mit WebKit-Gesten kompatibel.
- **Schnelle Tab-/Space-Navigation:** Kurzer Tap oeffnet Tabs, Long-Press fuehrt
  direkt zu Workspaces/Mediathek. Offene, gespeicherte und geraeteferne Seiten
  sind strukturell unterscheidbar, aber keine getrennten Produkte.
- **Ruhige Browserflaeche:** Die Website dominiert. Browser-Chrome darf sich
  subtil an Workspace- oder Seitenfarbe anlehnen, bleibt aber kontrastfest,
  sicherheitsklar und bei reduzierter Transparenz vollstaendig opak.
- **Private Eindeutigkeit:** Privat ist bereits vor der Navigation durch
  Hintergrund, Adressflaeche, Text und Symbol eindeutig. Ein Moduswechsel
  braucht keine Suche in einem separaten Einstellungsmenue.
- **Alltagsdetails:** Linkvorschau mit neuem Vorder-/Hintergrundtab, Reader,
  Uebersetzung, Seitenzoom, Desktop-Site, Picture-in-Picture, Handoff,
  Share-Action, Widget und Default-Browser-Einstieg werden als native
  Plattformfaehigkeiten bewertet.

### Nicht uebernehmen

- kein `Browse for Me`, `Call Arc`, Pinch-to-Summarize oder sonstiger
  verpflichtender AI-/Cloud-Antwortdienst;
- kein eingebauter breiter Adblocker, Trackerblocker oder Cookie-Banner-
  Automatismus in dieser Welle; Ahoi haelt die festgelegte Extension- und
  Privacy-Grenze ein;
- kein stilles Auto-Archivieren. Eine spaetere Aufraeumfunktion ist opt-in,
  zeigt Vorschau/Undo und verwechselt nie temporaer, gespeichert oder
  synchronisiert;
- kein unsicherer Zertifikats-Bypass, kein stilles Oeffnen externer Apps und
  keine fremden Marken, Texte, Illustrationen, Sounds oder Animationen;
- keine Arc-Account-Abhaengigkeit. Ahoi startet local-first und bleibt ohne
  CloudKit als vollwertiger Browser nutzbar.

### Bewusster Ahoi-Vorsprung

- bidirektionale, verschluesselte Mac-iPhone-iPad-Replikation normaler Tabs,
  Workspaces und Baumzustand statt einer nur lesenden Mobile-Sidebar;
- tiefere Ordner-/Workspace-Hierarchie mit stabilen IDs, Tombstones und
  konfliktfester Reihenfolge;
- signierte Remote-Aktionen wie `Auf Mac oeffnen`, `Fokussieren` und
  bestaetigtes Schliessen;
- keine Produkttelemetrie und keine Kontopflicht fuer lokales Browsing;
- iPad als echte adaptive Ahoi-Oberflaeche mit Sidebar, Tastatur und Pointer,
  nicht nur als vergroesserte iPhone-Ansicht.

## Priorisierter Umsetzungsauftrag dieser Welle

### P0 - sichtbare Produktqualitaet und belastbarer Browserkern

1. Pruefe den aktuellen Dirty-Worktree, sichere fremde Aenderungen und arbeite
   ausschliesslich im vorhandenen Mobile-Branch weiter. Vermische die Welle
   nicht mit einem laufenden Ahoi-Chromium-Build.
2. Behebe jede Start-, Restore-, Crash-, Blank-Screen- und Doppel-Tab-Ursache,
   bevor weitere Oberflaeche hinzugefuegt wird.
3. Reduziere die ueberladene iPhone-Aktionsliste. Primaere Aktionen bleiben
   direkt erreichbar; seltene Seiten-, Daten- und App-Aktionen erhalten klar
   benannte Gruppen in einem ruhigen Sheet/Control-Center statt eines langen,
   abgeschnittenen Pop-up-Menues.
4. Schaerfe die Befehlssuche: weniger optische Unruhe, stabiler Kontrast,
   sichtbare Ergebnisarten, begrenzte Treffer, Tastatursteuerung, eindeutiger
   aktiver Treffer und opaker Reduced-Transparency-Fallback.
5. Mache die Tab-Uebersicht workspace-zentriert und inhaltsreich, ohne
   Screenshot-Zwang: Favicon, Titel, Ursprung, saved/temporary, Aktivitaet,
   Workspace, Auswahl und Close/Undo. Leerer Raum darf nicht die eigentliche
   Hierarchie ersetzen.
6. Stelle den privaten Modus in einer eigenstaendigen Ahoi-Sprache dar:
   tiefes neutrales Ink/Graphit, eindeutiges Schutzsymbol und Text; kein
   verspielter Magenta-Klon und keine persistente/synchronisierte Spur.
7. Behebe sichtbare Accessibility-Luecken. Insbesondere muessen Sheet-
   Navigationsbuttons, Plus, Fertig, Bearbeiten, Modusauswahl und alle
   symbol-only controls als echte, fokussierbare Controls mit Namen, Wert,
   Hinweis und mindestens 44x44 pt Zielgroesse erscheinen.
8. Belege den Kernflow mit frischem Profil auf iPhone und iPad: Start, reale
   HTTPS-Seite, Suche, Back/Forward/Reload, neuer Tab, Wechsel, Close/Undo,
   Workspace speichern/verschieben, Privat, Kill/Restore und Offline/Retry.

### P1 - mobile Geschwindigkeit nach Arc-Vorbild

1. Implementiere optionales Auto-Fokus/Keyboard fuer neuen/leeren Tab.
2. Implementiere Tab-Flick auf der unteren Leiste mit sauberer
   Back/Forward-Gestenabgrenzung, Haptik nur bei tatsaechlichem Wechsel und
   Reduced-Motion-Ersatz.
3. Implementiere eine beim Scrollen kompakt werdende Bottom-Bar, die bei
   Aufwaertsscrollen, Fokus, Navigation, Fehler oder privatem Status sofort
   wieder voll verstaendlich wird.
4. Implementiere Pull-to-refresh ueber WebKit/UIRefreshControl so, dass erst
   beim Loslassen geladen wird und der Nutzer vorher abbrechen kann.
5. Long-Press auf Tabzaehler/Tabstapel oeffnet direkt Workspaces/Mediathek;
   kurzer Tap bleibt Tab-Uebersicht. VoiceOver erhaelt getrennte Aktionen.
6. Fuehre lokale Top Sites aus explizit gespeicherten oder haeufig genutzten
   normalen Seiten ein. Private Daten, Sync-Geheimnisse und externe
   Empfehlungsdienste bleiben ausgeschlossen.

### P2 - Desktop-Funktionen sinnvoll auf Mobile uebertragen

- **Jetzt migrieren:** einheitliche Suche ueber lokale Tabs, Workspaces, Baum,
  erlaubten Verlauf und Geraete-Tabs; saved/temporary; Remote-Tab-Oeffnen;
  `Link an Mac`; Umbenennen, Speichern und Verschieben; Downloads/Verlauf;
  Appearance-/Search-/Sync-Einstellungen; sichere Website-Daten-Aktionen.
- **Mobil uebersetzen:** Desktop-Sidebar wird auf iPhone zu Mediathek/Sheet und
  auf iPad zur persistenten, kollabierbaren Sidebar. Desktop-Cmd+T wird zur
  mobilen Befehlssuche. Hover-Aktionen werden zu Swipe-/Context-Actions mit
  Undo. Desktop-Workspace-Geste wird auf Mobile nur in konfliktfreien
  Startbereichen angeboten.
- **Plattformgerecht begrenzen:** Zwei echte Seiten duerfen auf iPad spaeter als
  speichersicherer Split erscheinen. Drei-/Vier-Pane-Desktop-Splits, DevTools,
  Extensions, Request-Header-/CSP-/CORS-Editoren und Little-Arc-Fenster werden
  nicht als halb funktionierende iPhone-Kopie gebaut.
- **Spaeter nach belastbarem Kern:** Reader, Uebersetzung, PiP, Handoff,
  Share-Extension, Home-/Lock-Screen-Widget, Spotlight und optionales
  tabbezogenes Aufraeumen mit Vorschau und Undo.

## Code- und Architekturkonventionen

- Swift 6 Strict Concurrency bleibt aktiv. UI, `WebPage` und Presenter gehoeren
  auf `@MainActor`; Stores, Migration, Sync, Indizes und Dateischreibvorgaenge
  sind Actors oder klar isolierte `Sendable`-Typen.
- `@unchecked Sendable` ist nur fuer eine schmale, dokumentierte Apple-API-
  Bruecke mit eigenem Synchronisationsbeweis erlaubt. Neue globale Singletons,
  unstrukturierte Detached Tasks und versteckte Timer sind verboten.
- `MobileBrowserController` bleibt alleinige Autoritaet fuer Tab-/Session-
  Lebenszyklus. Views mutieren keine Persistenz, CloudKit-Records oder WebKit-
  Stores direkt. Abhaengigkeiten werden ueber Protokolle/Initialisierer injiziert.
- URL-/Scheme-/Origin-/External-App-/Download-/Permission-Entscheidungen laufen
  durch zentrale Policies. Keine zweite, leicht abweichende Validierung in
  View-Closures oder Sync-Code.
- Normale und private `WKWebsiteDataStore` werden konstruktiv getrennt. Private
  Tabs gelangen nie in Restore, Verlauf, Suche, Snapshot, Sync, Remote-Command,
  Crash-Evidenz oder Logs.
- Dateischreibvorgaenge sind versioniert und atomar (`temp -> fsync/replace` wo
  erforderlich). Migrationen besitzen Backup, Versionsmarker, Dry-/Read-Proof
  und idempotenten No-op-Zweitlauf.
- Stabile Domaenen-IDs bleiben unabhaengig von SwiftUI-/WebKit-Objektidentitaet.
  Wire-Format, kanonische JSON-Bytes, HLC-Semantik und Tombstones werden nicht
  fuer UI-Komfort umdefiniert.
- Neue oder wesentlich ueberarbeitete Swift-Dateien bleiben moeglichst unter
  800 Zeilen und haben genau eine Hauptverantwortung. Bereits uebergrosse
  Dateien werden bei beruehrten, klar trennbaren Bereichen schrittweise
  zerlegt; keine kosmetische Grossmigration ohne Testnutzen.
- Produktionsstrings stehen ausschliesslich im String Catalog mit Deutsch und
  Englisch. Keine Stringverkettung fuer Saetze, keine festen Textbreiten und
  keine sichtbaren Debug-/Fixture-Texte im Release.
- Verwende SF Symbols beziehungsweise vorhandene echte Ahoi-Assets. Keine
  Emojis, Unicode-Piktogramme, handgebauten SVG-/Canvas-Ersatzicons oder aus
  ImageGen ausgeschnittenen Bedienelemente.
- `project.yml` ist fuer generierte Xcode-Projektstruktur autoritativ. Wenn
  Targets/Dateien/Build Settings geaendert werden, XcodeGen reproduzierbar
  ausfuehren und `project.pbxproj` gemeinsam pruefen.
- Jeder Fehler hat einen lokalisierten, handlungsfaehigen Zustand. `try?` darf
  an Sicherheits-, Persistenz-, Sync- und Navigationsgrenzen keinen Fehler
  verschlucken.
- Tests folgen dem Risiko: Policy-/Model-/Migration-/Wire-Unit-Tests,
  Controller-Integration mit Fakes, danach wenige stabile UI-Tests und immer
  sichtbarer Simulator-/Geraetepass fuer die geaenderte Journey.

## Verbindliche Ahoi-Designsprache Mobile

### Charakter

Ruhig, maritim, warm, praezise und content-first. Ahoi uebernimmt Arcs raeumliche
Klarheit, nicht dessen Marke oder verspielte Gesten um ihrer selbst willen. Eine
Ansicht beantwortet sofort: Wo bin ich, welche Seite ist aktiv, in welchem
Workspace liegt sie, ist sie privat, und was ist die naechste Hauptaktion?

### Farbe und Material

- Grundflaechen: warme, leicht getoente System-Neutrals statt kaltem Vollweiss;
- Hauptakzent: Ahoi-Orange fuer aktive Handlungen und Markenmomente, nicht fuer
  jeden Text oder jede Flaeche;
- Strukturakzente: maritime Blau-/Petroltoene und benutzerdefinierte
  Workspace-Farben;
- Privat: tiefes Ink/Graphit mit ruhigem Violett nur als sparsame
  Sekundaerinformation;
- Glass/Blur nur fuer kurze Browser-Chrome- und Sheet-Ueberlagerungen. Kein
  mehrfaches Blur, das Text, Website und Ergebnistypen gleichzeitig verwascht;
- alle Kombinationen muessen in Hell/Dunkel, Increase Contrast, Differentiate
  Without Color und Reduce Transparency verstaendlich bleiben.

### Typografie, Raster und Controls

- ausschliesslich Dynamic-Type-faehige Systemtypografie mit klarer Hierarchie;
- 8-pt-Raster, 16-20 pt Seitenabstand auf iPhone, 20-28 pt auf iPad;
- mindestens 44x44 pt Touchziele, mindestens 8 pt Abstand zwischen
  konkurrierenden destruktiven/positiven Aktionen;
- Radien folgen Groesse und Bedeutung: kompakte Controls 10-14 pt, Karten und
  Sheets 18-28 pt; keine zufaellige Mischung;
- Browser-Chrome respektiert Safe Area, Tastatur, Home Indicator, Rotation,
  Stage Manager und Split View;
- Favicons sind echt oder neutrale, konsistente Fallbacks. Auswahl, saved,
  temporary, private, remote und offline sind nicht allein farbcodiert.

### Bewegung und Feedback

- Standarduebergaenge 180-240 ms, direkt und abbrechbar; Federn nur fuer
  raeumliche Tab-/Workspace-Bewegung;
- ein Tab-Flick folgt dem Finger und rastet anhand Distanz/Geschwindigkeit ein;
- Schliessen/Loeschen bietet Undo und entfernt die Zeile erst nachvollziehbar;
- Haptik bestaetigt Zustandswechsel, nicht jeden Tap;
- Reduce Motion ersetzt raeumliche Bewegungen durch kurze Crossfades ohne
  Informationsverlust.

## ImageGen-2-Layoutphase

ImageGen dient nur als visuelle Layoutstudie, nicht als Quelle fuer
Produktionsicons, Texte oder unpruefbare Browserfunktionen. Verwende als
Referenzen die frisch akzeptierten iPhone-/iPad-Screenshots, aktuelle Ahoi-
Desktop-Evidenz und die oben definierte Designsprache. Erzeuge zwei klar
unterschiedliche, jeweils zusammenhaengende Layoutboards:

1. **Richtung A - Content Deck:** maximal kleine, beim Scrollen kollabierende
   Bottom-Bar; zentrale Search-/Command-Karte; kompakte tab cards nach
   Workspace; dunkler privater Zustand; iPad mit schmaler einklappbarer Sidebar.
2. **Richtung B - Workspace Canvas:** Bottom-Bar mit staerker sichtbarer
   Workspace-Identitaet; Mediathek als raeumliche Hauptnavigation; Tabs als
   flache Zeilen mit saved/temporary-Sektionen; iPad mit persistenter breiter
   Baum-Sidebar und optionalem Zwei-Pane-Ziel.

Jedes Board zeigt mindestens: iPhone Website, Suche offen, Tab-/Workspace-
Uebersicht, Privat sowie iPad Sidebar. Gleiche Inhalte, Viewports und
Systemzustaende erlauben den direkten Vergleich. Bewerte danach Lesbarkeit,
Thumb-Reach, Website-Flaeche, Informationsdichte, Ahoi-Eigenstaendigkeit,
Dynamic Type, Reduced Transparency und Umsetzungsrisiko. Ohne ausdrueckliche
Nutzerwahl wird nur die konservativere, besser messbare Richtung fuer einen
kleinen P0-Slice umgesetzt; die andere bleibt versionierte Referenz.

## Verbindlicher Ausgangsstand

- Der fruehere Companion ist in `apps/AhoiMobile/` aufgegangen. Der aktuelle
  Kandidat ist ein echter nativer WebKit-Browser und enthaelt weiterhin lokale
  Persistenz, Suche, Workspaces/Baum, Geraete-Tabs, CKSyncEngine-Transport,
  verschluesselte Payloads sowie signierte Remote-Befehle.
- Vorhandensein im Quellcode ist kein Abschlussbeleg. Der vollstaendige
  Browser-, Sync-, Accessibility-, Performance- und Releasevertrag wird in
  dieser Welle gegen den aktuellen Kandidaten neu ausgefuehrt.
- Die Dokumentation nennt teilweise iOS/iPadOS 17+, waehrend SwiftPM und das
  generierte Xcode-Projekt iOS 26.0 konfigurieren. Das wird in dieser Welle
  bewusst und einheitlich auf **iOS 26.0 und iPadOS 26.0** festgelegt.
- Alle vorhandenen `IOS-01` bis `IOS-15` sind reale `ASSISTED_E2E`-Faelle und
  bleiben bis zur Ausfuehrung auf echter Hardware `NOT_RUN`.
- Es gibt zu Beginn weder ein releasefaehiges `.xcarchive`/`.ipa` noch
  TestFlight-Evidenz noch einen realen Mac-iPhone-CloudKit-Roundtrip.
- Die macOS-App bleibt ein Chromium-`//chrome`-Produkt. Der Mobile-Browser ist
  kein Chromium-Port und keine alternative Browser-Engine.

## Ergebnisdefinition

Die Umsetzung ist erst abgeschlossen, wenn:

1. `AhoiBrowser Mobile` als eigenstaendiges iPhone-/iPad-App-Target startet,
   Websites in der App rendert und normale sowie private Browsing-Sessions
   korrekt trennt;
2. Adress-/Suchfeld, Navigation, Tabs, Workspaces, Baum, Verlauf, Favoriten,
   Teilen, Downloads, Website-Suche, Wiederherstellung und iPad-Anpassung als
   zusammenhaengende User Journey funktionieren;
3. lokale Mobile-Tabs und Mobile-Verlauf ueber die bestehende Ahoi-Sync-Grenze
   publiziert werden, ohne Cookies, Zugangsdaten, Site Storage, Permissions,
   Downloads, Inkognito oder Keychain-Secrets zu synchronisieren;
4. die bisherigen Companion-Funktionen in die Browser-App integriert und nicht
   als zweite parallele App weitergefuehrt werden;
5. die App die Voraussetzungen fuer Apples Managed Default Browser Entitlement
   erfuellt und HTTP-/HTTPS-Links korrekt annimmt;
6. sichtbare User-Tests zuerst ausgefuehrt und dokumentiert wurden, danach die
   fokussierten und breiten programmatischen Gates;
7. ein signiertes Archive und eine installierte echte Geraeteversion verifiziert
   wurden; TestFlight/App-Store-Zustaende werden nur behauptet, wenn sie real
   vorliegen;
8. Dokumentation, Testregister, externe Gates, Datenschutz, Threat Model und
   Release-Status den tatsaechlichen Stand ohne Widerspruch beschreiben.

## Produktgrenze und Engine-Entscheidung

- Nutze auf iOS/iPadOS ausschliesslich das vom Betriebssystem gelieferte WebKit.
- Bevorzugte Schicht auf iOS 26 ist WebKit for SwiftUI mit `WebView` und
  `WebPage`. Wo ein Browservertrag damit nicht vollstaendig ausdrueckbar ist,
  darf eine schmale, testbare UIKit-Bridge um `WKWebView` verwendet werden.
- Erzeuge niemals einen eigenen Renderer, Cookie-Store, TLS-Stack,
  Zertifikatsspeicher, JavaScript-Interpreter oder Download-Netzwerkstack.
- Ein eigener Chromium-/Blink-Port, BrowserEngineKit oder ein alternatives
  Browser-Engine-Entitlement ist nicht Teil dieses Ziels.
- Workspaces teilen in normalem Browsing denselben persistenten
  `WKWebsiteDataStore`. Workspace-Wechsel duerfen Logins nicht trennen.
- Private Tabs verwenden ausschliesslich einen nicht persistenten Data Store,
  werden nie wiederhergestellt, nie indiziert und nie synchronisiert.

## Projekt- und Migrationsstruktur

- Migriere `apps/AhoiCompanion/` nach `apps/AhoiMobile/` und das App-Produkt zu
  `AhoiMobile` beziehungsweise dem sichtbaren Namen `AhoiBrowser`.
- Ein eigenstaendiger Core darf `AhoiMobileCore` heissen. Vorhandene Typen mit
  stabilem Wire-Vertrag muessen nicht kosmetisch umbenannt werden, wenn das
  Sync-Kompatibilitaet gefaehrdet.
- Bewahre die Lesbarkeit vorhandener lokaler Companion-Snapshots. Implementiere
  eine explizite, idempotente Migration mit Versionsmarker und Backup vor der
  ersten Mutation.
- Bestehende CloudKit-Record-Types, UUIDs, HLCs, Feldversionen, Tombstones und
  kanonische JSON-Bytes bleiben kompatibel.
- Entferne nach erfolgreicher Migration kein historisches Format, solange ein
  Upgrade von der letzten Companion-Version noch unterstuetzt werden muss.
- Es bleibt genau ein mobiles App-Produkt. Kein dauerhaftes paralleles
  Companion-Bundle und kein zweiter aktiver Datenbestand.

## Browser-Domaene

Fuehre eine vom synchronisierten Baum getrennte Browser-Laufzeit ein:

- `MobileTabID`: stabile UUID unabhaengig von WebKit-Objektidentitaeten;
- `MobileTabRecord`: Workspace, Parent/Position, aktuelle URL, Titel, Favicon,
  Erstellungs-/Aktivzeit, saved/temporary, normal/private, Restore-Metadaten und
  optionaler Snapshot-Verweis;
- `BrowserSessionController`: alleinige Autoritaet fuer aktive normale und
  private Tabs, Auswahl, Navigation, Wiederherstellung und Memory Pressure;
- `BrowserPageController`: genau eine WebKit-Seite samt beobachtbarem Zustand,
  Progress, Titel, URL, Lade-/Fehlerstatus, Back/Forward und Dialogen;
- `BrowserSessionStore`: atomare, versionierte Persistenz fuer normale
  Tab-Sessions. Private Sessions existieren nur im Speicher;
- `BrowsingHistoryWriter`: schreibt nur erlaubte Hauptframe-Navigationen in den
  lokalen Ahoi-Verlauf und die bestehende Sync-Outbox;
- `DownloadCoordinator`: uebergibt WebKit-Downloads an systemeigene APIs,
  praesentiert Fortschritt/Fehler und speichert keine Downloadinhalte in
  CloudKit;
- `BrowserPermissionCoordinator`: zeigt Kamera, Mikrofon, Standort,
  Zwischenablage, Popups und externe Schemes mit sichtbarem Ursprung und
  sicherem Default;
- `BrowserOpenURLRouter`: verarbeitet HTTP/HTTPS, Universal Links, Share Sheet,
  App-Deep-Links und Default-Browser-Aufrufe; unbekannte/custom Schemes werden
  nicht still ausgefuehrt.

Ein synchronisierter `RemoteTab` ist eine Sicht auf einen Tab eines anderen
Geraets. Er darf nicht als lokales WebKit-Laufzeitobjekt missbraucht werden.
Lokale normale Mobile-Tabs werden aus der Browser-Laufzeit in den bestehenden
Remote-Tab-Vertrag publiziert.

## Informationsarchitektur und Ahoi-UI

### iPhone

- Die Webseite ist die primaere Flaeche.
- Eine kompakte untere Navigationsleiste enthaelt Zurueck, eine kombinierte
  Adress-/Sucheinstiegsflaeche, Tab-/Workspace-Einstieg und Mehr.
- Tippen oder Herunterziehen auf die Adressflaeche oeffnet die zentrale
  Ahoi-Befehlssuche als eigenes, ruhiges Sheet mit gedimmtem Hintergrund.
- Die Suche priorisiert in dieser Reihenfolge: Eingabe als URL/Suche,
  offene lokale Tabs, Workspace-/Baumtreffer, Verlauf, Geraete-Tabs und
  Aktionen. Ergebnisse sind begrenzt, tastaturbedienbar und visuell eindeutig.
- Der Tab-Switcher gruppiert nach Workspace, trennt saved und temporary,
  bietet echte Close-/Undo-Aktionen und zeigt private Tabs nur im expliziten
  privaten Modus.
- Workspace-Wechsel sind mit horizontaler Geste moeglich, duerfen aber niemals
  die normale Back/Forward-Webgeste abfangen. Konflikte werden durch klare
  Startbereiche und Accessibility-Alternativen geloest.

### iPad

- Nutze eine kollabierbare Ahoi-Sidebar mit Workspaces, Baum, offenen Tabs und
  Geraete-Tabs sowie dieselben Flush-/Abstands-/Farbprinzipien wie Desktop.
- Unterstuetze Tastaturkuerzel und Pointer fuer neues Tab, Suche, Close,
  Workspace-Wechsel, Zurueck/Vor, Reload und Sidebar.
- Ein iPad-spezifischer Zwei-Pane-Modus ist erlaubt, wenn beide WebKit-Seiten
  sichtbar, fokussierbar und speichersicher bleiben. Drei-/Vier-Pane-Desktop-
  Layouts werden auf Mobile nicht imitiert; sie werden als klar dokumentierte
  Plattformaequivalenz behandelt.

### Visuelle Sprache

- Uebernimm Ahoi-Tokens fuer System/hell/dunkel, Akzent, Workspace-Farbe,
  Typografiehierarchie, Radien, Abstaende, Fokus und Fehlermeldungen.
- Liquid Glass/Material ist Dekoration, keine Layoutabhaengigkeit. Reduzierte
  Transparenz, erhoehter Kontrast, reduzierte Bewegung und Strom-/Memory-Druck
  erhalten einen vollstaendigen opaken Fallback.
- Nutze echte Favicons oder neutrale, konsistente Fallbacks; keine Emojis als
  Ersatz fuer Produkticons.
- Alle destruktiven Aktionen brauchen Undo oder eine passende Bestaetigung.

## Funktionsumfang der oeffentlichen v1

### Navigation und Seiten

- URL- und Suchbegrifferkennung mit konfigurierbarer Suchmaschine;
- HTTP und HTTPS, Back, Forward, Reload/Stop, Pull-to-Refresh;
- Fortschritt, Ladefehler, Offlinezustand und Retry;
- Link-Kontextmenue: neues Tab, neuer Workspace-Tab, privat oeffnen, kopieren,
  teilen, Leseliste/speichern;
- Seitensuche, Reader sofern WebKit sauber unterstuetzt, Desktop-/Mobile-Site,
  Zoom/Textskalierung und Share Sheet;
- JavaScript-Dialoge, neue Fenster/`target=_blank`, Datei-Upload,
  Medienwiedergabe, Fullscreen und systemeigene Auth-Challenges;
- Downloads mit Fortschritt, Abbruch, Teilen/Oeffnen und nachvollziehbaren
  Fehlern;
- externe App-Schemes nur nach sichtbarer User-Aktion und Bestaetigung.

### Tabs, Workspaces und Baum

- neues normales und privates Tab;
- temporare und gespeicherte Tabs;
- Tab umbenennen, speichern, verschieben, duplizieren, schliessen, Undo Close;
- Workspaces/Ordner/Seiten anlegen, umbenennen, sortieren, verschieben und
  tombstone-sicher loeschen;
- Session Restore normaler Tabs nach Prozessende/Crash;
- sichere Degradation, wenn eine Seite nicht restauriert werden kann;
- Memory-Pressure-Discard inaktiver Seiten mit URL/Titel/Snapshot-Erhalt und
  transparentem Reload bei Reaktivierung;
- keine Phantom-, Doppel- oder falschen Workspace-Tabs.

### Suche und Verlauf

- gemeinsame lokale Suche ueber Workspaces, Baum, lokale normale Tabs,
  erlaubten Verlauf und normale Geraete-Tabs;
- Suchdaten verlassen das Geraet nur als ausdrueckliche Navigation zur
  gewaehlten Suchmaschine;
- Verlauf anzeigen, durchsuchen, einzelne/zeitliche Eintraege loeschen und die
  konfigurierte Retention anwenden;
- private oder nicht erlaubte Navigationen gelangen nie in Index/Verlauf/Sync.

### Desktop- und Geraeteintegration

- alle bisherigen Companion-Funktionen bleiben innerhalb der Browser-App
  erreichbar;
- Link an konkreten Mac/Workspace senden;
- normalen Mac-Tab signiert oeffnen, fokussieren und nach Bestaetigung schliessen;
- normale Mac-/iPhone-/iPad-Tabs lokal filtern und im eigenen Mobile-Browser
  oeffnen;
- lokale Mobile-Tab-Aktivitaet wird mit richtigem Geraetetyp, Workspace und
  Freshness publiziert;
- keine Remote-Aktion auf veraltete, widerrufene, private oder ungueltige Tabs.

### Default Browser und Systemintegration

- Registriere HTTP-/HTTPS-Handling und alle von Apple geforderten
  Default-Browser-Contracts.
- Fordere das Managed Default Browser Entitlement fuer das finale Bundle an;
  ein Antrag ist kein erteiltes Entitlement.
- Verarbeite Kaltstart, Warmstart und bereits laufende Szene fuer externe
  HTTP-/HTTPS-URLs idempotent und ohne leeres Doppel-Tab.
- Unterstuetze mehrere iPad-Szenen nur mit einer expliziten Session-Autoritaet;
  andernfalls deaktiviere Mehrfenster ehrlich.
- Universal Links, Handoff/NSUserActivity, Share Extension und Spotlight sind
  optionale Folgefunktionen und duerfen v1 nicht destabilisieren.

## Datenschutz und Sicherheit

- Keine Produkttelemetrie, Usage-Pings oder automatischen Crash-Uploads.
- Kein Google-Browserkonto und kein Chrome Sync.
- Cookies, Passwoerter, Autofill, Site Storage, Permissions, Downloadinhalte,
  HTTP-Auth-Zugangsdaten, Header-Secrets, private Tabs und Extension Storage
  werden nie in Ahoi CloudKit geschrieben.
- CloudKit bleibt default-off, local-first, private database, custom zone und
  verschluesselte Payload.
- Accountwechsel, Zoneverlust, Keyverlust und physische Deletes bleiben
  fail-closed und benoetigen die vorhandenen ausdruecklichen Recovery-Gates.
- URL-Validierung lehnt Userinfo, unsichere Remote-Befehls-Schemes,
  `javascript:`, `data:`, `file:` und willkuerliche Custom Schemes an allen
  Sync-/Command-Grenzen ab.
- Private WebKit-Daten werden beim Verlassen des privaten Modus verworfen.
- Website-Ursprung ist in Permission-, Download-, Auth- und External-App-
  Dialogen immer sichtbar.
- Aktualisiere Threat Model, Privacy Policy, App Privacy Angaben und
  Datenflussdokumentation vor TestFlight.

## Barrierefreiheit, Sprache und Bedienung

- Vollstaendige VoiceOver-Namen, Werte, Hinweise, Rotor-Reihenfolge und
  dynamische Statusankuendigungen fuer Navigation, Ladefortschritt, Tabs,
  Workspace und Dialoge.
- Dynamic Type bis zu Accessibility-Groessen ohne abgeschnittene Kernaktionen.
- Mindest-Touchflaechen, Switch Control, Full Keyboard Access, Pointer und
  reduzierte Bewegung.
- Deutsche und englische Strings ausschliesslich ueber String Catalogs; keine
  produktionssichtbaren Hardcodes.
- URLs, Domains und sicherheitsrelevante Meldungen bleiben verstaendlich und
  werden nicht durch dekorative UI verdeckt.

## Performance- und Lebenszyklusbudgets

- Kaltstart mit lokalem Datensatz ohne CloudKit-Abhaengigkeit.
- Erste Browserflaeche darf nicht auf Sync, Favicon-Netzwerk oder Migration
  warten.
- Nur aktive/kurz zuvor verwendete Seiten bleiben live; bei Memory Pressure
  werden Hintergrundseiten kontrolliert freigegeben.
- Keine fuenfminuetigen Timer im Vordergrund, wenn CKSyncEngine bereits
  geeignete Ereignisse liefert; periodischer Fallback muss suspendierbar sein.
- Tab-Switch und Workspace-Wechsel bleiben bei realistischem Bestand flussig.
- Favicon-, Snapshot- und Suchindizes haben klare Groessen-/Retention-Limits.
- Miss Kaltstart, Warmstart, erste Navigation, Tab-Switch, Memory nach
  1/5/20 Tabs und Energie im Idle auf mindestens einem echten iPhone und iPad.

## Verbindliche Reihenfolge der Umsetzung und Verifikation

### Phase 0 - Baseline und sichere Migration

1. Dirty Worktree, Branch, Remotes und laufende Ahoi-Chromium-Builds pruefen.
2. In isolierter Branch/Worktree arbeiten und fremde Aenderungen erhalten.
3. Zielprompt, Status und Plattformziel iOS/iPadOS 26 vereinheitlichen.
4. Companion-Code inventarisieren und wiederverwendbare Core-/UI-Teile
   markieren.
5. App-/Core-Migration plus lokale Datenmigration implementieren.

### Phase 1 - kleinster sichtbarer Browser-Slice

Implementiere zuerst genau den vertikalen Weg:

`Start -> Adresse/Suche -> Webseite -> Zurueck/Vor/Reload -> neues Tab ->
Tab wechseln -> App beenden -> normales Tab wiederherstellen`.

Erst wenn dieser Slice kompiliert, wird er sichtbar getestet. Keine breite
Testsuite vor dieser sichtbaren Kontrolle.

### Phase 2 - User Tests zuerst

Fuehre mit frischem Profil zuerst sichtbare, reale Bedienpfade aus und bewahre
Screenshots/Video/Notizen mit Buildbindung:

- `MOB-USER-01`: Kaltstart, URL eingeben, reale HTTPS-Seite laden;
- `MOB-USER-02`: Suchbegriff, Ergebnis, Back/Forward/Reload;
- `MOB-USER-03`: drei Tabs anlegen, wechseln, schliessen, Undo, Restore;
- `MOB-USER-04`: Workspace wechseln und Seite speichern/verschieben;
- `MOB-USER-05`: privates Tab, normaler/private Modus visuell eindeutig,
  nach Schliessen keine Restore-/Verlaufsspur;
- `MOB-USER-06`: externe HTTP-/HTTPS-URL bei Kalt-/Warmstart;
- `MOB-USER-07`: Datei-Upload, Download, Teilen und `target=_blank`;
- `MOB-USER-08`: Permission- und External-App-Dialog mit sichtbarem Ursprung;
- `MOB-USER-09`: Rotation, Dynamic Type, VoiceOver und Reduced Motion;
- `MOB-USER-10`: iPad Sidebar, Tastatur, Pointer und optional Zwei-Pane;
- `MOB-USER-11`: Offline, Prozesskill, Memory Pressure und Restore;
- `MOB-USER-12`: Mac-/Mobile-Geraete-Tabs und Link an Mac;
- `MOB-USER-13`: unerlaubte Schemes/Remote-Aktionen werden erklaert abgewiesen;
- `MOB-USER-14`: 1/5/20 Tabs ohne Phantom-/Doppeltabs;
- `MOB-USER-15`: visuelle Ahoi-Konsistenz in hell/dunkel, hoher Kontrast und
  reduzierte Transparenz.

Simulator-Evidenz ist als `SIMULATOR_VISIBLE` zu kennzeichnen. `DEVICE_VISIBLE`
und `ASSISTED_E2E` erfordern eine frisch installierte, exakt gebundene App auf
echter Hardware.

### Phase 3 - fokussierte programmatische Tests

Erst nach dem ersten sichtbaren User-Pass:

- Unit-Tests fuer URL-/Suchrouting, Tabzustand, Sessionmigration,
  saved/temporary, private Filter, Restore, Reihenfolge und Undo;
- Tests fuer lokale History-/RemoteTab-Publikation und Ausschlussklassen;
- Tests fuer Kalt-/Warmstart-Deep-Links und Doppel-Tab-Vermeidung;
- UI-Tests fuer Navigation, Tab-Switcher, Workspace, private UI,
  Accessibility-Identifier und Fehlerdialoge;
- Golden-/Konvergenztests fuer unveraenderte CloudKit-Wire-Bytes;
- Test der Migration von realistischen Companion-Snapshots und no-op rerun;
- deterministische Fake-/Fixture-Seiten fuer Auth, Popup, Download, Upload,
  Media, Permission und Offline.

### Phase 4 - breite Gates

- kompletter Swift-Package-Testlauf;
- iPhone- und iPad-Simulator-Builds in Debug und Release;
- statische Analyse, Concurrency-Warnungen, String-/Privacy-/Entitlement-Audit;
- bestehende Repository-, CloudKit- und Companion/jetzt-Mobile-Gates;
- keine Chromium-Vollsuite fuer reine Mobile-Aenderungen, sofern keine
  Chromium-Sync-Grenze geaendert wurde;
- bei Ahoi-Chromium-Buildaktivitaet keine konkurrierende CPU-intensive Suite.

### Phase 5 - signierte Geraete- und CloudKit-E2E-Welle

- finale Bundle IDs, Team, CloudKit-Container, Keychain-Gruppen und Profile;
- signierter Development-/Ad-Hoc-Build auf echtem iPhone und iPad;
- reale Mac-Mobile-CloudKit-Online/Offline/Konflikt/Tombstone/Recovery-Welle;
- bisherige `IOS-01` bis `IOS-15` real ausfuehren und Evidenz binden;
- neue `MOB-USER-01` bis `MOB-USER-15` auf echter Hardware wiederholen;
- Default-Browser-Entitlement und Systemauswahl real verifizieren;
- Archive erzeugen, exportieren, installierte Bits gegen Archive hashen und
  Crash-/Log-/Privacy-Evidenz sichern.

### Phase 6 - TestFlight und Release

- App Store Connect Record, Signing, Privacy Manifest, App Privacy,
  Screenshots, Altersfreigabe, Export Compliance und Review Notes;
- interner TestFlight-Build mit exakter Buildnummer und Commitbindung;
- Upgrade von der letzten Companion-Version mit Backup/Migration/no-op proof;
- TestFlight-Journeys auf mindestens einem iPhone und iPad;
- verbleibende externe Apple-/Review-Gates ehrlich als Blocker dokumentieren;
- kein `RELEASE_PASS`, bevor Build, Archive, Upload, Processing, Installation
  und Journeys real belegt sind.

## Abnahmematrix

Nutze folgende Evidenzstufen strikt:

- `SOURCE_COMPLETE`: Quellcode und Dokumentation vorhanden;
- `LOCAL_BUILD_PASS`: lokaler Compile-/Link-Pass;
- `SIMULATOR_VISIBLE`: frisch installierter Simulatorbuild sichtbar geprueft;
- `UNIT_PASS` / `INTEGRATION_PASS`: exakt benannte Suiten mit Exit 0;
- `DEVICE_VISIBLE`: frisch installierter, buildgebundener Geraetepass;
- `ASSISTED_E2E_PASS`: reale Accounts, CloudKit, physische Aktion und
  anschliessende technische Verifikation;
- `ARCHIVE_PASS`: `.xcarchive`/Export samt Signatur, Entitlements und Hash;
- `TESTFLIGHT_PASS`: verarbeiteter Build real ueber TestFlight installiert;
- `RELEASE_PASS`: alle releasekritischen Gates einschliesslich Default Browser,
  Datenschutz und Upgrade erfuellt.

Keine Stufe impliziert automatisch die naechste.

## Commit-, Integrations- und Liefervertrag

- Kleine, thematisch fokussierte Commits mit Tests/Evidenzhinweisen.
- Keine fremden Dirty-Worktree-Aenderungen committen oder verwerfen.
- Branch und Remote-SHA vor Merge/Push vergleichen.
- Nach jeder CPU-intensiven Phase erneut auf Ahoi-Chromium-Buildaktivitaet
  pruefen und eigene Prozesse bei neuem Ahoi-Build gezielt pausieren/beenden.
- GitHub-hosted Actions mit Billing-/Zero-step-Grenze nicht verwenden oder
  endlos wiederholen; lokale Gates und vorhandene Self-hosted Runner bevorzugen.
- Implementierung, Tests, Dokumentation und Evidenz gemeinsam pushen.
- Am Ende exakt berichten: umgesetzt, lokal gebaut, sichtbar im Simulator,
  sichtbar auf Geraet, CloudKit-E2E, Archive, TestFlight, Release sowie jeder
  verbleibende externe/manual Gate.

## Definition of Done

`AhoiBrowser Mobile` ist nicht deshalb fertig, weil eine WebView sichtbar ist.
Fertig bedeutet eine alltagstaugliche native Browser-App mit koharenter
Ahoi-Bedienung, sicherem normalen/privaten Lebenszyklus, belastbarer
Tab-/Workspace-Wiederherstellung, bestehender verschluesselter
Mac-Mobile-Integration, sichtbarer User-Abnahme vor den automatisierten Gates,
signierter Hardware-Evidenz und ehrlich getrenntem TestFlight-/Release-Status.
