# Registry-Vorschlag 30. September 2026

Vorschlag der Desktop-Lane für den Owner von `config/test-registry.json`.
Nichts davon ist angewendet; `config/test-registry.json` und das Masterziel
wurden nicht verändert.

## Kurz

1. **`status` in der Registry bleibt `NOT_RUN`.** Die Registry ist ein
   Katalog, kein Statusspeicher: `test_registry_covers_every_master_test_id`
   in `tests/repository/test_repository.py` verlangt für **jeden** Eintrag
   `status == "NOT_RUN"`, und die Registry-IDs müssen exakt den
   ``- `ID`: Text``-Zeilen des Masterziels (plus `MOB-USER-01..15`) mit
   identischer Beschreibung entsprechen. Die Registry hat keine
   Evidenzfelder (nur `id`, `suite`, `description`, `primaryClass`,
   `requiredEvidenceClasses`, `releaseCritical`, `status`). Echte Status
   entstehen in `tools/requirement_audit.py` aus kandidatgebundener
   `result.json`-Evidenz (`tools/evidence.py`, Vokabular `PASS`, `FAIL`,
   `BLOCKED_*`, `NOT_RUN`). Ein Statuswechsel in der Registry würde den
   Test rot machen; deshalb stehen die beobachteten Status unten als
   Tabelle, nicht als Registry-Diff.
2. **22 neue Einträge**, alle `NOT_RUN`: `WS-ISO-14..22` (Handoffs 013,
   016, 018, 020, 024), `WS-MERGE-01..07`, `CMD-MOVE-01`, `MOB-FLICK-01/02`,
   `MOB-EXT-01..03` (ADR 0012). `WS-MERGE-06` und `MOB-EXT-02/03` sind nicht
   gefragt, gehören aber zum Katalog von ADR 0012 und werden mit
   aufgenommen, damit er vollständig ist. Jede neue ID braucht die
   gleichlautende Zeile im Masterziel, sonst schlägt der Test fehl; beides
   steht unten.
3. **0 geänderte Registry-Felder.** Geändert haben sich nur die
   beobachteten Status: 15 Zeilen mit **PASS**, 35 offene Zeilen
   (PARTIAL/FAIL/BLOCKED/NOT_RUN), siehe Tabelle.
4. Registry-Reihenfolge: an das Ende von `tests` anhängen (wie `WS-ISO-01..13`
   in `7401b8e`). Masterziel: Baum-/Workspace-Fälle nach `WS-ISO-13`
   (`### Tree, Tabs und Workspaces`), Mobile-Fälle nach `IOS-15`
   (`### iOS-/iPadOS-Companion und Remote Control`).

Klassenwahl: sichtbare Desktop-/Simulator-Fälle `CU_E2E`; `WS-ISO-15`
(analog `WS-ISO-08`), `WS-ISO-20` (analog `WS-ISO-12`) und `WS-MERGE-06`
(analog `SYNC-*`) `INTEGRATION`; `WS-ISO-21/22` brauchen den iCloud-Konto-
wechsel beziehungsweise echtes CloudKit des Owners und sind deshalb
`ASSISTED_E2E` (ohne `CU_E2E`, nach der Regel, die
`test_physical_or_user_authenticated_journeys_require_assistance` für die
bestehenden assistierten Fälle prüft). Beschreibungen sind aus den Handoffs
und ADR 0012 übersetzt; der Owner kann sie vor dem Anwenden umformulieren,
muss dann aber Master und Registry gleich halten.

## Beobachteter Status je ID (nicht in die Registry schreiben)

„PASS“ nur bei grünem Verdict auf dem genannten installierten Kandidaten
beziehungsweise dem genannten Simulator-Lauf. `PARTIAL` ist kein
Audit-Status; für `requirement_audit.py` zählt es als `NOT_RUN`, bis eine
vollständige `result.json` vorliegt. Alle Desktop-Kandidaten sind M153;
nach dem M154-Roll gilt jede Zeile als erneut zu belegen.

