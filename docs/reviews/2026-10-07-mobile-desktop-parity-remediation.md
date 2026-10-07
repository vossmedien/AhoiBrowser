# Mobile-Desktop-Parität: begrenzte Korrektur F01–F03

Stand: 7. Oktober 2026. Handoff `AHOI-DM-REMEDIATION-20261007-01`,
Delegation `8e74e15d`. Vertrag: [Migrationsplan, Revision 1](2026-10-07-desktop-mobile-migrationsplan.md),
DM-T00 → DM-T01/02 → DM-T03 → DM-T08. Komfortfunktionen DM-T05–07,
Popup-/Permissions-Neuvertrag und Extension Step 2 sind nicht beauftragt.

**Aktueller Stand:** Start und Sourcebefunde belegt; keine Produktkorrektur,
kein ausführbarer Kandidat, keine sichtbare Abnahme oder Lieferung.
Überlappende Sourcearbeit wartet auf konkrete Owner-Handbacks. Dieser Bericht
ist der ausdrücklich eigene, disjunkte Dokumentpfad; der bestehende
[Mobile-Checkpoint](../ACTIVE_MOBILE_CHECKPOINT.md) bleibt beim Masterowner.

## Identität und Startquittung

| Grenze | Tatsächlicher Beleg |
| --- | --- |
| Ursprung / Ergebnisempfänger | Kachel `7AEA6E2A-E94E-4DC8-AE31-D52F6B4B0DF2`, Thread `01a11669-db56-7243-b2bc-fee9fb1fa2e2` |
| Neue sichtbare Zielkachel | `AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A`; `cockpit_sessions(cwd:)` erkennt sie als eigenen Worker für Delegation `8e74e15d` |
| Neuer nativer Thread | `01a11680-b5c7-70e2-a086-1402477603f8`, durch `CODEX_THREAD_ID`, native `session_meta` und Goal-Receipt belegt; Cockpit-Sessionliste veröffentlicht noch keine eigene `source_thread_id` |
| Eigenes Goal | Erstes `get_goal` → `null`; danach genau ein `create_goal` mit dem unveränderten Auftragsziel, Status `active`, `createdAt=1791379249`. API liefert keinen separaten `goalId`: Identität ist Thread + Anlagezeit. Fremde Goals unverändert. |
| Goaltext | `AHOI-MOBILE-DESKTOP-PARITY-20261007: Behebe die im Migrationsplan DM-F01–03 sourcebelegten bestehenden Integrationslücken für Preview-Sitzungskontext, Workspace-Merge mit Split/Archiv und namespacegebundene isolated-Nutzung; liefere verifizierte attribuierbare Änderungen über die bestehende main-/Mobile-Lieferung, ohne fremde Goals oder Popup-Aufträge zu ersetzen.` |
| Kanonisches Repository | `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`, bestätigt durch absolutes `git rev-parse --git-common-dir` und `git worktree list --porcelain` |
| Eigener CWD / Worktree | `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-mobile-workspace-korrektur-8e74e15d`; beim Start sauber, keine zweite Projektkopie |
| Eigener Branch / Basis | `cockpit/mobile-workspace-korrektur-8e74e15d` / `c3a1d1362b6cadffa522f6d39ca03a0a47438c8b` |
| Angewendete Ausführung | Anbieter `openai`, Modell **`gpt-6-astra`**, Effort **`xhigh`**, CLI `0.160.1`, `codex-tui`, Approval `never`; native `session_meta`/`turn_context` und Cockpit stimmen überein. Sol/xhigh ist nur die Empfehlung. |
| Tatsächlicher lokaler Account | `CODEX_HOME=/Users/vossmedien/Library/Application Support/Terminal Cockpit/Accounts/DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5/CodexHome`; eigener Eintrag in `Session Insight/sessions.json` bestätigt dieselbe Account-ID und Modell/CWD. Keine Credentials gelesen, keine Konto-/Routing-/Reserve-/Modelländerung. |
| Nativer Metadatenbeleg | Unter diesem CODEX_HOME: `sessions/2026/10/07/rollout-2026-10-07T15-14-56-01a11680-b5c7-70e2-a086-1402477603f8.jsonl`; nur ausgewählte Metadatenfelder gelesen, kein kopierter Verlauf |
| Startmeldung an Ursprung | `cockpit_note` quittiert `7845f202`, Ergänzung zu Account/Modell/Transport `6f0f5026`; Annahme beim Cockpit, keine behauptete fachliche Abnahme |
| Routing-/Jev-Grenze | Tatsächlich angewendete Konfiguration belegt; eigener Jev-Entscheidungsbeleg noch nicht zugänglich. Bestehenden Startbeleg beim Ursprung angefragt, keine Neuroutung. |
| Direkter Orchestratortransport | Request `22d97bfb-f254-46b6-886d-75be03e56a95`, gebunden an obige Zielkachel/Thread/CWD: `SOURCE_UNAVAILABLE: current MCP consumer and valid source binding required`; Status danach `REQUEST_NOT_FOUND`. Keine Zustellung, keine blinde Wiederholung. |

