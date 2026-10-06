# AHOI-M155-MULTICLOSE-20261007 – Sourcehandoff

Stand: 7. Oktober 2026. Quellcode geliefert; Kandidatenabnahme, native Review,
Standardbranch-Integration, Push und Lieferung sind noch offen. Das eigene native
Goal bleibt ACTIVE. Delegation: `c70f3d38`; keine Unterdelegation.

## Identität, Routing und Ownership

| Grenze | Tatsächlicher Beleg |
| --- | --- |
| Neue Cockpit-Session | `cockpit_sessions(cwd)` meldet `4CD99365-8552-4BE7-8FD9-CE663A46C9AA`, Rolle M155-Mehrfachschließen, Worker der Ursprungssession. |
| Neuer nativer Thread | `session_meta.id`, `CODEX_THREAD_ID` und Cockpit: `01a1134d-3995-7e41-81cd-5fc0f3b8ea57`. |
| Worktree/CWD | `pwd`, `git worktree list`, `session_meta.cwd` und Cockpit stimmen auf `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-m155-mehrfachschliessen-c70f3d38` überein. |
| Eigener Branch | `cockpit/m155-mehrfachschliessen-c70f3d38`; anfangs sauber, HEAD `9e18dfecc34e2d4b17fa8015fa36f30d533f782f`. |
| Eigenes natives Goal | Erstes `get_goal`: null; genau ein `create_goal` mit dem ausdrücklich beauftragten Ziel. Rückgabe bindet das ACTIVE Goal an den eigenen Thread, erstellt `1791325236`. Späteres `get_goal` bestätigt dieselbe Identität/Zielsetzung und ACTIVE. Kein Budget gesetzt oder Verbrauch verändert. |
| Anbieter | Cockpit: Codex; native `session_meta.model_provider`: `openai`, `originator`: `codex-tui`, CLI `0.160.1`. |
| Angewandtes Modell | Native `turn_context.model`: `gpt-6.1-sol`; Cockpit unabhängig GPT-6.1-Sol. |
| Angewandte Denkstufe | Native `turn_context.effort`: `xhigh`; Cockpit unabhängig xhigh. |
| Account | Native Umgebung `CODEX_HOME` liegt im Accountordner `DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5`. Der Cockpit-Nachrichtenbeleg `43164cc4-f21f-4239-a5e7-e8beb294e6c8` bindet `caller.accountID=defaa5b4-a7fe-4558-9214-cafead98d4c5` an diese Session, diesen Thread und CWD. |
| Caeli / Jev-Entscheid | Noch kein separater tatsächlicher Alias-/Routingentscheid-Beleg verfügbar. Anfrage unter derselben Nachrichten-ID aufgenommen, bisher `queued`; das beweist keine Routingantwort. Empfehlung aus dem Auftrag ist kein angewandter Routingbeleg. |
| Native Ausführungsregeln | `turn_context`: `approval_policy=never`, `sandbox_policy.type=danger-full-access`. |
| Root | Cockpit bestätigt Ursprungssession `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread/Goal `01a11179-cb6b-7dc1-9e53-f3d72fefc198`, kanonischen Branch und Hauptcheckout. Worker hat Root-Goal, Root-Quellen und Root-Checkpoints nicht verändert. |

Native Rollout-Quelle:
`/Users/vossmedien/Library/Application Support/Terminal Cockpit/Accounts/DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5/CodexHome/sessions/2026/10/07/rollout-2026-10-07T00-19-50-01a1134d-3995-7e41-81cd-5fc0f3b8ea57.jsonl`.
Nur Identitäts-/Konfigurationsfelder gelesen; keine Zugangsdaten gelesen oder verändert.
Ownership-Notizen `2463d131` und `6f19f3b4` wurden für die vollständige
Root-Session beim Cockpit aufgenommen; das ist keine Root-Ausführungsquittung.

## Verifizierte Basis und Fehlerbelege

- Worker- und read-only Root-HEAD bei Diagnose: `9e18dfec` vollständig wie oben.
  `git diff --quiet 1e340a17..HEAD` für Sidebar, SessionBridge, Command-Adapter,
  Chromium-Pin und MultiSelect-Journey: exit 0. Die untersuchte Grenze ist daher
  bytegleich mit Desktopbasis `1e340a17b5ff6042575dc2b2b96df2bd4603ec3a`.
- `config/chromium.json`: Chromium `155.0.8059.26`, Pin
  `16c3e55476d3564bea713314b2fff638749ce3e6`.
- Roots vorhandene `installed-1e340a17.json` bindet die Inhouse-Installation an
  diese Quellrevision und diesen Pin; Bundle-Hash
  `b2515fbbe5ea002d979d780e8c1eae0615777f80f000543f69ad9538fb647258`,
  bewachte Installation/Postinstall-Verifikation bestanden. Diese historische
  Quittung ist keine neue Live-Prüfung und keine öffentliche Release-Evidenz.
  Entwicklungsstand `ef982afe` stammt hier nur aus der Übergabe.
- Read-only gelesen im kanonischen Root:
  `artifacts/tests/accept-m155-1e340a17-20261006/multi-select/{verdict.json,steps.txt,ax-no-closed-toast.txt,browser.log}`.
  Diese ungetrackten Artefakte fehlen im Worker-Worktree und wurden nicht kopiert.
- Neun Auswahl-/Aktivitäts-/Menüheader-Gates bestanden. `steps.txt` zeigt
  Alpha/Beta/Gamma ausgewählt, `AXPress ... Offene Tabs schließen ... -> 0`,
  `closedToast=false`, `pages after: 4`, `tabsClosed=false`.
  AX danach enthält dieselben vier temporären Seiten; kein Closed-Toast.
  `browser.log` enthält keinen eigenen Close-Dispatch-Beleg; seine Start-/Signing-
  und Updater-Meldungen erklären diese Grenze nicht.

## Ursache und gemeinsamer Kommandoweg

`ShowMultiSelectionMenu()` setzt `kMultiSelection` und bietet Close-ID `1801`
bei mindestens einem aufgelösten offenen SavedPage-/Temporary-Knoten an.
`IsCommandIdEnabled()` hatte keinen Zweig für diesen Scope. Es fiel auf die
Einzelknoten-Abfrage und deren `default: false`. Somit waren die angebotenen
Multi-Aktionen deaktiviert. Chromium `SimpleMenuModel::IsEnabledAt()` delegiert
genau dorthin; RemoteCocoa serialisiert das Ergebnis als `is_enabled`.
`axtool press` gibt nur `AXUIElementPerformAction` zurück und prüft weder Enabled
noch einen wirklichen Dispatch. AX-Erfolg 0 ist daher kein Beleg für ausgeführtes
Close. Dies ist ein belegter Produktfehler am Menüvertrag, kein nachgewiesener
Modifier-/Paint-/Launcherfehler. Der tatsächliche Dispatch nach dem Fix bleibt
durch Roots sichtbaren Lauf zu belegen.

Alle direkten gemeinsamen Aufrufer wurden verfolgt:

- Runtime-Zeile/Pane: `ShowContextMenuForView()` → Shared-UUID → Multi-Menü.
- Gespeicherte Zeile sowie gespeicherter Runtime-Proxy:
  `ShowNodeContextMenu()` → dasselbe Multi-Menü.
- Native Menüausführung: `ExecuteCommand()` → `RunMultiSelectionCommand()`;
  die vorgeschalteten CrossLevelMove-/OtherProfileMedia-ID-Bereiche fangen
  `1801` nicht ab. Weitere direkte Aufrufer existieren laut `rg` nicht.
- Command-Bar-Adapter verwenden Browser-/Shortcut-/Sidebar-Move-Verträge und
  rufen dieses Multi-Close-Kommando nicht auf; dort ist keine Änderung nötig.
- Einzel-Close, Close-all-temporary, Split-Close und Archive sind benachbarte
  Schutzpfade, keine weiteren Aufrufer des Multi-Kommandos. Close-all-temporary
  nutzt bereits `GroupPageClose` und denselben jetzt geteilten Close-Lock.

Identitäten: `FindTreeNodeIdForTab()` lässt temporäre Tabs absichtlich aus;
`FindSharedTreeNodeIdForTab()` liefert deren gebundene `runtime.node_id`.
`BindTreeNodeToTab()` trägt sowohl temporäre als auch gespeicherte Seiten in
`node_tabs_` ein; `FindTabByTreeNodeId()` löst genau diese Map auf. Vor der Bindung
ist die Shared-ID null, es entsteht keine auswählbare Ersatzidentität.
Der existierende Test `SessionBridgeTest.SavesTemporaryTabAtWorkspaceRootIdempotently`
prüft den Erhalt derselben Shared-ID beim Speichern und erneuten Temporärmachen.
Der hier vorhandene Menüeintrag beweist bereits mindestens eine Live-Auflösung;
der Enabled-Fehler liegt davor, nicht an einer falschen UUID-Konvertierung.

## Minimaler Eingriff und Schutzgrenzen

Das konkrete Multi-Menü und sein Workspace-Untermenü bestimmen die erlaubten
IDs. Nur dort tatsächlich angebotene IDs werden freigegeben; ein laufender
Close-Lock deaktiviert eine weitere Close-Aktion. Die übrigen Menüscopes bleiben
unverändert. Alle angebotenen Multi-Aktionen litten an demselben Enabled-Fehler;
deren Umsetzung wird nicht erweitert.

Die alte, nun erstmals wirksame Close-Schleife würde einzeln schließen. Das
verletzt die bestehende Veto-Regel aus
[Handoff 006](../../handoffs/crest-hardening/006-batch-before-unload/HANDOFF.md).
Daher verlässt die Aktion den nativen Menüloop per bestehendem Task-/WeakPtr-
Muster und nutzt `GroupPageClose::Ask`. Ein Veto verwirft die Frage und erhält die
Auswahl; kein Tab oder Datensatz wird durch diese Aktion vorher entfernt.
Nach vollständiger Zustimmung schließt `ClosePages()` über Chromium, ohne den
Before-Unload-Dialog erneut auszulösen.

Die angeforderten Tabs werden als native WeakPtrs festgehalten.
`RegisterWillDetach(kDelete)` veranlasst eine Abschlussprüfung nach der nativen
Mutation; Fenstertransfer `kInsertIntoOtherWindow` gilt nicht als Schließen.
Toast und Auswahlfreigabe folgen erst, wenn sämtliche festgehaltenen nativen
Tab-Identitäten erloschen sind. Ein verschwundener Map-Eintrag allein wird nicht
als Close gezählt; Wiederöffnung derselben UUID verwechselt nicht die Lebenszyklen.
Die bestehende Frage bleibt bis dahin der Lock; keine Status-/Timerpollschleife.

Gemischte Auswahl: Ordner und geschlossene gespeicherte Seiten haben keinen
Live-Tab und werden nicht geschlossen oder gelöscht. Temporäre und gespeicherte
Live-Seiten verwenden dieselbe Auflösung. Native Close-Beobachter bleiben die
einzigen Schreiber für das Entfernen tatsächlich geschlossener temporärer
Knoten; gespeicherte Seiten und native Lesezeichen werden nicht gelöscht.
Splits: Die visuelle Reihenfolge besucht die einzelnen Runtime-Panes; die
ausgewählten Pane-Tabs werden geschlossen, ungewählte Mitglieder bleiben unter
Chromiums bestehenden Split-Abbau-/Fokusregeln. Kein implizites Schließen aller
Split-Mitglieder, keine neue Split-/Bookmark-/Sync-Mutation. GroupPageClose
behält seine Navigations-/Verschwinde-/Überlappungsvetos.
Keine WebContents-, Profil- oder Partitionübertragung und keine neue Abhängigkeit.

## Commit, vollständiger Diffumfang und Prüfstand

Quellcode-Commit: `d01316f73e3f1b9b69ccd072ba4707959f5fa69b`
(`fix(sidebar): enable multi-selection close and preserve group vetoes`),
`Lane: desktop`, DCO `Signed-off-by: vossmedien <christian@vossmedien.de>`.

| Datei | Umfang / Zweck |
| --- | --- |
| `overlay/chromium/src/ahoi/browser/ui/sidebar/browser_sidebar_host_menu_state.cc` | +12/−0; konkretes Multi-Menü freigeben, Close-Lock beachten. |
| `overlay/chromium/src/ahoi/browser/ui/sidebar/browser_sidebar_host_multi_select.cc` | +75/−22; vorhandene Gruppenfrage, native Abschlussidentitäten, Menüloop verlassen. |
| `overlay/chromium/src/ahoi/browser/ui/sidebar/browser_sidebar_host_view.h` | +6/−6; zwei Methoden, drei Statusfelder, Lock-Kommentar; fünf Leerzeilen entfallen zur bestehenden 800-Zeilen-Grenze. |
| `tools/desktop_e2e/multi-select-journey.sh` | +46/−0; gezielte Enabled-/Gruppenveto-/Retry-/Delta-Regression. |
| `docs/reviews/2026-10-07-m155-multiclose-fix-handoff.md` | Dieser Bericht, eigener Dokumentationscommit. |

Sourceumfang: 4 Dateien, +139/−28. Nur vereinbarte Dateien wurden geschrieben.
Vorhandene Auswahlbelege wurden gelesen; keine Modifier-/Paint-Reparatur.
CmdMove, Launcher, Sicherheitsgates, Mobile, Sync und fertige Crest-Pakete bleiben
unverändert. Keine gemeinsamen Builds, App-/GUI-Aktionen, Installation oder Push.

Tatsächlich ausgeführt:

- `git diff --check` sowie Commit-Diffcheck: PASS.
- `bash -n tools/desktop_e2e/multi-select-journey.sh`: PASS.
- Syntax der drei eingebetteten Python-Prüfungen: PASS.
- Physische Zeilen der drei geänderten C++-Dateien: 490/356/800, alle im Budget.
- `python3 tools/check_lane_boundaries.py --all --since 9e18dfec`: PASS,
  0 errors, 0 notes; DCO/Desktop-Trailer anhand des realen Commits kontrolliert.

Nicht ausgeführt: C++-Build, sichtbares E2E, native Testbinaries, vollständiges
Repository-Skript (enthält einen Swift-Build), native `codex review`.
Diese Grenzen sind Root vorbehalten; Syntax-/Diffprüfung ist keine Laufzeitabnahme.
Die neue Regression erreicht die tatsächliche native Enabled-Grenze und scheitert
bei erneut deaktiviertem Close. Anschließend erzwingt sie ein Before-Unload-Veto,
prüft alle vier verbleibenden Tabs und keinen Closed-Toast und wiederholt dieselbe
Auswahl ohne Handler. Die ursprünglichen `closedToast`-/`tabsClosed`-Gates bleiben;
zusätzlich muss ausschließlich Delta übrig bleiben. Noch kein Kandidatenresultat.
Gemischte Auswahl und Split-Abbau sind quellseitig betrachtet, hier nicht live geprüft.

## Versionsquellen

Context7 `resolve_library_id(Chromium, M155 Close/lifecycle)` scheiterte mit
`Monthly quota exceeded`; kein passender Context7-Vertrag wurde behauptet.
Offizielle Quellen wurden direkt vom unveränderlichen Pin gelesen
(Gitiles `?format=TEXT`, Base64 dekodiert; Browser-Werkzeug konnte Gitiles nicht öffnen):

- [SimpleMenuModel](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/ui/menus/simple_menu_model.cc): `IsEnabledAt` delegiert; Header bestätigt `GetIndexOfCommandId` als `optional<size_t>`.
- [RemoteCocoa-Menüadapter](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/ui/views/controls/menu/menu_runner_impl_remote_cocoa.mm): `ModelToMojo` serialisiert Enabled; `CommandActivated` ruft `ActivatedAt`.
- [TabInterface](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/components/tabs/public/tab_interface.h): native WeakPtrs und getrennte Detach-Gründe Delete/InsertIntoOtherWindow.
- [TabModel](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_model.cc) und [TabStripModel](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_strip_model.h): Close gehört Chromium und kann verzögert sein.
- [M155 Page-Protokoll](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/third_party/blink/public/devtools_protocol/domains/Page.pdl): `handleJavaScriptDialog` unterstützt Before-Unload mit bool `accept`.

## Planstand und konkreter Root-Nächsterschritt

1. Session/Basis/Ownership: belegt; Caeli-Alias/Jev-Entscheid noch offen.
2. Wirkungslose Close-Grenze, Aufrufer und Identitäten: eingegrenzt.
3. Minimaler Fix und gezielte Regression: im Sourcecommit fertig; Laufzeit offen.
4. Diff/Lane/DCO/Handoff: geprüft und geliefert.
5. Kandidat/E2E/fokussierte Checks/Review/Main/Push/Lieferung: Root-Handback offen.

Root übernimmt `d01316f7` und diesen Bericht auf seinem bestehenden Lieferweg
in genau den nächsten gemeinsamen inkrementellen Kandidaten. Nach Ownership-/
Ressourcen-/Lockprüfung bewacht bauen, signieren, Herkunft prüfen und installieren.
Zuerst `tools/desktop_e2e/multi-select-journey.sh <exakte-installierte-App> <neue-Evidenzwurzel>`
über den bestehenden GUI-/Launcherweg ausführen. Enabled, sichtbarer Before-Unload-
Dialog, erfolgreiches Veto, vollständiger Gruppenerhalt, kein falscher Toast,
erhaltene Auswahl und anschließender echter Drei-Tab-Close müssen PASS sein.
Danach notwendige fokussierte Prüfungen am selben Kandidaten und eine separate
native `codex review --commit <Roots-zurechenbarer-Integrationscommit>`;
keine Reviewkette oder Vollmatrix. Bei materieller Korrektur betroffene sichtbare
Reise erneut. CmdMove bleibt Roots eigener Wiederholung mit vorhandenem
`AHOI_E2E_ALLOW_FOREIGN_BUILD=1` nach seinen Gates vorbehalten.

Root liefert das zugeordnete Handback in diese bestehende Session/Thread:
Kandidatenrevision und Artefakt-/Provenanzpfad, sichtbare Verdict-/Schrittbelege,
fokussierte Ergebnisse, tatsächliche Reviewrevision/-umfang/-ergebnis,
Standardbranch- und Remote-SHA, geschütztes geliefertes Artefakt samt relevanter
Laufzeitabnahme sowie fehlenden Routingbeleg. Worker trägt dies hier nach und
korrigiert konkrete technische Rückmeldungen im selben Auftrag.

Aktuell sämtliche Root-Abnahme-/Integrations-/Lieferfelder: **OFFEN**.
Der Worker wartet nach genau einem `cockpit_report` ereignisgesteuert auf dieses
Handback. Fertiger Quellcode erfüllt das native Goal noch nicht.
