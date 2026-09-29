# Konsolidierter Zielprompt: AhoiBrowser Mobile Abschlusskapsel

> Stand: 30. August 2026. Dieser Prompt konsolidiert die bisher getrennt
> behandelten AhoiCompanion-, iOS-/iPadOS-Browser-, CloudKit- und Apple-
> Release-Arbeiten. Er ersetzt keine Anforderung aus
> `outputs/AhoiBrowser-Mobile-Zielprompt.md`, sondern legt deren eindeutige
> Ausfuehrungs-, Ownership- und Integrationsgrenze fuer diese Session fest.

## Rollenauftrag

Arbeite im kanonischen AhoiBrowser-Repository den bestehenden AhoiCompanion
vollstaendig zu **AhoiBrowser Mobile fuer iOS/iPadOS 26** aus. Nutze SwiftUI,
Swift 6 mit vollstaendiger Strict Concurrency und ausschliesslich Apples
systemgeliefertes WebKit. Schließe alle kontrollierbaren Produkt-, Sync-,
Security-, Accessibility-, Signing-, Test- und Release-Arbeiten selbststaendig
ab. Eine vorhandene Code-Naht, ein Build, ein Simulatorbild, ein Upload oder ein
Apple-Antrag ist immer nur die exakt benannte Evidenzstufe.

Die vollstaendige fachliche Spezifikation, End-to-End-Matrix und Definition of
Done bleiben in `outputs/AhoiBrowser-Mobile-Zielprompt.md` autoritativ. Bei
Widerspruch gilt jener Vollprompt; diese Kapsel entscheidet nur ueber
Arbeitsaufteilung, Reihenfolge und kontrollierte Integration.

## Gemeinsamer Branch, disjunkte Session-Zustaendigkeit

Mobile- und Desktop-Session arbeiten parallel im selben kanonischen Repository
und auf demselben Integrationsbranch
`codex/desktop-core-feature-wave-20260830`. Der gemeinsame Dirty-Arbeitsstand
ist laufende, zu bewahrende Produktarbeit und darf weder pauschal
zurueckgesetzt noch versteckt oder durch einen frischen Checkout ersetzt
werden. Ein separater Mobile-Branch oder dauerhafter Mobile-Worktree ist nicht
Teil dieses Vertrags.

Diese Session verantwortet darin ausschliesslich den Mobile-Abschluss. Die
parallele Desktop-Session verantwortet insbesondere:

- Chromium-/macOS-Browser-Chrome und Sidebar/Splits;
- Arc- und Zen-Import;
- uBlock Origin Classic/Lite und Extension-UI;
- Desktop-Onboarding beziehungsweise den normalen Browser-Importdialog;
- Desktop-spezifische E2E-Fixtures, Patches und installierte Runtime-Evidenz.

Diese Mobile-Session veraendert keine dieser Desktop-Flaechen. Beide Sessions
koexistieren ueber disjunkte Pfad- und Prozessverantwortung im selben
Arbeitsstand. Es wird kein zweiter Desktop-Build gestartet und kein Prozess
der Desktop-Session beendet, repriorisiert oder ueberschrieben.

## Mobile-eigene Pfade

Primaerer Schreibbereich dieser Session:

- `apps/AhoiMobile/**`;
- `outputs/AhoiBrowser-Mobile-*.md`;
- `docs/IOS_BROWSER.md` und `docs/IOS_BROWSER_E2E_EVIDENCE.md`;
- Mobile-Design- und Mobile-Evidenzartefakte;
- `tools/mobile_evidence_artifacts.py` und
  `tools/verify_mobile_release_evidence.py`;
- Mobile-spezifische Repository-Tests und Apple-Signing-Konfigurationen.

Fuer den real verlangten Mac-iPhone-iPad-Sync darf diese Session ausserdem nur
die schmale, Mobile-abhaengige Mac-Gegenpartgrenze bearbeiten:

- Ahoi-CloudKit-Entitlement-/xcconfig-Templates im Sync-Overlay;
- exakte Mobile-/Mac-Keychain-Gruppen- und Container-Validatoren;
- fail-closed Release-/Signing-Helfer, soweit sie fuer die fuenf Mobile-Modi
  und den Mac-CloudKit-Gegenpart unvermeidbar sind.

## Gemeinsame Dateien und Merge-Protokoll

Folgende Dateien koennen logisch beide Produktwellen beruehren und werden
deshalb nicht nebenlaeufig blind zusammengefuehrt:

- `config/external-gates.json`;
- `config/product.json`;
- `config/macos-entitlements.json`;
- `docs/SYNC.md` und `docs/RELEASING.md`;
- `tools/release/cli.py`, `tools/release/signing.py` und
  `tools/verify_macos_entitlements.py`;
- `overlay/chromium/src/ahoi/browser/sync/**`;
- Desktop-Settings-Vertragstests, die den Mobile-Sync-Opt-in spiegeln.

Regeln fuer diese Schnittmenge:

