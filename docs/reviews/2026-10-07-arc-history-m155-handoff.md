# Arc-Verlauf M155: Quellenübergabe e5fbc1f3

Status 8. Oktober: Quellportierung von Root als `850aa587` übernommen und
Zeitregel in `6eefc4a8` bestätigt/korrigiert. Eigener ursprünglicher Worktree
auf unverändertem Branch wiederhergestellt. Eigenes natives Goal nach ausdrücklicher End-to-End-Freigabe ACTIVE,
identischer Thread/createdAt; keine Erfüllung vor realer Lieferung.
Build/Signatur, native20/20, sechs Historyfälle und die guarded Devinstallation
sind unten belegt. Betroffene sichtbare Postreview-Reruns und die gemeinsame
Desktop-main-/Push-Abnahme bleiben offen. Die ältere Chronologie bleibt erhalten.
Stabile Identität `worker-handoff:e5fbc1f3`, Goalkennung
`AHOI-ARC-HISTORY-M155-20261007`. Root besitzt Integration und Auslieferung.

## Identität und tatsächliche Quittungen

| Beleg | Tatsächlicher Stand |
| --- | --- |
| Cockpit-Session | `1019DDF3-0EE8-4145-B368-325E1A620C5A`, Delegation `e5fbc1f3`; `cockpit_sessions(cwd)` bestätigt eigenen Worktree und Rolle. |
| Neuer nativer Thread | `01a113e8-cfc5-79b1-9a36-71e8e45f3a93`; eigene `session_meta` Zeile 1, Start `2026-10-07T01:09:47.093Z`, CLI `0.160.1`, `originator=codex-tui`, `model_provider=openai`. Verschieden vom Root-Thread. |
| Native Auftragseingabe | Eigener Rollout Zeile 9, `2026-10-07T01:09:55.978Z`, user-Eingabe mit 19.978 UTF-8-Bytes, SHA-256 `39e9cd0ed47a1d8fa682def80a9efea534468a62f875650e3905afd7f529b702`. Keine zusätzliche Zieleingabe. |
| Empfehlung | Initialauftrag: `gpt-6.1-sol/xhigh`. |
| Geplantes Routing / Jev | Keine unabhängige Entscheidungsquittung vorhanden; tatsächliche Konfiguration ist kein Nachweis des Entscheidungswegs. |
| Angewandte Konfiguration | Rollout `turn_context` Zeile 8: `model=gpt-6.1-sol`, `effort=xhigh`, `approval_policy=never`, Sandbox `danger-full-access`; Cockpit meldet unabhängig GPT-6.1-Sol/xhigh. Keine Einstellung geändert. |
| Account | `CODEX_HOME` unter `Accounts/DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5/CodexHome`; Orchestrator-Receipt bestätigt dieselbe Caller-Account-ID. Codex ist nativ/Cockpit belegt; Alias Caeli stammt aus Auftrag, noch keine unabhängige Aliasquittung. Keine Credentials gelesen. |
| Eigenes Goal | Erstes `get_goal` liefert `null`; genau ein `create_goal` mit dem gelieferten Ziel liefert eigenen Thread, `active`, `createdAt=1791335405`, initial 0 Tokens/0 Sekunden, kein Tokenbudget gesetzt. Identität bleibt erhalten. |
| CWD / Branch / Basis | `pwd`, `session_meta`, `git worktree list` und Cockpit stimmen überein: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-arc-verlauf-m155-e5fbc1f3`, `cockpit/arc-verlauf-m155-e5fbc1f3`, Basis `0c6466980ef95be77c1b5d61d721ca7b1766e747`; anfänglich sauber. |
| Root / main | Kanonischer Rootbranch `codex/desktop-core-feature-wave-20260830` anfänglich ebenfalls `0c646698`; lokal gespeicherte `main` und `origin/main` beide `13423f52ced6744842dfd2fea16f9aadda1e5f3f`. Kein Remote-Servernachweis, kein Root-Schreibzugriff. |

Eigener Rollout:
`/Users/vossmedien/Library/Application Support/Terminal Cockpit/Accounts/DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5/CodexHome/sessions/2026/10/07/rollout-2026-10-07T03-09-47-01a113e8-cfc5-79b1-9a36-71e8e45f3a93.jsonl`.
Nur eigene Metadaten, Auftrags-/Turnfelder und Quittungen wurden gelesen.
Root `68E66C9E-7E52-4842-B328-03D9CD6D3057` /
`01a11179-cb6b-7dc1-9e53-f3d72fefc198`, Mobile55, C70 und Crest bleiben unverändert.

## Aufgaben und Abhängigkeiten

1. **Erledigt:** neue Identität, eigener Worktree, `null → active` belegen;
   geltende Regeln, Skills und Integrationsvertrag lesen.
2. **Erledigt:** gepinnte M155-APIs, Donor-/Root-Deltas und tatsächliche
   Aufrufer-/Profil-/Recoverygrenzen gelesen und dokumentiert.
3. **Erledigt als Quellen:** minimale Portierung im Rootmodell und tatsächlicher
   Settings-UI; Scope-/Ownerhandback erhalten. Root hat die Zeitregel
   inzwischen auf `not_after=now` gebunden (`6eefc4a8`), kein erneuter Zeitentscheid.
4. **Erledigt:** gezielte Quellenchecks, Diff/DCO/Lane/Quellsyntax und
   800-Zeilen-Budget; Sourcecommit `346edf636ce764885f8125c3e1c9abbf4ae8aeab`.
   Dieser Bericht bindet Dateiliste, Vendorbelege und Prüfgrenzen daran.
5. **Erledigt am Kandidaten 6f06daf0:** guarded Build/Signatur, sechs echte
   Settings-/native Historyreisen und 20 fokussierte native Historyfälle.
6. **Korrigiert, betroffener sichtbarer Wiederholungslauf offen:** zwei native
   Reviewrunden, drei konkrete Journeyskriptbefunde; Symlink-/Hardlink-/Port-
   Negativprüfungen bestehen. Keine dritte Reviewrunde.
7. **Erledigt als Entwicklungslieferung:** zurückgegebenes exaktes 6f-Appbundle,
   bewachte atomare Installation und isolierter Startup-Smoke; Rootreadback bestätigt.
8. **Offen bei Root:** minimalen Arc-Nachtrag aufnehmen, sichtbare Wiederholung
   nach tatsächlichem GUI-Handback, gemeinsame Main-/Push-/Desktop-Abnahme.
   Erst tatsächliche Schlussbelege erlauben den eigenen Goalabschluss.

Ein natives Planwerkzeug wird hier nicht angeboten; diese Aufgabenliste ist
der gepflegte Fortschrittsstand im ausdrücklich zugewiesenen Bericht.

## Gelesener Vertrag und offene Gates

Der vollständige [Integrationsvertrag r1](2026-10-07-desktop-main-integration-contract.md)
wurde gelesen, einschließlich Abschnitten 4 und 6 und aufgabenrelevanter
Quellen: Root-/Donor-`docs/ARC_HISTORY_IMPORT_PLAN.md`, Arc-Modul-README,
Service/Factory/Abschluss-/Profilmapping, Patch 0084 sowie Master-Migration,
IMPORT-ARC-01–12, WS-ISO-10, Browser-Import-Gate und DoD22.
README/BUILDING, Desktop-/Bookmarks-Ownership, Architektur, ADR0011 und
Lanevertrag wurden gelesen. Keine nähere AGENTS-Datei vorhanden; tatsächlich
geltendes Root-AGENTS gelesen, globale Regeln aus Initialkontext. Skills:
[project-feature-workflow](</Volumes/Macintosh HD - Daten/Cloud/Projekte/_Organisation/Codex/skills/project-feature-workflow/SKILL.md>),
[terminal-cockpit-communication](/Users/vossmedien/.agents/skills/terminal-cockpit-communication/SKILL.md)
und [Orchestrator-Handoff](</Volumes/Macintosh HD - Daten/Cloud/Projekte/_Organisation/Codex/skills/saas-product-workflow/references/orchestrator-handoff.md>).

### Wiederaufnahme nach Scopehandback

Die hier zugestellte Rootnotiz (Absender `68E66C`, Scopekorrektur e5fbc1f3
vorSource) bestätigt die tatsächlichen Settings-Dateien
`ahoi_arc_import_section.{ts,html.ts,css}`, deren WebUI-Test,
`tests/repository/test_ahoi_settings_page_contract.py`, den richtigen Patch
`0084-ahoi-arc-imported-visit-source.patch` und Patch-README als ownbar.
Arc-Modul, series und dieser Bericht bleiben zugewiesen. Die falsch vermuteten
Arc-UI-Verzeichnisse und der alternative Patchname werden nicht angelegt.
Rootnotiz `bc8b9223` quittiert die Übernahme in derselben Identität und fragt
weiterhin konkret nach der Zukunftszeitregel. Die erste Gatephase und ihr
bereits einmal abgegebener `cockpit_report` bleiben historische Belege;
kein zweiter Reportaufruf und kein Ersatzworker.

Die neue Cockpitmeldung nennt Überlappung mit Mobile55 für BUILD/DEPS und
zwölf History-Dateien. Lesender Gitcheck: Mobile-Worktree HEAD
`f69f6ceb87e5af59f30ac9ac163984419c2d189a`, **keine** uncommitteten Arcänderungen;
letzter Arccommit ist genau Donor `28deb7986a1939ce9d23ed1d2eccbf67fedbef03`.
Dies beweist allein keine Datei-Freigabe. Rootnotiz `633fb7d1` fordert die
konkrete Ownerzuordnung, ohne Mobiledateien oder -Goal zu verändern. Die danach
zugestellte Rootnotiz zu Sourceintegration `7a4d5602` bestätigt ausdrücklich
„deine Ownership bleibt“ und die Actual-WebUI-Freigabe via Rootnotiz `c6a6cfb5`.
`c6a6cfb5` ist dabei eine Notizkennung, kein hier auflösbarer Gitcommit. Root
bestätigt unabhängig das eigene ACTIVE-Goal und Sol/xhigh. Arc-/Settings-Code
ist zwischen Rootbasis `0c646698` und `7a4d5602` tatsächlich unverändert
(`git diff` dieser Pfade leer); Mobile Arcstatus sauber, letzter Arccommit Donor.
Die neue Rootserie enthält 0097, das own Delta fügt ausschließlich 0084 hinzu.
Root-HEAD inzwischen `7a4d5602eae7ac59e082d65a2e808e9648135699`; eigener
Worktree/Basis bleiben erhalten, keine neuere Rootarbeit zurückgesetzt.

### Historische erste Gatephase

Root-Ownershipnotiz `962d3ac0` wurde beim Cockpit aufgenommen; das ist keine
Ownerantwort. Entscheidungsanfrage `28a985de-dddc-4dc2-8a8a-edb8b3cae982`
wurde `2026-10-07T01:11:05Z` als `queued`, danach `sending` und `2026-10-07T01:12:24Z` als `answered` quittiert.
Die Antwort bestätigt ausdrücklich die offene Zukunftsentscheidung. Sie
verweist auf Rootmeldung `84750A2A`: die vier Settings-/Testdateien und
`patches/chromium/0084-ahoi-arc-imported-visit-source.patch` seien als
Scopekorrektur freigegeben, Zustellung/Übernahme seien aber nicht belegt.
Die konkrete Scopequittung wurde erst nach dieser ersten Gatephase zugestellt (oben).
Gezielte Rootnotiz `8f858270` fordert dieselbe konkrete Quittung und die
Zeitentscheidung an; Aufnahme quittiert, keine Ownerantwort vorhanden.
Kein zweiter Worker oder neuer Handoffauftrag erstellt.

- **Zeitregel offen:** Donor `28deb7986a1939ce9d23ed1d2eccbf67fedbef03`
  fordert im Plan die Ablehnung zukünftiger Zeitstempel, der Runner setzt
  `not_after=now+Days(1)`. Integrationsvertrag r1 benennt den Gegensatz ohne
  bestätigte Auflösung. Root wurde konkret nach `>now`-Ablehnung gegenüber
  24-Stunden-Toleranz gefragt; keine Produktannahme übernommen.
- **Erste Gatephase, inzwischen aufgelöste Scopefrage:** tatsächliche UI liegt
  in `overlay/chromium/src/chrome/browser/resources/settings/people_page/ahoi_arc_import_section.{ts,html.ts}`;
  Argument11/Kategorie/Zählresultate müssen dort angepasst werden. Außerdem
  betroffen: `overlay/chromium/src/chrome/test/data/webui/settings/ahoi_arc_import_section_test.ts`
  und `tests/repository/test_ahoi_settings_page_contract.py`. Konkrete Pfade,
  Zweck und Ownership wurden angefragt; zunächst ausschließlich gelesen.
  Die zwei zugewiesenen `ahoi/.../arc_import`-UI-Verzeichnisse existieren nicht.
- **Vendorzugriff:** kein gemeinsamer Checkout benutzt. Context7
  `resolve_library_id(Chromium, M155 History...)` meldet `Monthly quota exceeded`.
  Web-open auf Gitiles konnte die Seiten nicht öffnen; direkte offizielle
  Gitiles-TEXT-Abfrage am Pin funktioniert. Keine APIs/Enumwerte geraten.

## Gepinnte Primärquellen und Konsequenzen

Quelle für alle folgenden Dateien ist der offizielle Gitiles-TEXT-Endpunkt
`https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/<Pfad>?format=TEXT`.
Die dekodierten Vendorbytes wurden ausschließlich in einem privaten temporären
Verzeichnis gelesen; kein gemeinsamer Checkout, Patchapply oder Build.
`chrome/VERSION` bestätigt **155.0.8059.26**; `DEPS:348` bestätigt
V8 **82b65fdd7b5847a41e76c9eeb04351ebec267785**, übereinstimmend mit
`config/chromium.json`/`config/dependency-build-workarounds.json`. Keine V8-API
wird für History verändert; kein V8-Buildpass behauptet.

| Primärquelle am exakten Chromium-Pin | SHA-256 der gelesenen Vendorbytes |
| --- | --- |
| [chrome/VERSION](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/VERSION) | `6d627a92d470577ba38f95ef4118a5c989175f01d1792be664a1450c17f698f4` |
| [DEPS](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/DEPS) | `2909b1d749ab33e980d7f226a67aa6f95245e773fd3b85e8551ca347bca71529` |
| [components/history/core/browser/history_backend.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_backend.cc) | `ff1039960f8448105013689b4ee8165a9128b78439bb44c092643c836375292c` |
| [components/history/core/browser/history_backend.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_backend.h) | `288180063d3e7f93f099b90dbba26f663de2678b42efb64c0dc8ba51c0a19b5c` |
| [components/history/core/browser/history_types.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_types.h) | `e064581964b55f43ea2663c28fbfd0d6d75cc78dfbb644e4f27590a06b4eb731` |
| [components/history/core/browser/visit_database.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/visit_database.cc) | `f883a2120554d4d3cbd4f5df3d1670c73f0184e83842c0bd69f46607cad93e67` |
| [components/history/core/browser/visit_database.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/visit_database.h) | `6cbf4a8b3e051794e6162e7903528943e51251fd777076ac4b80cf190f305c67` |
| [components/history/core/browser/history_service.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_service.cc) | `4ad3439001284f60981277956ce4fb14ff0367448b96f8df9d7012de5f6c9d7b` |
| [components/history/core/browser/history_service.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_service.h) | `6c4cf3f63f50e3d9e4d1bc8fdafb075eae5a294ac91e622f08991a06407eae52` |
| [components/history/core/browser/history_database.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_database.h) | `62feb374ae8f92250998ee436aa1e70edf5255764a6b660706186bd011b91f65` |
| [components/history/core/browser/expire_history_backend.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/expire_history_backend.cc) | `1d67aab88a8d8094ab4fe120b5d6dad6914841636dc76f908589505ed24f4de4` |
| [components/history/core/browser/history_db_task.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/history_db_task.h) | `4d2a0e2a8c63ba99fa2f04c7776474b3f8d319201a24f346793ac4e569614524` |
| [chrome/browser/history/history_service_factory.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/history/history_service_factory.cc) | `613fbdd7e281047b9a3ccfd6590b89969f5cc1d63790e6276217dd02a70704ba` |
| [sql/transaction.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/sql/transaction.h) | `7fba5cfac1f94add16dbd6e0f53b3baddf619cfa401510f2cb83318e67e781ec` |
| [tools/metrics/histograms/metadata/sql/histograms.xml](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/tools/metrics/histograms/metadata/sql/histograms.xml) | `bb747c9ef75234a8cbe4a56275b82b793a6c91c605b8d32167ac9aaab18c73de` |

Am Pin gelesene API-Grenzen und durch die Portierung adressierte Unterschiede:

- `VisitSource` endet weiterhin bei `SOURCE_OS_MIGRATION_IMPORTED=7`;
  Wert 8 ist am Pin frei. `VisitSourceFromInt` muss den neuen Wert ausdrücklich
  abbilden. SQL-Tag `AhoiArcHistoryImport` fehlt upstream; der vorhandene
  Patch0001 fügt bereits `AhoiTabTree` hinzu. 0084 daher auf die R-Serie
  adaptieren, ausschließlich die fünf ursprünglichen Vendorpfade berühren.
- **Tatsächliche Compile-Inkompatibilität:** Donorwriter verwendet
  `database->transaction_nesting()!=1`. M155-`HistoryDatabase` bietet diesen
  Member nicht. `HasActiveTransactions()` ist vorhanden; `CreateTransaction()`
  darf laut Header ausdrücklich nur `HistoryBackend`, kein `HistoryDBTask`,
  aufrufen. Keine neue/nestende Overlaytransaktion oder Testzugriff als Ersatz.
- **Durabilitätsgrenze:** Donor-`CommitForAhoiImport()` ruft `void Commit()`.
  M155-`CommitSingletonTransactionIfItExists()` liest den bool von
  `singleton_transaction_->Commit()`, gibt ihn aber nicht weiter und setzt
  die Transaktion zurück. `BeginSingletonTransaction()` kann ebenfalls
  scheitern. Nur ein tatsächlich bestätigter Commit und gültige nächste
  Backendtransaktion dürfen Erfolg/Committed-Journal erlauben. Ein blinder
  void-Return oder `HasActiveTransactions()` allein beweist keine Durabilität.
  Das ist ein konkret zu korrigierender Seam, kein fertiger Implementierungsbeleg.
- `AddPagesWithDetails` lässt vorhandene URLRows unverändert und fügt einen
  LINK-Visit hinzu. `DeleteVisit(const VisitRow&)` löscht exakte Visit-ID und
  Sourceeintrag, korrigiert eingehende Referenzen, verändert keine URL-Counter
  und gibt **void** zurück. Rücknahme muss tatsächlichen Zustand nachlesen.
  `DeleteURLs(vector<GURL>)` löscht über Expirer alle Visits, URL/Faviconzustand,
  sendet Benachrichtigungen und committet selbst. Deshalb ausschließlich auf
  nachweislich neu erzeugte URLs innerhalb derselben serialisierten DBTask
  anwenden; vorherige/fremde URLs und Visits erhalten. Generisches ExpireHistory
  ist kein exakter Ersatz.
- `HistoryDBTask::RunOnDBThread` mit `true` läuft genau einmal auf der
  Backendsequenz; `DoneRunOnMainThread` wird bei Cancel nicht aufgerufen.
  `ScheduleDBTask` benötigt den lebenden Backendrunner. `HistoryService::Cleanup`
  invalidiert WeakPtrs und entfernt den Runner. Arc-Runner vor HistoryService
  beenden/canceln, mit Factory-DependsOn und per Zielprofil eigenem Service.
  `EXPLICIT_ACCESS` umgeht im Factoryzugriff die Historysaving-Policy: deshalb
  zusätzlich Policy **je ausgewähltem Zielprofil** prüfen. Keine
  Hauptprofil-Policy als Ersatz, kein Zielservice-Fallback.
- Root-Service behält `BuildForRegularProfile()` und SessionBridge-DependsOn.
  In der Rootbasis endet `FinishJournalWrite` nach Freigabe des Guards bei
  `FinishWithSeparatedWorkspaces`; auch der Replaypfad gibt ihn früh frei.
  Für History muss die Abschlusskette genau einmal bis terminal verlängert
  werden. Hauptplan entfernt getrennte Arcprofile; Donorreader vereinigt
  dagegen alle Backup-Historyquellen. Diese Quellen nach Zielprofil filtern,
  pro Ziel Journal/Policy/Runner/Shutdown binden; Donor nicht blind übernehmen.
- Donorwriter validiert HTTP(S), aber nicht nochmals Credentials; Reader
  benutzt `IsSafeImportUrl`. Der DBtask-Eingang muss die erforderliche
  Credentialgrenze erhalten. Upstream `AddPagesWithDetails` schreibt in seinen
  beiden Fehlerpfaden URL in DLOG: ARC-Pfad im minimalen Patch redigieren,
  andernfalls ist die geforderte Loggrenze nicht belegt.

Kein fehlender APIvertrag wurde durch eine geratene Methode ersetzt.
Die nachfolgend beschriebenen Sourceadaptierungen implementieren diese
Übergänge. Donor- und ergänzte native Unit-/Browser-/WebUI-Fälle wurden gelesen
und vorbereitet, weder kompiliert noch ausgeführt.

## Zusätzliche gelesene Lifecycle-/GN-Primärquellen

Alle ebenfalls am oben genannten exakten M155-Pin, nur private temporäre
Gitiles-TEXT-Kopien. `LoadProfileByPath` ruft bei nicht registriertem Ziel seinen
Callback genau einmal mit nullptr auf, auch bei bool false. Der Port benutzt
kein CreateProfileAsync als Fallback. Pfad, Regular-/OTR-Zustand und aktive
Workspace-Registry werden nach dem Laden erneut geprüft. Native
`ScopedProfileKeepAlive::TryAcquire` schützt das geladene Ziel während der
Settings-Operation; bestehender Origin `kChromeViewsDelegate`, kein neuer Enum.
Er schützt nicht gegen Browserquit. Factory-Dependencies ordnen Arc-Shutdown
vor SessionBridge/HistoryService; Runner invalidiert WeakPtrs und cancelt
Tracker. Ziel-Shutdown liefert dem noch lebenden Aufrufer einen terminalen
Recoveryfehler. `GetVisitsSource` meldet nur Backendverfügbarkeit (DB-Abfrage
intern void), DeleteVisit/DeleteURLs ebenfalls keine Erfolgsbools: überprüfte
Visit-IDs, URLzeile, Quellen und Commitbarriere ersetzen keine zugesicherte
Rückmeldung dieser Methoden. Native Fehler-/Recoverypfade bleiben Rootchecks.

| Primärquelle | SHA-256 der gelesenen Bytes |
| --- | --- |
| [chrome/browser/profiles/profile_manager.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/profile_manager.h) | `cba93d3f2d0c1dc9e6a0be507527543e6d5d5433bd0bd521b29f019c61ad7a9a` |
| [chrome/browser/profiles/profile_manager.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/profile_manager.cc) | `7bbb91ce3f1159b540666686b8cbc2e887a561978f9d7ebb7aa9230e42a7dec3` |
| [chrome/browser/profiles/profile.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/profile.h) | `dc1ff74874ce73db6eeb579ed9b5a85a3226387ce3a4172eb814336562634485` |
| [chrome/browser/profiles/BUILD.gn](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/BUILD.gn) | `adfc94ede8a34bf7a5c89d51c788eeac0b04c8d20bbaf0fa5c55c396c7eace92` |
| [chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h) | `c5e29afba78dd1b5f37a19c11644f53e2c97d13f435322bab81a5f5277c1db6a` |
| [chrome/browser/profiles/keep_alive/profile_keep_alive_types.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/keep_alive/profile_keep_alive_types.h) | `682b5461c71387bf3ec6b12871a9c7e6554e4f93571f435fd0b9c7f87225ad15` |
| [chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.cc](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.cc) | `8ff801b2c40bfb7e6cc5dfa7c3d14d6da83ed3cc6f2ae37408c9ce805da33a82` |
| [chrome/browser/profiles/keep_alive/BUILD.gn](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/profiles/keep_alive/BUILD.gn) | `caeed305ece490921a98662420fa3932b6d15c4aa786b149bcc6574512556113` |
| [chrome/browser/history/BUILD.gn](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/history/BUILD.gn) | `185667582721a9bb132b767bffe4969b27de24e7fb5deeaf7a44244dd0a43b6f` |
| [components/history/core/test/BUILD.gn](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/test/BUILD.gn) | `c3a0dee3f584ec7783f4c19440aa4518ba9d0d26db44665a489d0bcad28932e8` |
| [base/task/cancelable_task_tracker.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/base/task/cancelable_task_tracker.h) | `b92323db010cd3a4b21edab8a84168e12496c2353be2a3014efac1f9d0da77d1` |
| [components/history/core/browser/url_row.h](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/history/core/browser/url_row.h) | `af7d93e38b70d97fc281376ab01fc9c7208b351043953068f39e9ec15754e225` |

## Implementierte Quellgrenzen

- Die zwölf `arc_history_*`-Spenderdateien wurden gezielt übernommen; der
  bestehende Parser, Profilmapper, Sidebar-/SessionBridge-Runtime, Format 3,
  Splits, Archiv und M155 BrowserTestRunner/TestSupport bleiben Rootmodell.
  `arc_import_recovery.{cc,h}` waren in Rootbasis bytegleich zum Donorparent;
  deren Historydelta nutzt den gemeinsamen verifizierten Backup-Leser.
- WebUI/Handler/Service halten Argument 10 (`args[9]`) als
  `separated_arc_profiles`; History ist optionales Argument 11 (`args[10]`).
  Handleraufrufe mit 8–10 Argumenten und native Defaultselections bleiben false.
  Kategorieauswahl erlaubt Sidebar oder History; getrennte Profile müssen
  tatsächlich ausgewählte Browserquellen sein. History verändert keinen
  Baumfingerprint; bestehende Profil-/Separatedwahl bleibt darin enthalten.
- `FinishJournalWrite → FinishWithSeparatedWorkspaces → HistoryFinishCommit`
  läuft genau einmal. Guard hält bis zum terminalen Reply. Reiner Verlauf
  umgeht jede Baummutierung; Sidebarreplay und Sidebar-No-op erreichen trotzdem
  History. Eine bereits abgeschlossene Sidebar wird bei Historyfehler erhalten.
- Hauptquelle wird um getrennte Quellen gefiltert. Jedes getrennte Ziel wird
  über die aktive Registry/den nativen ProfileManager geladen und benutzt
  seinen eigenen ArcService/HistoryService, Policy, Keepalive, Journal und
  Shutdown. Unverfügbares/deleting/OTR/falsches Ziel führt zum Fehler; kein
  Hauptprofil-Fallback. Gemischte Backups werden nicht ins Hauptprofil replayt:
  separate Quellen erhalten erforderlichenfalls einen frischen gefilterten
  Backup. Das verhindert Quellenübertritt auch in Prepared-Recovery.
- Owner-only/hashgeprüfter Backup inklusive DB/WAL und geprüfter SHM-Manifest-
  Einträge; SHM wird nach akzeptiertem Vertrag neu gebildet. Keine SQLite-
  Lesung der laufenden Arcdatei. HTTP(S), Credentials, Schema-, Größen-, Mengen-
  und Zeitfensterfilter bleiben. Import-UI/Journale melden ausschließlich
  Zählwerte/Hashes; Arc-AddPages-Fehlerlogs enthalten keine URLs/Titel. Keine
  Sync-/Keychain-/CloudKit-Nutzung, keine echten Profile verwendet.
- Eine HistoryDBTask pro Zielbatch. M155-Barriere bestätigt den tatsächlichen
  Singletoncommit und Nachfolgetransaktion, ohne transaction_nesting oder
  zusätzliche SQLtransaktion. Idempotenz benutzt exaktes URL/Visit-Time-Paar.
  Rücknahme löscht nur neue IDs (auch wenn der Source-Insert fehlte) auf
  bestehenden URLzeilen und ausschließlich neu erzeugte URLs über DeleteURLs.
  Vorherige IDs/Quellen/URLzeile und entfernte Source-Zeilen werden geprüft;
  unbestätigte Durabilität/Rücknahme bleibt Prepared/RecoveryRequired.
- Profilbezogene History-Prepared-Recovery geschieht vor Veröffentlichung einer
  Vorschau im Hauptprofil und bereits vorhandenen getrennten Zielen. Policy
  wird je Ziel geprüft; ohne Historymarker blockiert disabled History keine
  reine Sidebarvorschau. Journalfehler bleiben fail-closed. Kein Zurücksetzen
  von Layoutwahl allein wegen Historyrecovery.

**Offene Zeitentscheidung:** Runner und bisheriger Spender bleiben unverändert
bei `not_after=now+Days(1)`. Das ist ausdrücklich **keine** bestätigte neue
Produktentscheidung. Root muss `>now` reject gegenüber 24h-Toleranz bestätigen
und den Plan/Vertrag passend binden; Anfrage `bc8b9223` bleibt ohne Antwort.
Diese Grenze ist vor Kandidatenabnahme zu lösen, nötige Korrektur in derselben
Workeridentität. Weder Datum noch Enum/API wurde geraten.

## Commits, Dateiumfang und Abhängigkeiten

Basis: `0c6466980ef95be77c1b5d61d721ca7b1766e747`.
Donor: `28deb7986a1939ce9d23ed1d2eccbf67fedbef03` (ungebaut/unabgenommen).
Produktquellen: **`346edf636ce764885f8125c3e1c9abbf4ae8aeab`**, Parent
`79acf1adfc74a7a3109e0c07796034c050db7fc6`, 34 Dateien,
3682 hinzugefügte/111 entfernte Zeilen. DCO, `Lane: desktop`.
Vorherige attributable Dokumentcommits, ausschließlich dieser Bericht:
`ad7463fe93c7642782f922724f24127af44466a7` und
`79acf1adfc74a7a3109e0c07796034c050db7fc6`. Dieser finale Bericht erhält einen
separaten Dokumentcommit; dessen SHA steht in der direkten nativen Übergabe.
Root kann die vier Commits in dieser Reihenfolge übernehmen; kein Branchmerge
mit alter Serie, kein ours/theirs, kein Zurücksetzen neuerer Rootquellen.

Vollständige eigene Dateiliste (Sourcecommit plus dieser Bericht):

```text
overlay/chromium/src/ahoi/browser/importer/arc/BUILD.gn
overlay/chromium/src/ahoi/browser/importer/arc/DEPS
overlay/chromium/src/ahoi/browser/importer/arc/README.md
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_import_runner.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_import_runner.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_journal.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_journal.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_journal_unittest.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_reader.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_reader.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_reader_unittest.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_unittest_support.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_writer.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_writer.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_history_writer_unittest.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_commit_support.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_handler.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_recovery.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_recovery.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_browsertest.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_factory.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_history.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_internal.h
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_runtime.cc
overlay/chromium/src/ahoi/browser/importer/arc/arc_import_service_separated.cc
overlay/chromium/src/chrome/browser/resources/settings/people_page/ahoi_arc_import_section.html.ts
overlay/chromium/src/chrome/browser/resources/settings/people_page/ahoi_arc_import_section.ts
overlay/chromium/src/chrome/test/data/webui/settings/ahoi_arc_import_section_test.ts
patches/chromium/0084-ahoi-arc-imported-visit-source.patch
patches/chromium/README.md
patches/chromium/series
tests/repository/test_ahoi_settings_page_contract.py
docs/reviews/2026-10-07-arc-history-m155-handoff.md
```

Keine GN-/DEPS-/Settings-Datei außerhalb des konkret korrigierten Scopes
geändert. Zusätzliche deklarierte GN-Abhängigkeiten sind vorhandene pinned
History-/Profile-/Keepalive-/SQL-/Testtargets; keine neue Infrastruktur oder
Produktabhängigkeit. Keine neue Harnessplattform oder Journey-Shell angelegt.
0084 betrifft exakt die fünf ursprünglichen Vendorpfade und nur dessen
additive Registrierung; Roots 0095/0096/0097 und aktuelle 0001-Crestseams bleiben
bei Rootintegration erhalten. Keine Rootcheckpoints/Mobile-/Crestquellen,
Goals, Account-/Routing-/Reserveeinstellungen geändert. Sharedcheckout/out
wurden nicht verwendet. Root muss seinen tatsächlichen Checkout-Pin vor dem
Apply belegen; die gelesene Primärquelle allein beweist den gemeinsamen
Checkoutzustand nicht.

## Tatsächlich ausgeführte Quellenprüfungen

Alle auf eigenem Worktree/privaten, entsorgbaren Pin-Kopien, keine Compilation:

- `python3 -m unittest discover -s tests/repository -p test_ahoi_settings_page_contract.py -q`:
  **12/12 PASS**; vorhandener Settingscheck um Argument-/Guard-/Zielgrenzen
  ergänzt. Früherer Lauf 11/11 vor der Ergänzung, abschließend 12/12.
- `python3 -m unittest discover -s tests/repository -p test_product_patch_stack.py -q`:
  **12/12 PASS**, aktive Patchregistrierung/Ledger/M155-Sourceverträge.
- `python3 tools/source_line_budget.py --chromium-src /private/tmp/ahoi-arc-port-wsadbizp/vendor`:
  **PASS, 1727 Sourcefiles, Maximum 800**; eigener overlay und lokale
  Ahoi-Patchmaterialisierung. `vendor` enthält keinen Live-Ahoi-Checkout.
  Browserregression 784, WebUI-Test 718, Historyservice 375 Zeilen.
- Im privaten `patch-base`: `git apply --check --whitespace=error-all <own-worktree>/patches/chromium/0084-ahoi-arc-imported-visit-source.patch`:
  **PASS** auf den fünf gepinnten Vendorfiles; XMLbasis enthält ausschließlich
  0001s bekannten AhoiTabTree-Tag. Kein tatsächlicher Stackapply oder Build.
- Python-Snippet mit `ast.parse` für DEPS/Settingscheck und ElementTree für
  gepatchtes SQL-XML: **PASS**. GN-Quellenlisten auf vorhandene/unique Dateien,
  erlaubte Pfade, UTF-8/LF, genau einmal 0084 und fünf Vendorpfade: **PASS**.
  Kein GN-Parser-/C++-/TS-Compilerpass behauptet; gn/tsc/clang-format waren
  im PATH nicht verfügbar. Native Compiler-/GN-/TS-Auflösung ist Roots nächstes Gate.
- `rg` über Overlay/Patches/Repositorytests/Desktopjourneys: sämtliche
  `ahoiArcCommit`-Aufrufer und die Abschlusskette gelesen. Testaufrufer um
  Argument11 ergänzt; bestehender arg10-Separated-Test erhält die Liste.
- Vollständiger eigener Source-/Donordiff auf Scope, Daten-/Profilgrenzen,
  minimale vorhandene Abhängigkeiten und Budget geprüft; keine Fremddiffs
  einbezogen. `git diff --cached --check` und Basis→Sourcecommit `--check` PASS.
- `python3 tools/check_dco.py --base 0c6466980ef95be77c1b5d61d721ca7b1766e747 --head 346edf636ce764885f8125c3e1c9abbf4ae8aeab`:
  **PASS für 3 Nicht-Merge-Commits**. `python3 tools/check_lane_boundaries.py --all --since 0c6466980ef95be77c1b5d61d721ca7b1766e747`:
  **0 errors/0 notes**. Finale Dokumentrevision wird vor Übergabe ebenso geprüft.
  Historischer DCO-Aufruf mit `--head HEAD` war ein CLI-Fehler und wurde auf
  tatsächlichen 40-Zeichen-SHA korrigiert; kein Produktbefund.

Vorbereitete, **nicht ausgeführte native Regressionen**: Reader-Sicherheit,
WAL/Hash/Owner-Backup, Journalprivacy/Preparedzustände, Writer-Pairidempotenz,
Baseline-URL-/Visit-ID-/Referrer-Erhalt, fehlender Source-Insert, unbestätigter
Commit/Recovery, zwei isolierte HistoryServices und public Commit für reinen
Verlauf/kombinierten Import/Sidebarreplay. WebUI ergänzt Zählwerte, reinen
Verlauf, Failure neben abgeschlossener Sidebar und unverändertes arg10.
Diese Quellenfälle ersetzen keine tatsächlich sichtbare Abnahme.

**Stop und Handback:** aktive Quellphase endet mit der einmaligen konkreten
Quellenübergabe dieses Stands an Root. `cockpit_report` wurde bereits bei der
ersten Gatephase genau einmal mit status abbruch aufgerufen und quittiert;
kein erneuter Report. Wiederaufnahme/Findingkorrekturen benutzen denselben
Handoff, Session/Thread/Goal und Worktree. Keine eigene schwere Phase ohne
konkreten Rootslot/Ownership-Handback. Warten auf neue Befunde/Lieferbelege,
keine unveränderten Statusabfragen, zusätzlichen Audits oder Ersatzworker.

## Native Blockadequittung nach Quellenübergabe

Die konkrete Quellenübergabe wurde als Rootnotiz `dc8d6d5e` aufgenommen:
Source `346edf636ce764885f8125c3e1c9abbf4ae8aeab`, Berichtstand
`52fc0c13d80c5833e41fe17722cb1a4f1a2a241b`, Prüfungen und offene Rootgates.
Danach wurden keine Produktquellen geändert und kein zweiter cockpit_report
aufgerufen. Der erste automatische Goalturn bestätigte die aktive Rootsession
und wartete 60 Sekunden ereignisgesteuert ohne neues Feedback. Der zweite
prüfte nur den konkreten Integrations-/Handbackstand: kanonischer Root-HEAD
`e23919641fd129d012652c3c269dfbc10c1952c7`; keine Historycommits/-Änderungen
an den relevanten Pfaden, Checkpoint fordert History weiterhin vor F.
Kein Zeitentscheid, Finding, Build-/GUI-Handback oder Lieferbeleg eingegangen.

Seit dem Quellstopp besteht über Übergabeturn und beide Goalfortsetzungen
dieselbe vollständige Abhängigkeit: alle verbleibenden Aktionen benötigen
Rootfeedback beziehungsweise Roots exklusiven Kandidaten-/Lieferweg. Keine
nützliche unabhängige autorisierte Arbeit bleibt. `update_goal(blocked)`
quittiert denselben Thread `01a113e8-cfc5-79b1-9a36-71e8e45f3a93`, dieselbe
unveränderte Objective, `createdAt=1791335405`, **status blocked**, aktualisiert
`1791338089`. Nur dieses eigene Goal geändert; keine neue Session, kein
Ersatzworker und keine Änderung am Root-/fremden Goal.

Nächster Schritt bleibt Roots konkrete Zeitentscheidung/Sourcebefund oder
Kandidatenhandback an dieselbe Identität, anschließend die betroffene Korrektur
beziehungsweise Abnahmebelege. Keine zusätzliche Quellphase, Prüfung oder
Statusschleife während der Blockade. Dieser Dokumentnachtrag hält ausschließlich
die einmalig neue native Quittung und den bestehenden Aufgabenstand fest.

## Wiederaufnahme und belegter Stand am 8. Oktober

Nutzer setzt dasselbe Goal erneut active; `get_goal` bestätigt denselben
Thread `01a113e8-cfc5-79b1-9a36-71e8e45f3a93`, `createdAt=1791335405`,
`updatedAt=1791452440`, status active. Keine neue Session/Zieleingabe und kein
Umwidmen. Kanonischer Root ist aktuell `e41ef67eb4305a0f9becf36207ca3293a3182a9c`.
Root hat Source `346edf63` als **`850aa5876e9171af2d760281d79fe3017a63c8b7`**
übernommen; Quellannahme/primäre M155-Barriere in
`865a95860f295e40afbfc4a870d09079d54c1670` dokumentiert. Die vollständige
konkrete Zeitkorrektur **`6eefc4a8d192830fc78ffe521f05958fbe302002`** wurde
als Gitdiff gelesen: Runner `not_after=now`, Readerfenster ebenso und bestehender
Zukunftsfall bei now+1h. Kein weiterer Workerzeitfix nötig. Historische offene
Zeitfragen oben beschreiben den ursprünglichen Übergabestand, nicht ein noch
offenes Produktgate. Keine neuen Vendor-/Runtimebelege daraus abgeleitet.

Nach der Konsolidierung fehlte der eigene Worktree tatsächlich; Cockpit zeigte
unverändert dieselbe Session im Rootcheckout. Notizen `0cb804e7` / `6abc76f2`
(Root/Konsolidierungsowner) und `62715269` (erneute Blockadequittung) benennen
diesen Übergang; keine Antwort/Freigabe daraus behauptet. Die jetzt angebotene
`cockpit_note_status`-API verweigert die alten achtstelligen Kennungen als
`INVALID_ARGUMENT: request_id`; Zustellung bleibt damit nicht nachgewiesen.
Kein erneutes Senden dieser Altanfragen oder Duplicateworker.

Der ursprüngliche Userauftrag weist denselben Pfad/Branch ausdrücklich zu und
verlangt dessen Erhalt für Findings. Deshalb wurde ausschließlich dieser
Arbeitsbaum aus dem unverändert erhaltenen eigenen Ref **e834b4e1** wieder
hergestellt: Path fehlend/nicht symlinked, Branch-HEAD exakt
`e834b4e105235553989dc4f6c60674684e60f7ce`, kein anderes registriertes Worktree
auf diesem Branch; `git worktree add <ursprünglicher Pfad>
cockpit/arc-verlauf-m155-e5fbc1f3` erfolgreich. `pwd`, HEAD, Branch und sauberer
Status danach bestätigt. Kein neuer Branch/Goal/Thread, keine Produktquelle,
kein Rootcheckout-Reset und keine schwere Phase. Nur Git-Worktreeverwaltung
und die Wiederherstellung des bereits zugewiesenen Arbeitsbaums. Geltendes
AGENTS am wiederhergestellten Pfad gelesen; sämtliche eigenen Dateiarbeiten
verwenden diesen CWD ausdrücklich. Die Cockpit-/Native-Session-CWD-Einstellung
wurde nicht geändert; ihr Root-Fallback bleibt von den tatsächlichen
Worktree-Prozess-CWD-Belegen getrennt.

`main` und lokal gespeichertes `origin/main` stehen inzwischen auf
`3c4d1006bca6634b7d92a6ce0d225a4eaa7b293d`. Beide Historycommits 850aa/6eef sind
**keine** Vorfahren von main (`git merge-base --is-ancestor`, jeweils exit 1).
Das ist keine Mainintegration dieses Ports und kein tatsächlicher Remote-
Push-/Lieferbeleg. Der Rootcheckpoint nennt F-Runner weiterhin NOT_STARTED;
kein neuer konkret zurechenbarer Build-, UI-, Native-Review- oder
Desktopauslieferungsbeleg ist hier vorhanden. Kein Goalerfüllen daraus.

Neue materielle Koordinationsnachricht: Worktree wieder vorhanden, Root-
Sourceannahme/Zeitregel erledigt, unabhängige Quellenphase bleibt beendet;
benötigt werden konkrete Rootbefunde beziehungsweise Kandidaten- und
Lieferquittungen. Stabile neue Nachrichten-ID `f3367dcc-82f8-4ce0-ac99-007350970e5e`, nur für dieses
neue Ereignis; kein weiterer cockpit_report. Berichtnachtrag ausschließlich
im eigenen Worktree, DCO/Lane/Diff/Dokumentreferenzen vor Übergabe geprüft.
Keine C++-/TS-/GN-/Runtimeprüfungen wiederholt, keine Rootcheckpoints verändert.

### Dauerhafte Orchestratorquittung für den neuen Stand

Die neue materielle Restore-Notiz wurde als `593ea9c5` aufgenommen. Für den
mitgegebenen vollständigen UUID `f3367dcc-82f8-4ce0-ac99-007350970e5e` liefert
`cockpit_note_status` mit vollständiger Quellbindung **REQUEST_NOT_FOUND: no
receipt for this caller**. Aufnahme der Legacy-Notiz ist damit keine belegte
Receiverzustellung; nichts identisch erneut gesendet.

Diese neue präzise Kommunikationsgrenze wurde über den vorgesehenen bestehenden
Orchestratorweg als Informations-Follow-up übergeben: Request
**`a1e5eb2c-a68d-4b13-a924-6a524df2f8a4`**, `acceptedAt/updatedAt=
2026-10-08T10:05:55Z`, **phase queued**. Quittung bestätigt getrennt Caller-
Account `defaa5b4-a7fe-4558-9214-cafead98d4c5`, dieselbe eigene Session/Thread
und den registrierten Root-CWD. Inhalt bindet den wiederhergestellten ursprünglichen
Worktree, Commit d70bfb43, Rootannahme850aa/Zeitfix6eef, ausstehende F-/main-/
Push-/Lieferbelege und ausdrücklich keinen neuen Worker/Goal/Quellenhandoff.
Nach einmaligem ereignisgesteuertem 60-Sekunden-Warten blieb derselbe Request
queued; keine Antwort, Ausführung oder Rootabnahme daraus behauptet. Keine
unveränderten neuen Anfragen und kein weiterer cockpit_report. Nächste
Wiederaufnahme auf neue Antwort/Befund/Lieferquittung derselben Identität.

### Erneute native Blockadequittung nach Wiederherstellung

Nach dem Wiederherstellungsturn und zwei automatischen Fortsetzungen ist keine
neue Antwort, kein Finding und kein Kandidaten-/Lieferhandback eingegangen.
Derselbe konkrete Orchestratorhandle `a1e5eb2c-a68d-4b13-a924-6a524df2f8a4`
bleibt unverändert queued; kein terminaler Fehler oder Abbruch behauptet.
Own Worktree bf6c0314 war sauber. Die Worktree-Abhängigkeit ist gelöst; alle
verbleibenden notwendigen Schritte sind Roots exklusiver F-Build-/GUI-/Review-/
Integrations-/Lieferweg. Eine neue Quellphase oder ein eigener schwerer Lauf ist
nicht freigegeben; keine unabhängige autorisierte Arbeit bleibt.

`update_goal(blocked)` quittiert denselben eigenen Thread/Objective und
createdAt1791335405, **status blocked**, updatedAt1791454445. Kein fremdes Goal
und keine Account-/Session-/CWD-Konfiguration geändert. Anfrage/Worktree/Branch
und volle Zielsetzung bleiben für echte Rootbefunde erhalten. Keine neue
Anfrage, kein weiterer cockpit_report und keine unveränderte Statusschleife.
Nur dieser neue native Status wird im vorhandenen Aufgabenstand festgehalten.

## Kandidatenabnahme und Abschlussgrenze

Root baut den kombinierten F-Kandidaten auf dem bestehenden guarded Weg.
Sichtbare Abnahme auf genau demselben Kandidaten, mit synthetischen geschützten
Backupquellen und tatsächlich aufgerufenem Produktweg:

1. Reiner Verlauf: Sidebar abgewählt, korrekte Zählwerte, kein Baumimport.
2. Kombiniert: Sidebarjournal abgeschlossen, danach History; Historyfehler
   erhält den erfolgreichen Sidebarimport.
3. Wiederholung und Sidebar-No-op: keine doppelten Visits; History kann später
   unabhängig vom bereits importierten Baum ausgeführt werden.
4. Getrennte Ziele: jede Arcquelle ausschließlich im ausgewählten Zielprofil,
   Policy-/Nichtverfügbarkeitsfehler ohne Hauptprofil-Fallback.
5. Exakte Rücknahme: vorhandene URL-/Visitdaten vor/nach vergleichen; fremde
   Daten einschließlich desselben Zeitstempels bleiben erhalten.
6. Prepared-Recovery: Unterbrechung an DB-/Journalgrenzen, Neustart/Discovery;
   tatsächliche Markerzustände und Idempotenz prüfen.

Danach notwendige fokussierte/native History-/Arc-/WebUIfälle und ein separates
natives `codex review` im kleinsten attributable Scope; höchstens zwei
begründete Runden. Fixtures/Quellenprüfungen ersetzen weder Kompilierung noch
sichtbare Produktabnahme. Findings gehen an dieselbe Workeridentität zurück.
Root liefert integrierten SHA, echten Push-/Remote-Ref-Beleg, ausgelieferten
SHA/Artefaktprovenienz und relevante Laufzeitbelege. Quellenübergabe ist keine
vollständige Goalerfüllung.

## Erweiterte Nutzerfreigabe: Ende der Root-Warteschleife

Der Nutzer erteilt ausdrücklich sämtliche Freigaben zum End-to-End-Abschluss.
Die vorherige Source-only/Root-Handback-Stopgrenze ist aufgehoben. Derselbe
Worker führt den vorhandenen kombinierten F über Build, sichtbare Historyreise,
fokussierte Checks/native Review und guarded Integration/Lieferung fort.
Root/C70 erhalten Ownershipnotizen fb4df9ad/f5f45d7b; fremde aktive Jobs bleiben
geschützt. Tatsächliche Inhouseadmission am 8. Oktober: CPU5.02/6.39/8.80 %,
Mittel6.74 %, memory_pressure68 % frei, Disk57908072KiB frei. Ältere Blockaden
werden nicht ungeprüft weitergeführt. Vorhandener Kandidat3c370175 ist bereits
signiert/installiert; seine echten Compilekorrekturen cstring_view/raw_ptr und
Zeitregel werden übernommen. Kein neuer Worker/Goal/Account oder Ersatzprodukt.

## Tatsächliche End-to-End-Fortsetzung am 8. Oktober

Nutzer widerruft die eigene Source-only/Root-Handback-Stopgrenze und erteilt
sämtliche Freigaben zum Abschluss. Kombinierter, bereits signierter/installierter
3c370175-Kandidat und neuer E2E-Anschluss a3b27385 übernommen; eigene native
Goalidentität bleibt ACTIVE. Runner55405 terminal0/sign/provenancePASS am
8. Oktober10:47:53Z, vorhandenes gemeinsames internes Output, keine neue
Chromiumkopie. Eigener Thread im Lock. Zurücknahme der SSH-Signaturfehlergrenze
nur über vorhandenen GUI-Signer, kein Adhoc-Signieren oder Guardbypass.

Erster sichtbarer Journeyversuch erreichte noch keine Importassertions: native
Neutabfläche statt Startup-Settingsroute, durch tatsächliche Page.navigate
korrigiert. Zweiter/dritter Versuch zeigt native HTML-only Quellenliste, also
Arc ohne authentifizierte Arc.app nicht angeboten. E2E-Service-Override allein
war unzureichend. cc6f76f2 verschiebt denselben owner-only/private-temp Quelle/
Target-Check in den gemeinsamen Finder; SourceUI und Service teilen ihn,
Officialbuild ignoriert den Schalter. Normale Arc-Authentifizierung, Vnode-/
Backup-/Hash-/Policy-/DBgrenzen unverändert. Keine Fake-WebUI-Antworten.

Neuer eigener Runner84444/cc6f76f2 gestartet, Sourcecompile/Link11 Schritte
pass, Signatur/Provenienz noch laufend bei dieser Notiz. Aufnahme auf Inhouse
CPU18.37/15.18/31.75 %, Mittel21.77 %,52 % Memory frei,56723876KiB frei;
4Jobs/4GiB Growth plus8GiB Reserve unverändert. Root informiert mit1dd7c8b5,
native Slots nicht durch Sessionstatus geraten. Quellen liegen ausschließlich
im eigenen Worktree; aktualisierter Targetmirror ist ein vorhandener Root-
Snapshot, nicht die kanonische Source. Live Runner/Lock wird vor jeder weiteren
Schreib-/GUIphase geprüft.

Journeyscript im ursprünglichen zugewiesenen Pfad arc-history-journey.sh:
main (history-only/kombiniert/replay), getrennte Profile, Rollback mit SQLite-
Trigger im ausschließlich geschlossenen Disposable-Ziel, Prepared aus tatsächlichem
Backup/committed-key und anschließende reale Discovery. Snapshot-/Journal-
Readback ist nativ, importUI zeigt nur Counters. Resultate bleiben NOT_PROVEN
bis Assertions und Screenshots auf genau demselben signierten Kandidaten laufen.

Aufgaben: laufenden guarded Candidate fertigstellen; sichtbare Journeys6,
gezielte native History-/Recoverychecks; getrennte OwnReview der tatsächlich
geänderten Quellen; tatsächliche Mainintegration/Push und guarded Desktop-
Lieferung. Fremde aktive Arbeit und aktuelle Rückroll-App werden erhalten.

## Aktueller nachgewiesener Stand — 8. Oktober, Quellenfeedback und Lieferung

Dieselbe eigene Goalidentität ist ACTIVE (get_goal: createdAt1791335405,
Thread01a113e8-cfc5-79b1-9a36-71e8e45f3a93). Keine neuen Goals/Worker,
Account-/Routing-/Berechtigungsänderungen und kein zweiter cockpit_report.
Eigene Shell-CWDs liegen im ursprünglichen e5fbc1f3-Worktree; die registrierte
Cockpit-CWD bleibt Root und wird nicht als Worktree-Konfigurationsbeleg umgedeutet.

### Exakter gebauter und installierter Kandidat

Source **6f06daf0ca0e79091a2fc48f6bd79bb838588149**, Basis des Nachtrags
**3c370175712beb90ec414bfd89b29a39d502fd15**. Derselbe kombinierte Full-F
enthält Root850aa587/6eefc4a8 und dessen reale M155-Compilekorrekturen.
Runner38766 terminal0 am11:30:13Z; Build/Signatur/Provenienz abgeschlossen.
Chromium16c3e554/155.0.8059.26, V8-Pin unverändert. Keine neue Vendorquelle.

- Executable SHA256 `bdd229591271f8d33e7b462f99b543a0b4afca213164b8d06049a80a4a6f463f`.
- Bundle tree SHA256 `1e38b13f0136e16c6e77c98d6041d7d8889887276190357257d5430fb663fb91`.
- GN args SHA256 `a33bdac560626059dc6a0f74be6edc76a2bb370bf5dd4891a4dfad2aace76ab4`.

Belege im [Kandidatenordner](../../artifacts/tests/desktop-arc-history-final-6f06daf0-20261008/):
focused-build-provenance.json, returned-bundle-verification.log,
installed-dev-6f06daf0.json, installation.log und installed-smoke.json.
527 portable Dylibs/238 Frameworkresources überprüft; native Signatur erhalten.
`install-dev-app.py` bestätigt gleichvolumige hashgeprüfte Stagecopy, vorherige
Kandidatenprüfung, quieszente App, renameatx_np(RENAME_SWAP), tatsächlichen
Postinstallreadback und automatische Rücknahme bei Verifikationsfehler.
Dev-App tatsächlich `/Applications/AhoiBrowser.app`, Source6f, Startup-Smoke
Chrome155.0.8059.26 in eigenem temporärem Profil bestanden; nur eigener PID beendet.
Vorherige funktionierende EF-Rückroll-App bleibt erhalten. Root8d00751f bestätigt
Source/Executable-Hash/deep-strict Signatur separat. Inhouse installierte Root3c
bleibt erhalten. Dies ist **Entwicklung/nightly**, releaseEvidenceEligible=false;
keine notarisierten Release-/Master-DoD-Belege behauptet. Die versehentlich
verwendete release-only verify-installed-app.sh meldete fehlende Release-Team-
Voraussetzung; der richtige Dev-Installer und verify-built-app.sh bestehen.

### Sichtbare native Historyabnahme und fokussierte Checks

Vier reale Journeys am unveränderten signierten 6f-Appbundle, jeweils eigene
owner-only synthetische geschlossene Arcquelle und native Settings-Steuerung,
Actual-Ahoi-Front-/Keyboardreceiver, Screenshots, native DB-/Journalreadbacks:

| Belegordner | Tatsächliches Ergebnis |
| --- | --- |
| visible-main-foreground-final | reiner Verlauf2 Visits ohne Baumänderung; kombiniert Sidebarimport+History noChanges; Wiederholung Sidebar-No-op mit Historyselected ohne doppelte Visits |
| visible-separated-final | main1 Visit, isoliertes Zielprofil1 Visit, korrekte registry/profile_dir und getrennte native HistoryServices |
| visible-rollback-final | echter post-write Fehler über bestehenden guarded native Writerhook; vollständiger URL/Visit/VisitSource-Digest unverändert; vorheriges committed Journal erhalten |
| visible-recovery-final | Prepared-Journal mit echtem Backup/Manifest/Contentkey; tatsächliche Discovery-Recovery committed; vorhandene URL/Visit/Source-Daten unverändert |

UI-Prüfungen bestätigen ausschließlich Zählwerte, keine History-URLs/Titel.
Keine WebUI-Antworten gemockt, keine echte Arc-SQLite/Profile/Keychain/CloudKit
verwendet. Danach compiled ArcHistory*-Checks in history-focused.log/xml:
**20/20 bestanden**, darunter Idempotenz, exakte Rücknahme/fehlende Sourcerows,
getrennte Services, WAL-Backup/Privacy und unbekannte Commit-/Recoverygrenzen.

### Native Review und konkrete Korrekturen

Separates natives `codex review --base 3c370175...` mit bestehendem Account/
CODEX_HOME. Erste gestartete Reviewrunde belegt reproduzierte Symlinkmutation,
aber keinen terminalen Gesamtverdict; eine vorherige unzulässige CLI-
Promptkombination startete keine Review. Fix2002efcf/53a8ff3a validiert Pfade.
Zweite tatsächliche Runde PID70104 terminal0, native-review-round2.log/.exit:
P1 ungebundener CDP-Port, P2 harte Links in mutierbaren Dateien, P2 fehlendes
Runnerverdict. **bbb4bb49** korrigiert alle drei konkreten Stellen: Portrefusal,
Listener-PID/Profilbindung vor CDP, Single-Link-Dateien, verdict.json erst nach
UI- und nativen Assertions. Rootb5cc825e hat diese Sourcekorrekturen geprüft.
Keine dritte Modellreview. Maximal zwei tatsächliche Runden bleiben eingehalten.

Reale Negativprüfungen am aktuellen Skript: eigene temporäre Symlink- und
Hardlink-Fallen werden vor Mutation abgelehnt (exit1, Hash extern unverändert),
ein eigener tatsächlicher TCP-Listener bewirkt occupied-port refusal(exit6)
vor Browserstart. review-boundary-results.json bindet Ergebnisse; Bashsyntax
und zwölf bestehende Settings-Quellenprüfungen bestehen. Quellenbudget:
`python3 tools/source_line_budget.py` **1732 Dateien, Maximum800, PASS**.
`git diff --check` besteht. Frühere Aufrufe mit dem nicht existierenden Namen
check_source_budget.py beziehungsweise zsh-unmatched tools/check_source* liefen
nicht; daraus kein Quellencheckpass behauptet.

Betroffene sichtbare Reruns nach Skriptkorrektur bleiben offen. Frühere Versuche
sind nicht überschrieben: Cachemode/RunningChromeVersion-Symlink-Setup repariert;
eine now+1h-Fixture wurde nach langem Ablauf korrekt importierbar, deshalb ist
nur die künstliche Zukunftsfixture auf9Tage gesetzt (native >now-Regel bleibt).
Letzter Rerun während Loginwindow abgebrochen; nur eigene PID90001/script89974
beendet, keine sichtbare Abnahme daraus abgeleitet. Bei aktueller erneuter
Read-only Admission ist Inhouse unlocked, CPU-Mittel53.43%, Memory69% frei,
15842348KiB verfügbar; tatsächlicher Frontowner BetterIPTV/Vega PID22716 mit
aktivem clock-probe31 PID23631 und Appium23554. Keine GUI übernommen.
Orchestratorrequest **d021a64d-126a-41ba-a16d-05a87e9d2e0c**, accepted
15:04:16Z, queued: konkreten GUI-Handback angefragt, keine Freigabe daraus behauptet.

### Minimaler Root-Nachtrag und Main-Grenze

Die produktrelevante Differenz gegenüber angenommenem Root3c besteht exakt aus
arc_import_discovery.{cc,h}, arc_import_service.cc und
arc_history_import_runner.cc; zusätzlich arc-history-journey.sh und dieser
Bericht/zugehörige Belege. Keine GN/DEPS/series/WebUI-Nachänderung nötig.
Die vier C++-Dateien in eigenem aktuellen HEAD entsprechen bytegenau gebautem6f;
Skriptfeedback verändert keine Appbytes. Validierte Disposable-Quelle/-Ziel
kann nur in !OFFICIAL_BUILD den gemeinsamen Finder und vorhandenen
fail_after_write-Hook nutzen. Normale Profil-/Backup-/Policy-/Historygrenzen bleiben.

Eigener HEAD518644dd erhält aktuelles akzeptiertes main5c9c6a5b ohne neue
Mobile-/Spike-Produktänderung. Er ist kein Root-Full-Mobile-Paket: Root besitzt
26 weitere noch nicht abgenommene Mobilepfade. Kein Rootcheckout zurückgesetzt.
Unpublizierte Kompositionsref cockpit/arc-main-verified-e5fbc1f3 /144ca55d
ist nur Vergleich, **keine Mainintegration, kein Push und kein Abnahmebeleg**;
1773 Pfade vs Main enthalten fremde noch offene Desktop-F-Abnahme.
Rootb5cc825e/8d00751f verlangt den kleinen Arc-Nachtrag gegen3c und erhält
ihn in derselben Identität. Tatsächliches Main/Serverreadback bleibt
**5c9c6a5bff56d8c231dc946389dc82b290a2cae9**; gemeinsamer Publisher ist Root.
Nächster Schritt: tatsächlicher GUI-Handback, betroffene sichtbare Reruns/
Runnerverdict, Rootannahme des minimalen Nachtrags; anschließend konkrete
Main-/Push- und Desktoplieferbelege von Root. Eigenes Goal bleibt bis dahin offen.

### Zustellbare Evidenzrepräsentation und konkrete Grenzen

Vollständiger Paket-Diffcheck fand ausschließlich originale terminale
Leerzeichen in generierten Review-/Browserlogs, keinen C++-/Skriptdiffbefund.
Rohlogs sind deshalb bytegetreu gzip-komprimiert (.log.gz, mtime0), niemals
bereinigt/gekürzt. raw-log-provenance.json bindet zusätzlich SHA256/Länge der
Originalbytes; gunzip liest die oben genannten .log-Belege. Unkomprimierte
lokale Kopien bleiben Prüfmaterial, werden nicht erneut versioniert.

Browserlogs der getrennten-/Rollback-/Recoveryreise enthalten einen
DevToolsAgent-DCHECK (!associated_receiver_.is_bound) in einem Renderer,
während Settings-/Importassertions und native Endzustände bestehen. Dies ist
kein stillschweigend grünes Crashgate: Ursache/Bezug zur Debug-CDP-Navigation
ist noch nicht bestätigt und gehört in den konkreten betroffenen GUI-Rerun.
Kein Datenverlust oder Arc-Backendfehler wird daraus ohne Reproducer behauptet.

Orchestrator d021a64d antwortete15:05:33Z: tatsächlicher BetterIPTVowner
1447392B-39C3-4904-BF44-A35F43F4F8F7/Thread01a11179-6b8e-73e3-9963-ef7ac9ee1884,
jedoch ausdrücklich keine Abstimmung/Weiterleitung. Direkte Notiz496005f4 wurde
vom Helper als projektübergreifend abgelehnt, kein Empfänger erreicht.
Neue konkrete Kommunikationsgrenze mit876e32ef-2b04-4bd8-9919-d6e3604ff0cb
accepted15:10:24Z weitergegeben; queued belegt keine GUI-Freigabe.
Minimaler Root-Nachtrag76fad15e gegen3c ist noch nicht angenommen; auf dieselbe
kleine Source/Evidenzzusammensetzung mit komprimierten Rohlogs wird er präzisiert.
Kein neuer Worker, kein Defaultbranch-Schreibzugriff oder Push erfolgt.