Gelesen: tatsächlich vorhandenes Root-`AGENTS.md` (keine näheren Dateien unter
`apps`, `docs`, `config`), die übergebenen globalen Regeln, README, relevante
Checkpoint-/Planabschnitte, ADR 0012, Lane-Datei, BUILDING und DCO-Regeln.
Kommunikation folgt dem Skill `terminal-cockpit-communication`; bei der
Transportgrenze wurde dessen ADR 0035 gelesen. Es wurde keine Einstellung geändert.

## Aufgabenplan und Ownergrenzen

Eine native Plan-API wird dieser Sitzung nicht angeboten. Der explizite
Dokumentanschluss des Skills `project-feature-workflow` führt die verlangte
Aufgabenliste hier. Tatsächlicher Cockpitimport/Verknüpfung ist noch nicht belegt;
nach Skillvertrag ist das eine ausdrückliche App-Aktion, kein automatischer Watcher.

<!-- cockpit-workflow:v1 -->
{
  "schemaVersion": 1,
  "contractID": "AHOI-DM-REMEDIATION-20261007-01",
  "revision": "r1",
  "kind": "code",
  "criteria": [
    {"id": "DM-AC-01", "title": "Vorhandene Mac-Page-Empfangskorrektur attribuierbar erhalten."},
    {"id": "DM-AC-02", "title": "Vorhandene Sync-Coalescing-/Suchkorrekturen erhalten."},
    {"id": "DM-AC-03", "title": "Preview, Retry und Adoption erhalten Storeprovenienz; Private bleibt nonpersistent."},
    {"id": "DM-AC-04", "title": "Veraltete Callbacks bleiben wirkungslos; Downloads, Dialoge, Permissions und Attachment behalten Provenienz."},
    {"id": "DM-AC-05", "title": "Merge erhält Split-/Archiv-IDs, eingebettete Zuordnung, Reihenfolge, Ratios und Topologie."},
    {"id": "DM-AC-06", "title": "Persistenz und Undo sind atomar; veraltetes Undo wird vollständig verweigert."},
    {"id": "DM-AC-07", "title": "Späte Peerrecords, Undo und Replay konvergieren gemäß bestehendem Konfliktvertrag."},
    {"id": "DM-AC-08", "title": "Isolierte Baum-/History-/Öffnen-/Ändern-Reisen verwenden eigenes Repository, Outbox, Namespace und Store."},
    {"id": "DM-AC-09", "title": "Keyverlust, Accountwechsel und Sync aus erlauben keinen Rückfall ins Hauptnamespace."},
    {"id": "DM-AC-15", "title": "Vorhandene Flick-/Fokuskorrekturen attribuieren; offene Hardware-/VoiceOver-Grenzen erhalten."},
    {"id": "DM-RECEIPT", "title": "Eigene Session/Thread/CWD/Basis/Goal/Routing und konkrete Owner-Handbacks sind belegt."},
    {"id": "DM-DELIVERY", "title": "Exakter Kandidat, sichtbare Reisen, Grenzchecks, natives Review, Root-Abnahme und main-/Mobilelieferung sind belegt."}
  ],
  "tasks": [
    {"id": "DM-T00", "title": "Start und attributable Mobilebaseline mit Master abstimmen", "criterionIDs": ["DM-RECEIPT", "DM-AC-01", "DM-AC-02", "DM-AC-15"], "dependencies": [], "status": "waiting", "ownerSessionID": "AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A", "projectPath": "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser"},
    {"id": "DM-T01", "title": "Preview-/Page-Provenienz korrigieren", "criterionIDs": ["DM-AC-03", "DM-AC-04"], "dependencies": ["DM-T00"], "status": "waiting", "ownerSessionID": "AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A", "projectPath": "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser"},
    {"id": "DM-T02", "title": "Merge/Receipt/Undo/Persist/Outbox um Split und Archiv vervollständigen", "criterionIDs": ["DM-AC-05", "DM-AC-06", "DM-AC-07"], "dependencies": ["DM-T00"], "status": "waiting", "ownerSessionID": "AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A", "projectPath": "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser"},
    {"id": "DM-T03", "title": "Getrennte Snapshots durch UI/History/Writer führen", "criterionIDs": ["DM-AC-03", "DM-AC-08", "DM-AC-09"], "dependencies": ["DM-T01", "DM-T02"], "status": "waiting", "ownerSessionID": "AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A", "projectPath": "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser"},
    {"id": "DM-T08", "title": "Exakten Kandidaten sichtbar prüfen, reviewen und ownergebunden liefern", "criterionIDs": ["DM-AC-03", "DM-AC-04", "DM-AC-05", "DM-AC-06", "DM-AC-07", "DM-AC-08", "DM-AC-09", "DM-DELIVERY"], "dependencies": ["DM-T03"], "status": "waiting", "ownerSessionID": "AE918FA0-5194-43DA-BDCE-A6ADBA4AE81A", "projectPath": "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser"}
  ],
  "evidence": [
    {"stage": "briefing", "reference": "docs/reviews/2026-10-07-mobile-desktop-parity-remediation.md#identität-und-startquittung", "revision": "r1", "environment": "Eigener Worktree; openai/gpt-6-astra/xhigh", "summary": "Native Start-/Goalbindung belegt; Jev-Entscheidungsbeleg und konkrete Source-Handbacks offen."},
    {"stage": "documentation", "reference": "docs/reviews/2026-10-07-mobile-desktop-parity-remediation.md#befunde-am-gemeinsamen-aufrufpfad", "revision": "c3a1d1362b6cadffa522f6d39ca03a0a47438c8b", "summary": "F01–F03 sourcebelegt; zentrale Bytegleichheit gegenüber main134/Feature849 geprüft. Keine Produktabnahme."}
  ]
}
<!-- /cockpit-workflow -->