1. Vor jeder weiteren Aenderung aktuelle Dirty-Mengen und Remote-SHAs lesen.
2. Mobile bearbeitet nur Mobile-/CloudKit-relevante Abschnitte; Desktop nur
   seine Browser-/Import-/Extension-Abschnitte.
3. Keine fremde Aenderung verwerfen, zuruecksetzen oder pauschal stage-en.
4. Commits enthalten explizite Pfadlisten und `git commit -s`.
5. Vor einem fokussierten Commit `HEAD`, Index und aktuelle Dirty-Mengen erneut
   lesen, damit parallel eingetroffene Aenderungen nicht versehentlich
   mitgestaged oder ausgelassen werden.
6. Arbeit im selben physischen Worktree ist beabsichtigt. Eine Session wird nur
   angehalten, wenn sich konkrete Schreibpfade oder laufende Schreibprozesse
   ueberschneiden; eine bloss gemeinsame Branch- oder Worktree-Nutzung ist kein
   Abbruchgrund.

Die aus der Umschaltphase stammende Aenderung an
`tests/repository/test_ahoi_settings_page_contract.py` bleibt erhalten, wird
aber bis zur bewussten Shared-Contract-Integration nicht ungeprueft einem
Mobile-Commit zugeschlagen.

Der kanonische Mobile-Produktstand ist in
`88e9b12e629aaad4e69a590f530754b983d38774` fokussiert festgehalten. Der exakt
bewahrte Stand des frueheren Mobile-Worktrees liegt auf
`archive/ahoi-mobile-pre-consolidation-20260830` in
`7bd492fa9d669868dceb1dbd91450cae5ae3bd3a`. Seine Historie wird mit einem
signierten `ours`-Merge verbunden, ohne den neueren kanonischen Mobile-Baum zu
ersetzen; vor und nach dem Merge muessen Baumidentitaet und Ancestry
maschinenlesbar geprueft werden.

Die vor dieser History-Integration erreichte Entwicklungsevidenz umfasst die
breite iPhone-Matrix mit dokumentiertem Harness-Nachlauf, korrigierte
Revocation- und Reduce-Motion-Paesse, iPad 3/3, Mobile Core 111/113 mit zwei
echten Entitlement-Skips, fokussierten Core 49/49, CloudKit/Security 36/36,
Mobile-Repository 20/20, Swift-Parse 94/94, erfolgreichen Analyzer und den
zweifach identischen XcodeGen-Hash
`49e503d5da232d2067094999b653096c9e5b923f7a8b8ebd561273a7f7f42643`.
Diese Werte bleiben Entwicklungsevidenz; der nach dem Merge entstehende exakte
Commit muss erneut sichtbar und danach programmatisch geprueft werden.

## Konsolidiertes Produktziel

1. **Harbor Deck** bildet die ruhige, content-first iPhone-Browseroberflaeche
   mit sichtbarem Ursprung, sicherer Navigation, Tabs und gruppierten Aktionen.
2. **Focus Voyage** bildet den normalen und eindeutig privaten New-Tab-/Such-
   Zustand ohne Datenleck aus privaten Tabs.
3. **Workspace Canvas** bildet die adaptive iPad-Regular-Width-Oberflaeche mit
   Sidebar, Baum, Tabs, Suche, Tastatur und Pointer.
4. Normale/private Tabs, Workspaces, Verlauf, Downloads, Uploads, Dialoge,
   Permissions, externe Schemes, Share Sheet, Restore, Offline- und
   Memory-Pressure-Recovery funktionieren als zusammenhaengende Journey.
5. AhoiCompanion-Daten werden versioniert, atomar, mit Backup, Readback und
   idempotentem Zweitlauf zu AhoiBrowser Mobile migriert.
6. Local-first-Sync bleibt opt-in und verschluesselt. Private Daten, Cookies,
   Credentials, Autofill, Website-Storage, Permissions, Downloads und
   Keychain-Secrets werden konstruktiv ausgeschlossen.
7. CKSyncEngine, AES-256-GCM-Payloads, atomarer First-Key-Bootstrap,
   Ed25519-Remote-Commands, Rotation, Widerruf, Account-/Zone-/Key-Recovery und
   Replay-Schutz werden fail-closed umgesetzt und real nachgewiesen.
8. Die fuenf Modi `DebugLocal`, `CloudKitDevelopment`,
   `TestFlightBootstrap`, `DefaultBrowserDevelopment` und `ReleasePostGrant`
   bleiben semantisch getrennt; kein Bootstrap-Archive wird nachtraeglich
   umetikettiert.
9. Alle Ahoi-eigenen Quellen bleiben bei maximal 800 physischen Zeilen und
   folgen den Repository-, DCO-, Secret-, Localization-, Concurrency- und
   Accessibility-Vertraegen.
10. Apple-, CloudKit-, TestFlight- und Default-Browser-Schritte werden bis zur
    real vorhandenen Berechtigungsgrenze abgeschlossen; externe oder
    menschliche Gates werden nicht als Pass etikettiert.

## Bewusste Nicht-Ziele dieser Session

