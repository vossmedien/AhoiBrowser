# Arc-Verlauf M155: Quellenübergabe e5fbc1f3

Status: minimale Quellportierung committet; Quellenübergabe an Root vorbereitet.
Keine Kompilierung, native Testausführung, sichtbare UI-, Review- oder
Lieferabnahme. Zukunftszeitentscheidung bleibt als konkretes Rootgate offen.
Eigenes natives Goal bleibt ACTIVE; keine native Pause/Blockade/Erfüllung behauptet.
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
   Settings-UI; Scope-/Ownerhandback erhalten. Die Zukunftszeitregel bleibt
   ausdrücklich unbestätigt, unverändert bei Donor `now+1day`.
4. **Erledigt:** gezielte Quellenchecks, Diff/DCO/Lane/Quellsyntax und
   800-Zeilen-Budget; Sourcecommit `346edf636ce764885f8125c3e1c9abbf4ae8aeab`.
   Dieser Bericht bindet Dateiliste, Vendorbelege und Prüfgrenzen daran.
5. **Wartend auf Root:** exakter kombinierter Kandidat, sichtbare Reisen,
   fokussierte/native Checks, separates Review, Integration/Push/Lieferung.
   Erst deren Belege erlauben den eigenen Goalabschluss.

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