| Schritt | Stand | Nächster konkreter Schritt / zuständiger Owner |
| --- | --- | --- |
| DM-T00: Identität, Pins, vorhandene Fixes | Sourceprüfung vorbereitet; Ownerabstimmung offen | Attributive Baseline mit Master abstimmen; kein ganzer Featurebranch-Merge |
| DM-T01: Preview-Provenienz | F01 bestätigt; Schreiben geparkt | Mobile55 gibt konkreten Lifecycle-/Preview-Umfang und verbindliche Basis zurück |
| DM-T02: Merge, Receipt, Undo, Outbox | F02 und vorhandene Schemafelder bestätigt; Schreiben geparkt | Master/Sync bestätigt Sourceumfang und Vertrag für späte Split-/Archivrecords |
| DM-T03: getrennte Inhalte und Writes | F03 bestätigt; Schreiben geparkt | Nach T01/T02: Master/Sync gibt Model-/Namespace-/Writerumfang zurück |
| Kandidat, sichtbare Reisen, Grenzchecks | Offen, nicht gestartet | Sourcehandback; danach tatsächliche Build-/DerivedData-/Gerätelease, frische CPU-/Speicher-/Diskaufnahme |
| Scoped natives Review | Offen, 0 Runden | Erst nach betroffenen sichtbaren Reisen und fokussierten Checks; höchstens zwei Runden gemäß Auftrag |
| Root-Abnahme, main, Push, Mobilelieferung | Offen, keine Lease | Expliziter passender Handback, attributable Integration und bestehende bewachte Lieferung |
| Checkpoint / Schlussreceipt | Offen | Konkreten Eintrag an Master liefern oder dokumentbezogenen Handback erhalten; Schlussmeldung erst nach akzeptierter Delivery |

