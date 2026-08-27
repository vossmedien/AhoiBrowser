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

## Verbindlicher Ausgangsstand

- Der vorhandene Companion unter `apps/AhoiCompanion/` umfasst einen nativen
  SwiftUI-Shell, lokale Persistenz, lokale Volltextsuche, Workspaces und
  Baumknoten, synchronisierte normale Tabs und Verlauf, CKSyncEngine-Transport,
  verschluesselte Payloads sowie signierte Remote-Befehle.
- Er ist heute **kein Browser**. Links werden mit `openURL` an den gewaehlten
  Standardbrowser uebergeben; es gibt keine WebView, keine lokale
  Browser-Session, keine Tab-Laufzeit und keinen eigenen Cookie-/Download- oder
  Permission-Lebenszyklus.
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