- kein ueberdimensionierter Onboarding-Wizard;
- kein Desktop-Arc-/Zen-Importer und keine Browser-Extension-Arbeit;
- kein Chromium/Blink auf iOS und kein alternativer Netzwerk-/TLS-Stack;
- kein Arc-Klon, keine fremden Assets und kein verpflichtender AI-Dienst;
- kein breiter Ad-/Tracker-/Cookie-Banner-Blocker in dieser Mobile-Welle;
- keine erfundenen Apple-, Rechts-, Trader-, Export- oder Review-Angaben;
- kein oeffentlicher App-Store-Release ohne separate ausdrueckliche Freigabe.

## Verbindliche Reihenfolge

### 1. Laufzeit zuerst sichtbar

- Aus dem jeweiligen aktuellen Mobile-Stand einen exakten `DebugLocal`-Kandidaten
  bauen, auf dedizierten Ahoi-iPhone-/iPad-Simulatoren installieren und kalt
  starten.
- Vor neuen programmatischen Testlaeufen die Kernjourney sichtbar bedienen:
  URL/Suche, HTTPS, Zurueck/Vor/Reload, neuer Tab, Tabwechsel, Close/Undo,
  Privat, Prozessende/Restore sowie iPad-Sidebar/Rotation.
- Jeder sichtbare Fehler ist ein Befund, kein kosmetisch zu ueberspringender
  Testfehler. Ursache beheben und denselben Schritt erneut sichtbar pruefen.

### 2. Fokussierte Gates

- Erst nach der sichtbaren Journey Swift-Compile, Strict-Concurrency,
  fokussierte Unit-/UI-/Signing-/Evidence-Tests und XcodeGen-No-op pruefen.
- Provider-freie Tests duerfen entitlementgebundene CloudKit-Faelle nur
  explizit als Skip ausweisen; signierte Targets muessen sie spaeter real
  ausfuehren.
- Ist ein sichtbarer E2E-Pfad durch Hardware, Entitlements, Provisioning,
  Accounts oder einen externen Dienst tatsaechlich blockiert, werden Blocker
  und letzte erreichbare Grenze exakt dokumentiert. Alle davon unabhaengigen
  programmatischen Tests laufen trotzdem; sie machen den blockierten E2E-Pfad
  nicht gruen.
- Regressionen sofort auf den kleinsten reproduzierbaren Vertrag reduzieren,
  beheben und erneut sichtbar beziehungsweise programmatisch beweisen.

### 3. Breite lokale Gates

- `git diff --check`, Source-Line-Budget, DCO, Secret Scan,
  `./scripts/test-repository.sh`, Swift-Pakete, Simulator Debug/Release und
  `xcodebuild analyze` ausfuehren.
- GitHub-hosted Jobs mit null ausgefuehrten Schritten wegen Billing bleiben
  unavailable infrastructure und werden nicht als gruen bezeichnet.

### 4. Signierte Apple- und CloudKit-Stufen

- Live-Zustand von Team `248AJ5BN47`, Bundle
  `app.ahoibrowser.AhoiBrowser`, App-ID-Prefix, Capabilities, Profilen und
  dediziertem Container `iCloud.app.ahoibrowser.AhoiBrowser` erneut lesen.
- Vor Container/App-ID-Zuordnung, App-Store-Connect-App-Datensatz,
  Production-Schema-Promotion, externer Beta/oeffentlichem TestFlight-Link,
  Managed-Entitlement-Antrag und oeffentlichem Release jeweils eine eigene
  aktionsnahe Bestaetigung einholen.
- DisplayPilot-Container, Profile und Schluessel niemals wiederverwenden.
- Development und Production jeweils mit installierten, signierten Bits und
  kandidatgebundener Mac-iPhone-iPad-/Privacy-Evidenz pruefen.

### 5. Commit, Integration und Wahrheit

- Mobile-Arbeit nach Verantwortlichkeit in kleine DCO-Commits teilen.
- Keine Desktop-Dirty-Dateien pauschal stage-en.
- Vor Push Remote-SHA lesen, Upstream integrieren und Shared-Dateien semantisch
  mit der Desktop-Welle vereinigen.
- Pushen und am Ende eine Wahrheitsmatrix fuer Source, Build, Simulator,
  physisches iPhone/iPad, CloudKit Development/Production, Archive,
  TestFlight, Default-Browser-Antrag/Grant/E2E und Release liefern.

## Definition of Done

Erledigt ist diese Kapsel erst, wenn die vollstaendige Definition of Done aus
`outputs/AhoiBrowser-Mobile-Zielprompt.md` fuer denselben finalen Kandidaten
nachgewiesen ist. Solange Apple-Grant, kompatibles physisches iPad,
CloudKit-Container, TestFlight oder menschliche Angaben fehlen, bleiben diese
Stufen offen; alle lokal und mit vorhandenen Berechtigungen kontrollierbaren
Arbeiten werden dennoch bis zum belastbaren, gepushten Stand abgeschlossen.