Masterowner: `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread
`01a11179-cb6b-7dc1-9e53-f3d72fefc198`. Mobile55:
`28777292-5A1A-4E05-97FD-1F4A7C0C9FB9`, Thread
`01a11356-7ffe-7ac2-a7a8-dc757416ca60`.

- Source-/Dokument-Handback Master angefragt, Cockpitquittung `ff2c7601`.
- Lifecycle-/Preview-Handback Mobile55 angefragt, Cockpitquittung `36d87a71`.
- Konkrete zusätzliche Rehome-/Writerseams an Master, Quittung `6b308758`.
- Weder Anfrage noch Prozessabwesenheit überträgt Ownership. Bis zur Antwort
  nur Read-only Sourceanalyse und dieser Bericht. Kein R71/DD95/A058-,
  Chromium-, MacGUI-, main- oder Releasebesitz.
- Fremde Änderungen im Hauptcheckout und sämtliche fremden Worktrees,
  Prozesse, Kandidaten und Artefakte bleiben unangetastet.

## DM-T00: vollständige Sourcepins und Anwendbarkeit

| Pin | Vollständige Revision / Beobachtung |
| --- | --- |
| Übergebenes main | `13423f52ced6744842dfd2fea16f9aadda1e5f3f` |
| Übergebenes Feature | `8493d89ce724b2a9016dcd9251ccaf5beb71d334` |
| Tatsächliches main / Remote | `0f3f016a732641dfdc7a1c75ab5243a76b4f6bf2`, per `git ls-remote origin refs/heads/main` bestätigt; jüngere Planungslieferung, kein F01–F03-Fix |
| Tatsächliches Feature / Remote / eigene Basis | `c3a1d1362b6cadffa522f6d39ca03a0a47438c8b`, per `git ls-remote` bestätigt |
| Bestehendes Remote | `https://github.com/vossmedien/AhoiBrowser.git` |
| Gelesener Migrationsplan | Vertragsrevision 1; SHA-256 `22c1254fe0464300c5f4eca523ab76edacab68e8b9b4eb39a6dcbb253e52183b` |

Folgende Blobs sind auf **main134, Feature849 und eigener Basisc3a bytegleich**:

| Datei unter `apps/AhoiMobile/Sources/AhoiMobileCore/` | Git-Blob |
| --- | --- |
| `MobileLinkPreview.swift` | `f063cfed5b9a5db1190a8d9baac8d93bba14b6df` |
| `MobileBrowserControllerWebPageLifecycle.swift` | `6569a98354f7ac5bfd95ccf02661f0d0c4e6520d` |
| `CompanionStoreWorkspaceMerge.swift` | `b092594562e3082c10850a601f6cfd8cf193fb5d` |
| `CompanionAppModelWorkspaceMerge.swift` | `4b91350ca954911710ad2b643bc3d6f271207867` |
| `SeparatedWorkspaceDataStores.swift` | `b13bd2225b9d243e8bf1e1dbc327f231f8e7e499` |
| `SeparatedWorkspaceSync.swift` | `c333f0047f802ae0968f8b8ba075ff79df4284c6` |
| `MobileBrowserSidebar.swift` | `7e19e3181757381422ea3d02730fe0dfc0ab2c86` |

`CompanionViews.swift` und `AhoiMobileBrowserView.swift` sind insgesamt nicht
bytegleich: der Featurestand ergänzt nur `.listRowSeparator(.hidden)` bzw.
den Flick-Preview-Callback. Die gemeldeten F03-Stellen bleiben identisch.
Kein Schluss von zentraler Bytegleichheit auf Gleichheit der gesamten Mobile-App.
`git diff --stat 13423f52..main` zeigt ausschließlich den Migrationsplan.