| ID | Status | Kandidat | Evidenz | Datum | Grund / Lücke |
| --- | --- | --- | --- | --- | --- |
| WS-ISO-14 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | reachableAfterRelaunch + loginKeptAfterRelaunch grün; Variante „Fenster schließen, aus Menü öffnen“ steht in ws-isolated-recovery-journey, nie gelaufen |
| WS-ISO-15 | **NOT_RUN** | — | tools/desktop_e2e/ws-isolated-recovery-journey.sh (neu) | — | Journey geschrieben, nie auf einem Kandidaten gelaufen |
| WS-ISO-16 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | mainBackAfterDelete, noChromiumProfileUi, profileAndEntryGone grün; verdict pass |
| WS-ISO-17 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | Relaunch-Teil grün; „genau ein Fenster, keine Neuladeschleife“ in ws-isolated-switch-journey, nie gelaufen |
| WS-ISO-18 | **NOT_RUN** | Quelle `372e7e33` (nicht gebaut) | OtherProfileMediaTest; ws-isolated-switch-journey | — | noch in keinem Build |
| WS-ISO-19 | **NOT_RUN** | — | ws-isolated-switch-journey mit `AHOI_E2E_CPU_LOAD=<n>` | — | nie gelaufen |
| WS-ISO-20 | **BLOCKED_ENTITLEMENT** | — | WsIso20*-Unit-Tests | — | braucht signierte CloudKit-Laufzeit; nur Unit-Tests |
| WS-ISO-21 | **BLOCKED_USER_ASSISTANCE** | Mobile `57b6ee1`/`a007aa3` | SeparatedWorkspaceSyncTests 20/0 (Simulator) | 2026-09-26 | echtes CloudKit + iCloud-Kontowechsel durch den Owner |
| WS-ISO-22 | **BLOCKED_USER_ASSISTANCE** | Mobile `57b6ee1` | Unit-Tests (Handoff 024 M3) | 2026-09-26 | echtes CloudKit Mac+iPhone durch den Owner |
| WS-MERGE-01 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-merge-journey-installed-d466bfef-20260930` | 2026-09-30 | verdict pass, alle Prüfungen true inkl. merge01PersistsAfterRelaunch |
| WS-MERGE-02 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-merge-journey-installed-d466bfef-20260930` | 2026-09-30 | merge02FlatInOrder, merge02PersistsAfterRelaunch |
| WS-MERGE-03 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-merge-journey-installed-d466bfef-20260930` | 2026-09-30 | merge03SameIdAndName/NodesBack/TabsBack/InSwitcher (auf 53 noch rot) |
| WS-MERGE-04 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-merge-journey-installed-d466bfef-20260930` | 2026-09-30 | merge04* inkl. NoCookieInTarget, DialogSaysNoUndo |
| WS-MERGE-05 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-merge-journey-installed-d466bfef-20260930` | 2026-09-30 | merge05AllOrNothing, merge05DatabaseIntact (Kill direkt nach Bestätigen) |
| WS-MERGE-06 | **NOT_RUN** | Vertrag `config/sync-format.json` | docs/ACTIVE_SYNC_COORDINATION.md (29.09.) | 2026-09-29 | nur Konformanz-/Unit-Tests; kein Zwei-Geräte-Lauf |
| WS-MERGE-07 | **PASS** | Mobile-Quelle `9e493762`, Simulator iOS 27.0 | `artifacts/computer-use/mobile/adr0012-visible-20260930/README.md` | 2026-09-30 | MobileWorkspaceMergeUITests grün; nur Simulator, kein Gerät |
| CMD-MOVE-01 | **PASS** | `efb78511` (Build 53) | `artifacts/computer-use/m153/cmd-move-journey-installed-efb78511-20260929` | 2026-09-29 | verdict pass inkl. ⌘Z-Undo; auf `47a37617` Undo noch rot; auf 54/56 nicht wiederholt |
| MOB-FLICK-01 | **PARTIAL** | Mobile-Quelle `9e493762`, Simulator | `artifacts/computer-use/mobile/adr0012-visible-20260930/README.md` | 2026-09-30 | Leisten-Flick, Vorschau, Verlauf grün; Randwischgeste zurück OPEN (expected failure, Gerätecheck nötig) |
| MOB-FLICK-02 | **PARTIAL** | Mobile-Quelle `9e493762`, Simulator | `artifacts/computer-use/mobile/adr0012-visible-20260930/README.md` | 2026-09-30 | „kein Flick bei einem Tab“ grün; VoiceOver-Aktion auf dem Simulator nicht ausführbar |
| MOB-EXT-01 | **PASS** | Mobile-Quelle `9e493762`, Simulator | `artifacts/computer-use/mobile/adr0012-visible-20260930/README.md` | 2026-09-30 | MobileWebExtensionSpikeUITests (2) grün; Spike-Umfang, nur Simulator |
| MOB-EXT-02 | **NOT_RUN** | — | — | — | v1-Schritt nach Nutzerentscheidung zum Spike |
| MOB-EXT-03 | **NOT_RUN** | — | — | — | v1-Schritt nach Nutzerentscheidung zum Spike |
| WS-ISO-01 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | plus ws-level-deletion `8cacd5ac` (levelChoiceShown, menuNamesLevel) |
| WS-ISO-02 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | 28/28, aber nur ein getrennter + ein gemeinsamer Workspace (gefordert: zwei + gemeinsam); Command-Bar-Vorschläge (`57799c08`) nicht gelaufen |
| WS-ISO-03 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | uBO je Profil grün; zweiter getrennter Workspace ungeprüft |
| WS-ISO-04 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | Übergabe per Menü grün; Tastatur/Befehlsleiste/Rahmen/Reflow in ws-isolated-switch-journey nie gelaufen; Trackpad manuell |
| WS-ISO-05 | **PARTIAL** | `964ebc78` (Build 56) | `artifacts/computer-use/m153/ws-cross-level-move-journey-installed-964ebc78-20260930` | 2026-09-30 | 24/25, verdict pass=false (undoLeavesTargetEmpty; Harness-Fix `ae355f4c` nicht nachgelaufen); Drag-and-drop manuell offen |
| WS-ISO-06 | **NOT_RUN** | — | Unit link_routing*; ws-isolated-routing-journey (neu) | — | Journey nie gelaufen |
| WS-ISO-07 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/ws-isolated-journey-installed-d466bfef-20260930` | 2026-09-30 | Löschen grün; Before-Unload Abbrechen/Bestätigen nie gelaufen |
| WS-ISO-08 | **NOT_RUN** | — | ws-isolated-recovery-journey (neu) | — | nie gelaufen |
| WS-ISO-09 | **PASS** | `880217d` (Build 33, veraltet) | `artifacts/computer-use/m153/ws-convert-journey-installed-880217d-rerun-20260926` | 2026-09-26 | 8/8; auf aktuellem Kandidaten nicht wiederholt |
| WS-ISO-10 | **NOT_RUN** | Quelle `89940ea7` (nicht gebaut) | ArcImportProfileMappingTest | — | weder gebaut noch Journey |
| WS-ISO-11 | **NOT_RUN** | — | ws-isolated-perf-journey (neu) | — | nicht gemessen |
| WS-ISO-12 | **BLOCKED_USER_ASSISTANCE** | — | Desktop sync_namespace/WsIso20-Units; iOS SeparatedWorkspaceSyncTests 20/20 | 2026-09-29 | echtes CloudKit je Profil (Owner) |
| WS-ISO-13 | **PASS** | `8cacd5ac` | `artifacts/computer-use/m153/ws-level-deletion-journey-installed-8cacd5ac-20260929 + ws-deletion-extended-journey-installed-8cacd5ac-20260929` | 2026-09-29 | beide verdict pass |
| AUTH-01..11, 13..16, 19, 21, 22, 25..27 | **PASS** | `964ebc78` (Build 56) und `d466bfef` (Build 54) | `artifacts/computer-use/m153/http-auth-journey-installed-964ebc78-20260930` | 2026-09-30 | auf beiden Kandidaten true |
| AUTH-17, AUTH-18, AUTH-20 | **FAIL** | `964ebc78` (Build 56) | `artifacts/computer-use/m153/http-auth-journey-installed-964ebc78-20260930` | 2026-09-30 | grün auf `d466bfef` (01:01), rot im neuesten Lauf 11:11 (forget_realm, auth17 no-dialog, auth18 save_after_reset, auth20 digest); instabil |
| AUTH-24 | **PARTIAL** | `964ebc78` / `d466bfef` | `artifacts/computer-use/m153/http-auth-journey-installed-d466bfef-20260930` | 2026-09-30 | nur Prompt + fail-closed Abbrechen; Anzeigen/Kopieren braucht Systemauthentifizierung (Gate password-manager-test-access); auf 56 auth24_manager_opened FAIL |
| AUTH-12 | **NOT_RUN** | — | results.json notDrivable | — | braucht HTTPS-Origin mit vertrauenswürdigem Zertifikat (Keychain-Änderung ausgeschlossen) |
| AUTH-23 | **BLOCKED_USER_ASSISTANCE** | — | results.json notDrivable | — | Mac B, CloudKit und iOS nötig |
| IMPORT-ARC-12 | **PASS** | `efb78511` (Build 53) | `artifacts/e2e/arc-import-real-20260929/ (result-53.json, result-53-repeat.json)` | 2026-09-29 | Import ok (5 Workspaces, 424 Seiten, 106 Ordner, 13 Splits), Wiederholung noChanges; sichtbare Prüfung nur im Checkpoint vermerkt, Evidenz enthält Zählwerte |
| SYNC-23 (DoD 13) | **PARTIAL** | Mac `964ebc78` (Build 56, scoped) + iPhone `7a172676` | `artifacts/sync-acceptance/real-device-20260929/receive-20260930/` | 2026-09-30 | reale Mac↔iPhone-Tab-Rundreise grün (DoD-13-Teil); Mac B fehlt, Herkunft/Filter nicht geprüft |
| SYNC-26 (DoD 13) | **PARTIAL** | wie SYNC-23 | wie SYNC-23 | 2026-09-30 | CloudKit-Geräte-Tabs ohne Google-Konto real belegt; „frisches Profil“ und kompletter Sync nicht geprüft |
| CRASH-01, CRASH-04, CRASH-07, CRASH-08 | **PASS** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/crash-recovery-journey-installed-d466bfef-20260930` | 2026-09-30 | verdict pass (auch `68006408`) |
| CRASH-02 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/crash-recovery-journey-installed-d466bfef-20260930` | 2026-09-30 | GPU-Prozess ersetzt, UI lebt; Video-Erholung nicht geprüft |
| CRASH-03 | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/crash-recovery-journey-installed-d466bfef-20260930` | 2026-09-30 | nur temporäre Tabs; gespeicherte Tabs laut Journey-Kopf nicht abgedeckt |
| UI-06 (Glass) | **PARTIAL** | `d466bfef` (Build 54) | `artifacts/computer-use/m153/glass-appearance-journey-installed-d466bfef-20260930` | 2026-09-30 | Schalter/Persistenz/Webinhalt unverändert PASS; Glas-Optik selbst = offene Owner-Sichtprüfung |
| WORKFLOW-03 (Tastenkürzel) | **PARTIAL** | `964ebc78` (Build 56) | `artifacts/computer-use/m153/keyboard-shortcuts-journey-installed-964ebc78-20260930` | 2026-09-30 | verdict pass (28/28; auf 54 commandBarShowsBinding rot); Konflikt mit Extension nicht geprüft |
| NAV-11 (⌘+Scroll) | **PARTIAL** | `47a37617` (Build 51) | `artifacts/computer-use/m153/nav-gestures-journey-installed-47a37617-20260929` | 2026-09-29 | verdict pass 8/8; Vorschau, Modalzustand, Split-Fokus nicht geprüft; auf 54/56 nicht wiederholt |
| NAV-12 (Auto-Scroll) | **PARTIAL** | `47a37617` (Build 51) | `artifacts/computer-use/m153/nav-gestures-journey-installed-47a37617-20260929` | 2026-09-29 | Hauptseite, verschachtelter Scroller, Escape grün; Klick/Tab-/Workspace-Wechsel/Fokusverlust nicht geprüft |

Build-Zuordnung: 51 `47a37617`, 52 `cf57973d`, 53 `efb78511` (29.09.),
54 `d466bfef`, 56 `964ebc78` (scoped CloudKit-Build, 30.09.); Quellen:
`docs/ACTIVE_DESKTOP_CHECKPOINT.md` (WS-ISO-Tabelle, Builds 51–53),
`docs/ACTIVE_MOBILE_CHECKPOINT.md` (ADR-0012-Sichtprüfung),
`docs/ACTIVE_SYNC_COORDINATION.md` (reale Rundreise 30.09. 10:38) und die
`verdict.json`/`results.json` der genannten Journey-Ordner.

## Neue Registry-Einträge

An `tests` in `config/test-registry.json` anhängen:

<!-- registry-entries -->
```json
[
  {
    "id": "WS-ISO-14",
    "suite": "WS-ISO",
    "description": "Einen `Vollständig getrennt`-Workspace anlegen, sein Fenster schließen, die App neu starten und ihn aus dem Workspace-Menü des Hauptfensters wieder öffnen; Verlauf und Anmeldungen sind erhalten.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-15",
    "suite": "WS-ISO",
    "description": "Prozess zwischen Profilanlage und Fenstererzeugung eines `Vollständig getrennt`-Workspaces beenden; der nächste Start hinterlässt entweder einen funktionsfähigen getrennten Workspace oder keine Spur.",
    "primaryClass": "INTEGRATION",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-16",
    "suite": "WS-ISO",
    "description": "Nur das Fenster eines `Vollständig getrennt`-Workspaces ist offen; ihn löschen: das Hauptfenster erscheint, und weder Chromiums Profilauswahl noch ein „Neues Profil“-Ablauf wird angezeigt.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-17",
    "suite": "WS-ISO",
    "description": "Vom gemeinsamen in einen `Vollständig getrennt`-Workspace wechseln, beenden und neu starten; genau ein Fenster, das zuletzt gezeigte, ist sichtbar, und der andere Workspace ist ohne Neuladeschleife über das Menü erreichbar.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-18",
    "suite": "WS-ISO",
    "description": "Im gemeinsamen Workspace Audio abspielen und in einen `Vollständig getrennt`-Workspace wechseln; der spielende Workspace ist erkennbar und lässt sich ohne Zurückwechseln pausieren.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-19",
    "suite": "WS-ISO",
    "description": "Unter CPU-Last, etwa während eines Builds, in einen `Vollständig getrennt`-Workspace wechseln, beenden und neu starten; genau ein Fenster ist sichtbar.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-20",
    "suite": "WS-ISO",
    "description": "Einen `Vollständig getrennt`-Workspace mit aktivem Sync löschen; nach der Aufbewahrungsfrist existiert die Zone `AhoiBrowserSyncV3-ws-<uuid>` nicht mehr, ihre Schlüsselversion ist stillgelegt, und der Companion hat den zugehörigen `WKWebsiteDataStore` entfernt.",
    "primaryClass": "INTEGRATION",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-21",
    "suite": "WS-ISO",
    "description": "Auf iOS in einem getrennten Workspace anmelden, das iCloud-Konto wechseln und den Übergang bestätigen; der Workspace erscheint pausiert, und nach dem Zurückwechseln ist die Anmeldung noch vorhanden.",
    "primaryClass": "ASSISTED_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "ASSISTED_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-ISO-22",
    "suite": "WS-ISO",
    "description": "Einen getrennten Workspace per Tombstone auf dem Mac stilllegen, während auf iOS eine seiner Seiten offen ist; die Seite schließt, und der `WKWebsiteDataStore` entsteht nicht neu.",
    "primaryClass": "ASSISTED_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "ASSISTED_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-01",
    "suite": "WS-MERGE",
    "description": "Gemeinsamen Workspace A mit Ordner, Split und laufendem Tab in gemeinsamen Workspace B zusammenführen; B enthält genau einen Ordner „A“, der Split bleibt intakt, der Tab läuft ohne Neuladen weiter, A ist entfernt, und nach Neustart gilt dasselbe.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-02",
    "suite": "WS-MERGE",
    "description": "Zusammenführen mit „Ohne Ordner“ hängt die Wurzelknoten von A flach und in ihrer Reihenfolge an B an.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-03",
    "suite": "WS-MERGE",
    "description": "Undo nach dem Zusammenführen stellt A mit derselben ID, demselben Namen, denselben Einstellungen und Knoten wieder her, und die Tabs kehren nach A zurück.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-04",
    "suite": "WS-MERGE",
    "description": "Workspace A mit eigenen Website-Sitzungen in gemeinsamen Workspace B zusammenführen; gespeicherte Seiten kommen in B an, offene Tabs von A werden per Before-Unload gefragt und geschlossen, Bindung und Partitionsdaten von A sind entfernt, kein Cookie aus A erscheint in B, und der Dialog sagt, dass sich das Zusammenführen nicht rückgängig machen lässt.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-05",
    "suite": "WS-MERGE",
    "description": "Ein Absturz zwischen den Verschiebungen und dem Entfernen von A hinterlässt entweder das vollständige Zusammenführen oder keine Änderung.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-06",
    "suite": "WS-MERGE",
    "description": "Gerät 1 führt A in B zusammen, während Gerät 2 offline eine Seite zu A hinzufügt; nach dem Sync beider Geräte liegt die Seite auf beiden in B, geroutet über den Tombstone mit `merged_into`.",
    "primaryClass": "INTEGRATION",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "WS-MERGE-07",
    "suite": "WS-MERGE",
    "description": "Workspaces auf Mobile zusammenführen, mit denselben Ergebnissen wie `WS-MERGE-01` bis `WS-MERGE-03`.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "CMD-MOVE-01",
    "suite": "CMD-MOVE",
    "description": "Der Befehlsleistenbefehl „In Workspace verschieben …“ verschiebt den fokussierten Ordner einschließlich seiner Kinder in den gewählten Workspace; Undo stellt den Ausgangszustand wieder her.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "MOB-FLICK-01",
    "suite": "MOB-FLICK",
    "description": "Wischgeste auf der unteren Leiste wechselt zum zuletzt genutzten Tab und zurück; die Randwischgeste über der Seite geht weiterhin im Verlauf zurück.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "MOB-FLICK-02",
    "suite": "MOB-FLICK",
    "description": "VoiceOver-Aktionen wechseln Tabs; in einem Workspace mit nur einem Tab gibt es keinen Tab-Flick.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "MOB-EXT-01",
    "suite": "MOB-EXT",
    "description": "Spike: eine gebündelte MV3-Testerweiterung injiziert per `WKWebExtensionController` ein Content Script und blockiert eine Anfrage über `declarativeNetRequest`.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "MOB-EXT-02",
    "suite": "MOB-EXT",
    "description": "Web Extension aus der Dateien-App installieren, aktivieren, deaktivieren und entfernen; im privaten Surfen bleibt sie aus, bis sie dort erlaubt wird.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  },
  {
    "id": "MOB-EXT-03",
    "suite": "MOB-EXT",
    "description": "Berechtigungsabfrage einer Web Extension pro Website; eine Ablehnung lässt die Seite unverändert.",
    "primaryClass": "CU_E2E",
    "requiredEvidenceClasses": [
      "UNIT",
      "INTEGRATION",
      "CU_E2E"
    ],
    "releaseCritical": true,
    "status": "NOT_RUN"
  }
]
```

## Neue Zeilen im Masterziel

In `outputs/AhoiBrowser-Master-Zielprompt.md` direkt nach der Zeile
``- `WS-ISO-13`: …`` einfügen:

<!-- master-tree -->
```markdown
- `WS-ISO-14`: Einen `Vollständig getrennt`-Workspace anlegen, sein Fenster schließen, die App neu starten und ihn aus dem Workspace-Menü des Hauptfensters wieder öffnen; Verlauf und Anmeldungen sind erhalten.
- `WS-ISO-15`: Prozess zwischen Profilanlage und Fenstererzeugung eines `Vollständig getrennt`-Workspaces beenden; der nächste Start hinterlässt entweder einen funktionsfähigen getrennten Workspace oder keine Spur.
- `WS-ISO-16`: Nur das Fenster eines `Vollständig getrennt`-Workspaces ist offen; ihn löschen: das Hauptfenster erscheint, und weder Chromiums Profilauswahl noch ein „Neues Profil“-Ablauf wird angezeigt.
- `WS-ISO-17`: Vom gemeinsamen in einen `Vollständig getrennt`-Workspace wechseln, beenden und neu starten; genau ein Fenster, das zuletzt gezeigte, ist sichtbar, und der andere Workspace ist ohne Neuladeschleife über das Menü erreichbar.
- `WS-ISO-18`: Im gemeinsamen Workspace Audio abspielen und in einen `Vollständig getrennt`-Workspace wechseln; der spielende Workspace ist erkennbar und lässt sich ohne Zurückwechseln pausieren.
- `WS-ISO-19`: Unter CPU-Last, etwa während eines Builds, in einen `Vollständig getrennt`-Workspace wechseln, beenden und neu starten; genau ein Fenster ist sichtbar.
- `WS-ISO-20`: Einen `Vollständig getrennt`-Workspace mit aktivem Sync löschen; nach der Aufbewahrungsfrist existiert die Zone `AhoiBrowserSyncV3-ws-<uuid>` nicht mehr, ihre Schlüsselversion ist stillgelegt, und der Companion hat den zugehörigen `WKWebsiteDataStore` entfernt.
- `WS-ISO-21`: Auf iOS in einem getrennten Workspace anmelden, das iCloud-Konto wechseln und den Übergang bestätigen; der Workspace erscheint pausiert, und nach dem Zurückwechseln ist die Anmeldung noch vorhanden.
- `WS-ISO-22`: Einen getrennten Workspace per Tombstone auf dem Mac stilllegen, während auf iOS eine seiner Seiten offen ist; die Seite schließt, und der `WKWebsiteDataStore` entsteht nicht neu.
- `WS-MERGE-01`: Gemeinsamen Workspace A mit Ordner, Split und laufendem Tab in gemeinsamen Workspace B zusammenführen; B enthält genau einen Ordner „A“, der Split bleibt intakt, der Tab läuft ohne Neuladen weiter, A ist entfernt, und nach Neustart gilt dasselbe.
- `WS-MERGE-02`: Zusammenführen mit „Ohne Ordner“ hängt die Wurzelknoten von A flach und in ihrer Reihenfolge an B an.
- `WS-MERGE-03`: Undo nach dem Zusammenführen stellt A mit derselben ID, demselben Namen, denselben Einstellungen und Knoten wieder her, und die Tabs kehren nach A zurück.
- `WS-MERGE-04`: Workspace A mit eigenen Website-Sitzungen in gemeinsamen Workspace B zusammenführen; gespeicherte Seiten kommen in B an, offene Tabs von A werden per Before-Unload gefragt und geschlossen, Bindung und Partitionsdaten von A sind entfernt, kein Cookie aus A erscheint in B, und der Dialog sagt, dass sich das Zusammenführen nicht rückgängig machen lässt.
- `WS-MERGE-05`: Ein Absturz zwischen den Verschiebungen und dem Entfernen von A hinterlässt entweder das vollständige Zusammenführen oder keine Änderung.
- `WS-MERGE-06`: Gerät 1 führt A in B zusammen, während Gerät 2 offline eine Seite zu A hinzufügt; nach dem Sync beider Geräte liegt die Seite auf beiden in B, geroutet über den Tombstone mit `merged_into`.
- `WS-MERGE-07`: Workspaces auf Mobile zusammenführen, mit denselben Ergebnissen wie `WS-MERGE-01` bis `WS-MERGE-03`.
- `CMD-MOVE-01`: Der Befehlsleistenbefehl „In Workspace verschieben …“ verschiebt den fokussierten Ordner einschließlich seiner Kinder in den gewählten Workspace; Undo stellt den Ausgangszustand wieder her.
```

Direkt nach der Zeile ``- `IOS-15`: …`` einfügen:

<!-- master-ios -->
```markdown
- `MOB-FLICK-01`: Wischgeste auf der unteren Leiste wechselt zum zuletzt genutzten Tab und zurück; die Randwischgeste über der Seite geht weiterhin im Verlauf zurück.
- `MOB-FLICK-02`: VoiceOver-Aktionen wechseln Tabs; in einem Workspace mit nur einem Tab gibt es keinen Tab-Flick.
- `MOB-EXT-01`: Spike: eine gebündelte MV3-Testerweiterung injiziert per `WKWebExtensionController` ein Content Script und blockiert eine Anfrage über `declarativeNetRequest`.
- `MOB-EXT-02`: Web Extension aus der Dateien-App installieren, aktivieren, deaktivieren und entfernen; im privaten Surfen bleibt sie aus, bis sie dort erlaubt wird.
- `MOB-EXT-03`: Berechtigungsabfrage einer Web Extension pro Website; eine Ablehnung lässt die Seite unverändert.
```

## Anwenden

Aus dem Repository-Wurzelverzeichnis, nachdem die eigenen offenen
Änderungen an der Registry gesichert sind (das Skript liest die Blöcke
oben aus dieser Datei, bricht bei schon vorhandenen IDs ab und schreibt die
Registry mit `indent=2` zurück):

```python
import json, pathlib, re, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
proposal = (root / "docs/registry-proposal-20260930.md").read_text(encoding="utf-8")
if len(sys.argv) > 2:
    proposal = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")

def block(name):
    m = re.search(rf"<!-- {name} -->\n```[a-z]*\n(.*?)\n```", proposal, re.S)
    if not m:
        raise SystemExit(f"block {name} missing")
    return m.group(1)

entries = json.loads(block("registry-entries"))
tree_lines, ios_lines = block("master-tree"), block("master-ios")

reg_path = root / "config/test-registry.json"
reg = json.loads(reg_path.read_text(encoding="utf-8"))
known = {e["id"] for e in reg["tests"]}
clash = [e["id"] for e in entries if e["id"] in known]
if clash:
    raise SystemExit(f"already in registry: {clash}")
reg["tests"].extend(entries)
reg_path.write_text(json.dumps(reg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

master_path = root / "outputs/AhoiBrowser-Master-Zielprompt.md"
master = master_path.read_text(encoding="utf-8")
for anchor_id, lines in (("WS-ISO-13", tree_lines), ("IOS-15", ios_lines)):
    m = re.search(rf"^- `{anchor_id}`: .*$", master, re.M)
    if not m:
        raise SystemExit(f"anchor {anchor_id} missing in master")
    master = master[: m.end()] + "\n" + lines + master[m.end():]
master_path.write_text(master, encoding="utf-8")
print(f"added {len(entries)} registry entries and master lines")
```

Aufruf: `python3 apply_registry_proposal.py .` (Code oben als Datei
speichern). Nur die Registry-Einträge per `jq`, ohne Masterzeilen (dann
schlägt der Test fehl, bis die Masterzeilen ebenfalls drin sind):

```sh
awk '/<!-- registry-entries -->/{f=1;next} f&&/^```json/{g=1;next} g&&/^```/{exit} g' \
  docs/registry-proposal-20260930.md > /tmp/new-entries.json
jq --slurpfile add /tmp/new-entries.json '.tests += $add[0]' \
  config/test-registry.json > /tmp/test-registry.json && mv /tmp/test-registry.json config/test-registry.json
```

`jq` formatiert die ganze Datei neu (Einrückung 2, `\u`-Escapes bleiben
UTF-8); danach `git diff --stat` prüfen.

## Danach prüfen

```sh
python3 -m unittest \
  tests.repository.test_repository.RepositoryContractTests.test_registry_covers_every_master_test_id \
  tests.repository.test_repository.RepositoryContractTests.test_physical_or_user_authenticated_journeys_require_assistance \
  tests/repository/test_requirement_audit.py \
  tests/repository/test_lean_chromium.py
```

oder vollständig `./scripts/test-repository.sh`. Beobachtete Status werden
erst durch kandidatgebundene `result.json` unter
`artifacts/e2e/<VERSION>/<ID>/` zu Audit-Status (`tools/evidence.py`).

## Validierung dieses Vorschlags

Geprüft am 30. September 2026 an einer Kopie von `config/`, `outputs/`,
`tools/`, `tests/`, `docs/`, `fixtures/` und `scripts/` im Scratchpad
(Registry-Stand `c1fbc5a9`, 433 Einträge); das Original blieb unberührt.

- Das Python-Skript oben, unverändert aus dieser Datei entnommen, hängt
  22 Einträge an (455) und fügt 22 Masterzeilen ein.
- Die `jq`-Variante ergibt eine inhaltsgleiche Registry.
- `test_registry_covers_every_master_test_id`,
  `test_physical_or_user_authenticated_journeys_require_assistance`,
  `test_split_view_contract_is_bounded_native_and_release_critical`,
  `tests/repository/test_requirement_audit.py` und
  `tests/repository/test_lean_chromium.py`: 20 Tests, OK.
- `tools/requirement_audit.py` deckt alle 455 IDs genau einmal ab, alle
  `NOT_RUN`.
- Gegenprobe: `WS-MERGE-01` mit `status: "PASS"` lässt
  `test_registry_covers_every_master_test_id` fehlschlagen. Deshalb enthält
  der Vorschlag keinen Statuswechsel in der Registry.