| Vorhandene Korrektur | Herkunft / Abhängigkeit / Integration |
| --- | --- |
| `f36ca9dfa64031daac7cb410230716efa32e982c` | Mac-Page-Envelope ohne orderKey; Bridge, Snapshot-Bridge und bestehende Wiretests. In eigener Basis enthalten, kein Vorfahr von main. |
| `7a17267692b3a8c60f4d56387d9583344208e5a9` | EventDriven-Coalescer, BoundedPass-Gate, Provider, AppModel und Projekt-/Testwiring; zehn Pfade einschließlich fremdem Sync-Checkpoint. In eigener Basis, nicht main. |
| `39503e080c89eac5fda74ac832467c6010d508eb` | Fremde Subscriptionbindung, Provider/Helper/Tests/Projektwiring; in eigener Basis, nicht main. |
| `9e493762fd36e8c8e5d646c723078d6ed40d12b4` | Flick öffnet Editor nicht; Nachbarvorschau und vorhandene Merge-/Flicktests. Mehrfachpaket mit Extensionfixture: nicht pauschal cherry-picken. |
| `e27dd243f6c6958a20d1646980ddaa80421fcaae` | Fokuszustand innerhalb des Hardware-Escape-Containers; bestehende Home-/Eingabeabnahme kandidatgebunden. |
| `5bf8a5651944a5399e7d727f73a5305b7b90d92b`, `0fb7154c4df7fa3efb0eef1bf72aee5c2b375fb3` | Zugehörige UI-Fokusvoraussetzung/Kommentar; echte Hardware-Escape-Abnahme bleibt laut Checkpoint offen. |

Konkrete Abhängigkeit der Sync-Baseline:
`a6d20b2c15f7451ff4e8648695136b0b80f7b5fb` löst den Aufruf von
CKSyncEngine aus dessen Delegate-Task-Kontext. Der vorhandene reale
Empfangsbeleg für `7a172676` enthält diesen Fix ausdrücklich. Er ist keine
neue unabhängige Aufgabe; eine Übernahme der Sync-Korrekturen muss diesen
bereits vorhandenen Zusammenhang mit Master klären. Der Gesamt-Mobilediff
main134 → eigene Basis umfasst 26 Pfade; das ist keine Übernahmefreigabe.
Auch Flick/Fokus und die genannte Delegatekorrektur sind Vorfahren der eigenen
Basis, nicht von main. Alle Übernahmen bleiben mit Master abzustimmen.

Mobile55 wurde zusätzlich read-only gegen seinen tatsächlich vorhandenen
Branch `cockpit/mobile-popup-berechtigungsumsetzung-55e4` geprüft
(`17a191fa8d11d44f03a24acec244fbc9ed5839da`, Bericht Vertragsrevision r19).
Sein Lifecycle-Delta gegen main134 ergänzt den DEBUG-Popup-Probeaufruf nach
Navigation. Dieser fremde Delta und sein Handback werden nicht durch eine
eigene Änderung aus der älteren Basis überschrieben.

## Befunde am gemeinsamen Aufrufpfad

### F01 / DM-AC-03 und DM-AC-04

`stagePendingLinkPreview` prüft Source-Tab/Workspace/Modus.
`presentStagedLinkPreview` erzeugt eine neue UUID und ruft `makePage` ohne
Workspace auf. `makePage` sucht ausschließlich im noch nicht ergänzten
`tabs`-Array. Damit fällt eine normale isolierte Preview auf
`normalWebsiteDataStore` zurück. `retryLinkPreview` lädt dieselbe Page;
zweistufige Adoption registriert sie erst danach, ohne den Store zu wechseln.
Private wählt bereits separat `WKWebsiteDataStore.nonPersistent()`.

Alle fünf produktiven Page-Aufrufstellen ermittelt: `page(for:)`,
`MobileBrowserController.createTab`, SharedTabs-Recovery, UITesting-Setup
und LinkPreview. Lifecycle besitzt außerdem Permission-, Link-, NewTab-,
ExternalScheme-, HTTPFailure- und Downloadcallbacks sowie Extensionattachment.
Korrektur muss deren Initiator- und Gültigkeitsprüfung zusammenhalten;
ein zusätzlicher optionaler Workspaceparameter allein beweist DM-AC-04 nicht.
Mobile55 behält den vorhandenen Popup-/Permissions-/Storagevertrag.

### F02 / DM-AC-05 bis DM-AC-07

`CompanionAppModel.mergeWorkspace` → `LocalFirstRepository.mergeWorkspace`
ändert Knoten und Source-Tombstone; Receipt/Undo/Enqueue umfassen nur diese.
`CompanionSnapshot.splitGroups` und `.archiveEntries` bleiben unberücksichtigt.
`persist()` rollt bei Storefehler auf den letzten persistierten Snapshot zurück.
Für Fehler vor dem Persist muss die gesamte Mutation ebenfalls zurückgenommen
werden; bestehende Intentmutationen zeigen dafür den lokalen `before`/`defer`-Weg.

Das vorhandene Schema deckt die verlangten Daten bereits ab:
Split `workspace_id`, `topology`, `ratios`, `tombstone`; Archiv
`snapshot`, `state`, `tombstone`. `SharedArchiveSnapshot` enthält Workspace,
Seitenreferenzen und optional `SharedSplitMetadata` mit eigener Workspace-ID.
Archiv-ID leitet sich aus logischer Seite/Split-ID ab, nicht aus Workspace-ID.
Ein Merge darf keine ID, Ratio, Topologie oder Archivzustandsgruppe neu erfinden.
Das vorhandene `merged_into` auf Workspace/Tombstone ist bereits codiert;
derzeit ist **kein fehlendes Wirefeld nachgewiesen**.

Offene gemeinsame Grenze: `CompanionImportBatch.swift:199–209` übernimmt
eingehende Split-/Archivrecords über den existierenden Field-Merge unverändert.
`CompanionSnapshot.treeNodesForPresentation` und `liveWorkspaceDestination`
leiten späte Baumknoten über Tombstone/Watermark um, nicht Split/Archiv.
DM-AC-07 verlangt den bestätigten Konflikt-/Rehomevertrag auch dafür.
Diese zwei Dateien stehen nicht in der anfänglichen Schreibdateiliste;
vor Änderung ist ein konkreter zusätzlicher Master-/Sync-Handback nötig.
Kein neues Wirefeld und keine künstlichen konkurrierenden Uhren vorgeschlagen.

### F03 / DM-AC-08 und DM-AC-09

`CloudKitSeparatedWorkspaceSession` besitzt bereits eigenes Repository,
Bridge, Key und Provider. Ihr `sync()` liefert dessen Snapshot;
`syncEnabledWorkspaces()` übernimmt daraus nur Name/Icon/Tombstone/Status.
Der Content wird nicht für Baum/History verfügbar gehalten.
`SeparatedWorkspaceRows(onOpen:nil)`, Sidebar-Auswahl/Saved-Items und
`openSharedPage` lesen ausschließlich den Hauptsnapshot.

Zusätzlich folgt die echte Historyreise
`synchronizeSelectedPageMetadata()` → BrowserView-Navigationcallback →
`CompanionAppModel.recordMobileNavigation(title:url:)`: die Navigation besitzt
zunächst einen Tab samt Workspace; der Aufruf verwirft diese Zuordnung und
schreibt in das Hauptrepository. Die History-Sheetinstanz verwendet ebenfalls
das Hauptmodell. Das sind Sourcebelege, keine ausgeführte Datenleckreproduktion.

Gemeinsame Writergrenze: `CompanionMobileSharedIntent.swift` bindet
`receiveSharedTabIntent` und `reconcileBrowserSharedProjection` fest an das
Hauptrepository/-snapshot. Dieser Pfad gehört nicht zur anfänglichen
Schreibdateiliste; der zusätzliche konkrete Routingvertrag ist beim Master
angefragt. Die bestehenden Namespace-/Registry-/Sessiongrundlagen bleiben
maßgeblich; keine neue Repositoryarchitektur.

## API-Vertrag und ausreichende Prüfung

Projektvorgabe: iOS 26 Deploymentminimum, Swift 6 / Strict Concurrency
`complete`, Xcode 27.0 / 27A266a, iOS SDK 27.0 / 24A430.
Die [bestehende versionierte Apple-/WebKit-Recherche](2026-10-06-mobile-webextension-popup-permissions.md)
wurde gelesen: SDK-Interfaces/Headers, MainActor und Extensionattachment.
Context7 meldete dort und in der Quellsession ausgeschöpftes Monatskontingent;
keine erneute Kontingent-/Kosten-/Pollschleife. Vor tatsächlich neuen
Frameworkaufrufen die passende aktuelle Apple-Dokumentation und gezieltes
SDK-Interface prüfen. Bislang kein neuer API-Aufruf und kein Compilerlauf.

Tatsächlicher lokaler Readback: `xcodebuild -version` und
`xcrun --sdk iphoneos --show-sdk-version/--show-sdk-build-version` bestätigen
27.0/27A266a und 27.0/24A430. Das WebKit-Swiftinterface
`arm64e-apple-ios.swiftinterface` hat SHA-256
`9d7ad9903b8ce8881430f9d6369d5963aa7d2845c31f3041a715e818186f17a1`;
`Configuration.websiteDataStore`, `webExtensionController` und der Pageinitializer
sind MainActor. Die aktuelle [Apple-Referenz](https://developer.apple.com/documentation/webkit/webpage/configuration/websitedatastore)
wurde über deren DocC-JSON gelesen (HTML verlangt JavaScript, Markdowntransport
wurde vom Browsertool abgewiesen): `websiteDataStore` ist ab iOS 26 verfügbar.
Der erneut gelesene [offizielle WebKit-Profilvertrag](https://webkit.org/blog/14423/building-profiles-with-new-webkit-api/)
bestätigt separate persistente Stores per UUID, Zuweisung vor WebView-Erstellung
und Fehler bei Storelöschung während Nutzung. Kein CloudKit-APIwechsel nötig
oder belegt; vor einem solchen Änderungsschritt wäre dessen Vertrag gesondert
anhand der verwendeten Integration zu prüfen.

Verifikation bleibt an den **einen exakt gebauten Kandidaten** gebunden:

1. Gemeinsame/isolierte Loginmarker derselben kontrollierten Site: Preview,
   Retry, Adoption, Sourcewechsel/Close, Callback-/Download-/Permissiongrenze,
   Private nonpersistent; danach fokussierte Store-/Lifecycle-Regression.
2. Shared A → B mit Ordner und flat, Split/Archiv, Undo/Neustart; danach
   Persistfehler, veraltetes Undo, Outbox und späte Offline-Peerrecords.
3. Optierter isolated-Namespace: Baum/History/Öffnen/Ändern; danach
   Keyverlust, Accountwechsel, Sync aus und Hauptnamespace-Negativnachweis.

Vor jeder schweren Phase frische Drei-Sample-Aggregate-CPUaufnahme
(macOS-Erstsample verwerfen), Speicher-/Swap-/Diskprüfung, konkrete
Ownership und Inhouse-Präferenz gemäß aktuellem Nutzervertrag.
Historische Ressourcenzahlen des Checkpoints sind keine aktuelle Messung.
Kein Build, UI-/Geräteeinsatz, CloudKit-Datenzugriff, Review, Push oder Release
wurde begonnen. Alle DM-AC-03–09 und Liefergates bleiben offen.

## Tatsächliche Befehle und Grenzen

- `pwd`; `git status --short`; `git rev-parse --show-toplevel --git-common-dir HEAD`;
  `git branch --show-current`; `git worktree list --porcelain`: eigene Bindung sauber.
- `git show -s --format=... <pin>`; `git merge-base --is-ancestor <pin> main/HEAD`;
  `git rev-parse <pin>:<path>`: vollständige Commit-/Blobidentitäten und Herkunft.
- `git diff 13423f52..HEAD -- <betroffene Pfade>`;
  `git show --stat ...`; `git ls-remote origin refs/heads/main refs/heads/codex/desktop-core-feature-wave-20260830`:
  tatsächliche Endpunkte und begrenzte View-Unterschiede.
- `rg`/`sed`/gezielte Python-Leseabfragen: Aufrufpfade, Verträge und native
  Metadaten. Einzelne explorative Dateinamenglob-Abfragen fanden keinen Pfad;
  anschließend wurden die tatsächlichen Namen per `rg` aufgelöst. Das sind
  Suchfehler, keine Produkt-/Buildfehler und keine Testergebnisse.

Dokumentprüfung: Workflow-JSON, eindeutige Kriterien/Tasks, Abhängigkeiten,
Owner-/Projektbindung, UTF-8/LF/Dateigröße, drei lokale Linkziele und 13 genannte
Commitobjekte geprüft. Dokumentcommit
`8fe1c4fb1615b1297210a2f9782ca981c7347955` enthält ausschließlich diesen Bericht.
Staged und committed Whitespaceprüfung bestanden; Worktree danach sauber.
`python3 tools/check_dco.py --base c3a1d1362b6cadffa522f6d39ca03a0a47438c8b --head 8fe1c4fb1615b1297210a2f9782ca981c7347955`
bestand für einen Commit. Der erste Aufruf mit symbolischem `HEAD` wurde vom
CLI-Vertrag abgewiesen; Wiederholung mit vollständiger SHA bestand.
`python3 tools/check_lane_boundaries.py --all --since c3a1d1362b6cadffa522f6d39ca03a0a47438c8b`:
0 Fehler/0 Hinweise. Keine Produktdatei geändert.
Kein Modellreview für diesen ausschließlich dokumentarischen Zwischenstand;
das vorgeschriebene Produktreview bleibt offen.

**Geparkter nächster Schritt:** Master bestätigt die attributable Mobilebaseline
und die konkreten F02/F03-Dateien samt zusätzlichem Rehome-/Writerumfang;
Mobile55 bestätigt Lifecycle-/Previewbasis und Schreibhandback. Danach in
derselben Kachel, demselben Thread, Branch und Goal DM-T01/T02 umsetzen.
Keine weitere unabhängige Produktänderung ist innerhalb der derzeit belegten
Ownership möglich. Unverändertes Statuspolling oder ein Ersatzworker ist kein
Fortschritt. Das eigene Goal ist nicht erfüllt und wird nicht abgeschlossen.

Vorgeschlagener Eintrag für den vom Master besessenen Mobile-Checkpoint:
`AHOI-DM-REMEDIATION-20261007-01 / 8e74e15d: eigener Worker AE918FA0,
Thread/Goal 01a11680-b5c7-70e2-a086-1402477603f8, sauber gestarteter eigener
Worktree mobile-workspace-korrektur-8e74e15d auf c3a1d136; tatsächlich
openai/gpt-6-astra/xhigh im bestehenden Account. F01–F03 sourcebestätigt,
main134/Feature849-Pins und Bytegleichheit geprüft. Bericht
docs/reviews/2026-10-07-mobile-desktop-parity-remediation.md enthält
Aufgabenvertrag und konkrete Source-/Sync-Abhängigkeiten. Noch keine Source-,
Build-, UI-, main- oder Lieferlease; wartet auf Handbacks von Master und
Mobile55. DM-AC-03–09, natives Review und Delivery offen. Dieselbe Identität
fortsetzen; kein zweiter Worker.`

Der konkrete Checkpointeintrag und Bericht wurden dem Master mit
Cockpitquittung `5a475558` angeboten; der Ursprung erhielt Stand und
Aufgabenanschluss mit `52883ebe`. Das belegt Annahme beim Cockpit, noch keinen
Dokumentimport, Handback oder fachlichen Empfang. Ein passender Sourcehandback
ist bis zum Abschluss dieser Vorbereitung nicht eingegangen. Stop-Bedingung
des Auftrags greift für die abhängige Umsetzung; das einmalige
`cockpit_report(delegation_id: "8e74e15d", status: "abbruch")` meldet diesen
ungeklärten Ownershipzustand, keine erfolgreiche Korrekturlieferung.
