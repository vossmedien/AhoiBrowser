# AhoiBrowser – Zielprompt Desktop-Kernfeaturepaket

**Geltungsstand: 1. September 2026.** Dieser Zielprompt operationalisiert die aktuelle Desktop-Welle aus `AhoiBrowser-Master-Zielprompt.md`. Der Master bleibt für das vollständige Produktziel maßgeblich; bei Widersprüchen gelten die späteren ausdrücklichen Nutzerentscheidungen: kein übergroßer Onboarding-/Transfer-Wizard, AnyChat ausschließlich über Chromiums normalen Chrome-Web-Store-Pfad, uBlock Origin Classic mit genau einem Ahoi-Installationsklick bis zum normalen Chromium-Berechtigungsdialog sowie größere zusammenhängende Featurepakete vor einem neuen Build.

## Ziel

Liefere einen signierten, unter `/Applications/AhoiBrowser.app` installierten M152-Kandidaten, der:

- beim Öffnen des Erweiterungsmenüs in einem echten Null-Tab-Fenster nicht mehr abstürzt;
- Erweiterungen transaktional installiert und fehlgeschlagene beziehungsweise abgebrochene Installationen ohne Profilreste beendet;
- uBlock Origin Classic/MV2 über eine authentische, eng gepinnte Upstream-Vertrauenskette betreibt, seine Wirkung gegenüber uBlock Origin Lite/MV3 sichtbar belegt und Lite erst nach bestandenem Classic-Neustarttest entfernt;
- AnyChat über den normalen Chrome-Web-Store-Pfad installiert, Action und Side Panel bedient und den Zustand nach Neustart bestätigt;
- Arc-Daten über eine normale, kompakte Browser-Importoberfläche nach Backup und Dry Run strukturtreu übernimmt und beim zweiten identischen Lauf keine Duplikate erzeugt;
- Zen als normale Importquelle vorbereitet, nur belegte Kategorien und Schemata anbietet und unbekannte Strukturdaten nicht errät;
- Split-/Resize-Verhalten in Sidebar und WebContents aus demselben Chromium-Splitzustand ableitet und für jede Pane-Position symmetrisch bedienbar macht;
- Command Bar, Quick Window, echtes Inkognito, Navigation, Standardbrowser-/Systemübergaben, Upload/Download/PDF/Druck, lokalen Passwortmanager/Autofill und HTTP-Authentifizierung als konventionelle Browser-Kernfunktionen im installierten Kandidaten vollständig abnimmt;
- anschließend die übrigen kontrollierbaren Funktionen des ursprünglichen Masterziels weiter umsetzt, statt neue Parallelmodelle oder Produktballast einzuführen.

Arbeite selbstständig bis zum belegten Ergebnis. Frage nur bei echter externer Freigabe, Account-/Biometrieaktion, nicht ableitbarer Produktentscheidung oder irreversibler Fremdsystemmutation. Externe Blocker stoppen keine unabhängige Arbeit.

## Aktueller Tatsachenstand

- Chromium-Pin: `152.0.7977.65`, Commit `fc4d67f1788019a27e32511137ceccbd2fafdaaa`.
- Kanonischer Workspace: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`.
- Zwei frühere normale AnyChat-Web-Store-Installationen abortierten beim erfolgreichen Laden der Erweiterung, weil AnyChats benannter `Command+Shift+S`-Befehl und Ahois Sidebar-Befehl gleichzeitig den einzigen High-Priority-Accelerator beanspruchten. Ahois Browser-Shortcuts sind deshalb normale Fallthrough-Handler; AnyChat benötigt weiterhin keinen eigenen Installer oder Produktsonderpfad. Davon unabhängig existierten ein Null-Tab-Crash im nativen `ExtensionsMenuViewModel` und ein späterer Null-Tab-Split-Crash über `NewSplitTab` nach `TabStripModel::IsTabPinned(-1)`; beide gehören zu ihren generischen Null-Tab-Fixes.
- Im Ahoi-Profil sind 1Password Stable und uBlock Origin Lite/MV3 vorhanden. AnyChat und uBlock Origin Classic sind nicht persistiert.
- Die exakten IDs sind:
  - 1Password Stable: `aeblfdkhhhdcdjpifhhbdiojplfjncoa`;
  - AnyChat: `khpefodpgnkegiohbolbaaeabnfdegln`;
  - uBlock Origin Lite: `ddkjiahejlhfcafbddmgiahcphecmpfh`;
  - historische uBO-Web-Store-ID: `cjpalhdlnbpafiamejdnhcphjbkeiagm`;
  - offizielles gorhill-GitHub-CRX für Classic 1.74.0: `fkgkibajhfbepljeaefdnfnegdcjomkh`.
- Das offizielle uBO-Classic-1.74.0-CRX ist MV2 und hat SHA-256 `b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e`. Diese Identität darf nicht als historische Web-Store-ID ausgegeben werden.
- Eine lokale Arc-App und ein lokales Arc-Default-Profil sind vorhanden. Ein reales lokales Zen-Profil wurde beim Start dieser Welle nicht gefunden.

## Nicht verhandelbare Ausführungsreihenfolge

1. Sichere reproduzierbare Ausgangsevidenz, ohne private Profildaten offenzulegen.
2. Implementiere beziehungsweise vervollständige genau den kleinsten zusammenhängenden Codepfad der aktuellen Welle.
3. Materialisiere die getrackte Overlay-/Patchserie deterministisch auf dem exakten Chromium-Pin.
4. Baue und signiere den Kandidaten; ein Build ist noch kein Funktionspass.
5. Installiere ihn atomar nach `/Applications` und binde jede Evidenz an Quell-SHA, Patchserien-Fingerprint, Bundle-Hash und Installationsreceipt.
6. Führe zuerst die betroffenen sichtbaren User-Journeys mit Computer Use im installierten Bundle aus. Nutze echte Maus-/Tastaturinteraktion, Dialoge, Neustarts und zustandsabhängige Wiederholungen.
7. Sichere frische Screenshots beziehungsweise zustandsbezogene Evidenz, prüfe sie visuell und kontrolliere danach, dass kein neuer Crashreport entstanden ist.
8. Führe erst jetzt die eng fokussierten Unit-/Browser-/Integrationsprüfungen der bereits sichtbar geprüften Funktion aus.
9. Findet ein programmatischer Test einen Defekt, korrigiere, baue und installiere erneut; wiederhole zuerst den exakt betroffenen sichtbaren Flow und erst danach den Test.
10. Breite Repository-, Patchkompositions-, Regression-, Packaging- und Release-Gates laufen als letzte Teststufe.
11. Committe logisch getrennt mit DCO-Sign-off, pushe den kanonischen Branch und berichte Implementierung, installierte Runtime-Evidenz, sichtbaren E2E-Status, Programmatik und externe Grenzen getrennt.

Keine programmatische Unit-, Browser-, Integrations- oder breite Testsuite darf vor dem ersten sichtbaren Lauf der betroffenen Funktion gestartet werden. Read-only-Diagnose, statische Diff-/Formatprüfung, Build, Signierung, Installation und Crashreport-Inventar sind keine Ersatztests, aber als Vorbereitung zulässig.

## Arbeitspaket A – Erweiterungsmenü und Installationsstabilität

- Alle Seiten-, Host-, Berechtigungs-, Reload- und Action-Zugriffe im Erweiterungsmenü behandeln ein fehlendes aktives `WebContents` explizit.
- Im Null-Tab-Zustand bleiben Name, Icon und generische Verwaltung einer Erweiterung sichtbar; seitengebundene Aktionen sind verborgen oder deaktiviert.
- Wechsel zwischen Null-Tab und normalem HTTP-/HTTPS-Tab aktualisiert denselben ViewModel-Zustand ohne Neustart.
- Mehrfaches Öffnen/Schließen, `Escape`, Außenklick, Fensterwechsel und Browserneustart crashen nicht.
- Abgebrochene oder fehlgeschlagene Store-Installationen hinterlassen keine Extension-Verzeichnisse, Preferences-, Secure-Preferences-, Toolbar-, Cache- oder Update-Reste.
- Ein erfolgreicher Installationspass zählt nur, wenn erwartete ID und Version in UI und profilorientiertem Readback nach Neustart übereinstimmen.

Pflichtjourneys: `RECOVERY-MAC-15`, `EXT-11`, `EXT-14`, `EXT-15`.

## Arbeitspaket B – uBlock Origin Classic/MV2

Erhalte die selektive MV2-Ausnahme ausschließlich für ein konkret gepinntes uBO-Classic-Paket. Allgemeines MV2 bleibt deaktiviert.

- Migriere die Produktidentität bewusst von der nicht lieferbaren historischen Web-Store-ID auf die tatsächliche ID des offiziellen gorhill-GitHub-CRX, wenn und nur wenn CRX3-Schlüssel, Paket-ID, Releaseversion, vollständiger Paket-Hash und Upstream-Provenienz exakt übereinstimmen.
- Zeige in UI und Evidenz ausdrücklich `Offizieller GitHub-Release`; behaupte niemals die Chrome-Web-Store-Identität für dieses Paket.
- Pinne Version `1.74.0`, Release-Commit, SHA-256 und CRX-Key zentral. Keine Wildcards, kein entpacktes ZIP, kein Entwicklermodus und keine bloße Manifest-ID genügen.
- Ein einziger bewusster Ahoi-CTA startet den gepinnten Download, die vollständige Verifikation und den Handoff an Chromiums normalen Permission-Prompt. Vor diesem Klick sind Quelle, Version, Commit, Hash und abgeleitete ID sichtbar; zwischen Prüfen, Herunterladen und Installieren gibt es keine weiteren Ahoi-Bestätigungsstufen.
- Behalte Chromiums normalen Permission-Prompt und atomare Installation bei. Autorisierung wird erst nach finalem ID-/Version-/Hash-/Key-Readback persistiert.
- Updates benötigen mindestens gleich starke Authentizität, Monotonie, Rollback- und Downgrade-Schutz. Bis dieser Pfad geschlossen ist, bleibt ein automatisches Extension-Update deaktiviert und wird nicht als Pass behauptet; uBOs Filterlistenupdate bleibt davon getrennt.
- Installiere Classic und Lite nie gleichzeitig für den Wirksamkeitsvergleich. Nutze getrennte frische Vergleichsprofile auf demselben signierten Bundle.
- Vergleiche kontrolliert: Netzwerkblockierung, kosmetische Filter, eigene Netzwerk- und Kosmetikregel, Dashboard/Options, Listenupdate, Kaltstart/Neustart, Persistenz, Ressourcen und bekannte DNR-Grenzen.
- Entferne uBlock Origin Lite aus dem persönlichen Ahoi-Profil erst, nachdem Classic im Zielprofil sichtbar blockiert, Einstellungen öffnet und einen vollständigen Browserneustart bestanden hat. Prüfe danach, dass Lite-ID, Dateien, Toolbarzustand und Preferences sauber entfernt sind.

Pflichtjourneys: `UBO-00` bis `UBO-13`; positive und negative Ergebnisse werden strikt getrennt.

## Arbeitspaket C – browserüblicher Import für Arc und Zen

Die UI bleibt bewusst schlicht und an etablierten Browsern orientiert:

1. `Browserdaten importieren` öffnen;
2. erkannte Quelle und Profil wählen;
3. nur real verfügbare Kategorien an- oder abwählen;
4. Vorschau beziehungsweise Zusammenfassung prüfen;
5. Import bestätigen;
6. Ergebnis mit importierten, übersprungenen und nicht unterstützten Kategorien anzeigen.

Es gibt keinen eigenständigen Transfer-Center-Bereich, keine mehrseitige Marketingstrecke und kein dauerhaftes Dashboard. Sicherheit und Wiederholbarkeit liegen im Service, nicht in zusätzlicher Oberfläche.

### Arc

- Verwende den vorhandenen profile-scoped `ArcImportService`; Settings/WebUI besitzt keine Importtransaktion.
- Erkennung basiert auf realem Bundle-/Profilzustand, geöffneten Dateien und echten Prozessen. Verwaiste `Singleton*`-Artefakte sind kein Laufzeitbeleg.
- Quelle bleibt read-only. Erzeuge vor Commit einen owner-only Snapshot einschließlich konsistenter SQLite-WAL-/SHM-Dateien und ein Manifest mit Hashes.
- Parser und Snapshot sind streng größen-, tiefen-, schema-, pfad- und typbegrenzt; Symlinks, Traversal, Gerätepfade, Zyklen, Waisen und beschädigte Referenzen schlagen fail-closed fehl.
- Mappe Spaces, verschachtelte Listen, gespeicherte Tabs und valide Splits deterministisch in Ahois vorhandene Workspace-, Tree-, Tab- und Chromium-Splitdienste. Erzeuge kein zweites Datenmodell.
- Commit ist additiv und atomar. Fehler stellen den exakten Vorzustand wieder her; der zweite Lauf desselben Snapshots ist ein erklärter No-op.
- Cookies, Passwörter, Tokens, Sessions, `Secure Preferences`, Extension Storage, Native Messaging, Keychain- und Inkognito-Daten bleiben ausgeschlossen.
- Führe den echten lokalen Arc-Datenstand nach immutable Backup und Dry Run sichtbar ein; veröffentlichte Evidenz enthält keine privaten Titel oder URLs.

### Zen

- Erkenne App, `profiles.ini`, reguläre Profile, Locks und echte Prozesse. Zeige bei fehlender Installation einen normalen Nicht-gefunden-Zustand.
- Verwende vorhandene Firefox-/Chromium-Importer-Seams für sicher unterstützte Standardkategorien.
- Ein Zen-spezifischer Strukturadapter wird nur für eine nachgewiesene konkrete Version und ein dokumentiertes Schema aktiviert. Ohne dieses Wissen bleibt Strukturimport verständlich nicht verfügbar.
- Für die technische Vorbereitung gehören Capability-Modell, Adaptergrenze, sichere Discovery und realistische temporäre Fixtures zum Umfang; ein Fixture ist kein realer Zen-Importpass.
- Sobald ein Strukturadapter freigegeben wird, gelten dieselben Snapshot-, Konflikt-, Transaktions-, Rollback-, Idempotenz- und Geheimnisausschlussregeln wie bei Arc.

Pflichtjourneys: `IMPORT-ARC-01` bis `IMPORT-ARC-12` und `IMPORT-ZEN-01` bis `IMPORT-ZEN-06`.

## Arbeitspaket D – Split-/Resize-Recovery

- Sidebar und WebContents lesen und ändern denselben Chromium-Splitzustand; kein zweites Ratio- oder Layoutmodell.
- Berechne lineare Drei-Pane-Geometrie mit allen tatsächlichen Divider-Gaps, sodass gezeichnete Divider, Hit-Areas und reale WebContents-Grenzen übereinstimmen.
- Verwende `views::ResizeArea` und `views::ResizeAreaDelegate` in upstream-kompatibler Vererbungs- und Lebensdauerreihenfolge.
- Jeder Divider besitzt eine ruhige sichtbare Linie, großzügige Maus-/Trackpad-Hit-Area und zugängliche Tastatur-/Accessibility-Aktion.
- Erster, mittlerer und letzter Pane funktionieren in horizontaler und vertikaler Ausrichtung symmetrisch. Reorder, Detach, Re-Split, Resize, `Escape`, Fensterwechsel und Neustart erhalten Identität, Reihenfolge, Fokus und Ratio.
- Split-Fehler rollen vollständig zurück und hinterlassen weder Highlight, Drag-Preview noch halbe Gruppe.

Pflichtjourneys: `RECOVERY-MAC-04`, die relevanten `SPLIT-*`-Fälle und sichtbare Zwei-/Drei-/Vier-Pane-Neustartabnahme.

## Arbeitspaket E – Daily-Driver I: Navigation, Dateien und Identität

Schließe die bereits im Master definierten Standard-Browserreisen, ohne neue Ahoi-Paralleldienste, zusätzliche Datenmodelle oder einen Onboarding-Wizard:

- Command Bar und Quick Window verwenden den vorhandenen `CommandService`, das normale Profil und dieselben Tab-/Tree-Identitäten.
- Inkognito verwendet ausschließlich Chromiums `OffTheRecordProfile`; Verlauf, Restore, Sync, gespeicherte Logins und Extension-Zugriff bleiben korrekt getrennt.
- Navigation, Popups, Upload, Datei-Drop, OAuth, Passkeys, Custom Protocols, Standardbrowser und externe Links verwenden die vorhandenen Chromium- und macOS-Systempfade.
- Download, Pause, Fortsetzen, Abbruch, Warnung, Finder-Übergabe, PDF und Druck werden mit synthetischen Dateien und verifizierbaren Hashes geprüft.
- Lokaler Passwortmanager, Autofill, Passkeys, 1Password, Bitwarden und HTTP-Auth bleiben fachlich und speicherseitig getrennt.
- Basic/Digest/Proxy-Auth sowie Realm-, Port-, Pfad-, Origin-, HTTPS- und Inkognito-Grenzen werden ausschließlich mit synthetischen Credentials geprüft.
- Implementiere nur nach einem sichtbaren Defekt eine Ahoi-Abweichung; ansonsten bleibt Chromiums Upstream-Verhalten unverändert.

Pflichtjourneys: `CMD-01` bis `CMD-06`, `QUICK-01` bis `QUICK-04`, `INC-01` bis `INC-05`, `NAV-01` bis `NAV-13`, `DEFAULT-01` bis `DEFAULT-02`, `DL-01` bis `DL-08`, `PASS-01` bis `PASS-07` und `AUTH-01` bis `AUTH-27`.

`NAV-07`, `NAV-09`, `PASS-03`, `PASS-07`, `AUTH-23` und `AUTH-24` bleiben ehrlich nutzerassistiert. Die lokale Passkey-Simulation und ein Secret-Scan sind nur Teilbelege; Plattformauthentifizierung und Mehrgeräte-Negativprüfung bleiben real erforderlich. Alle davon unabhängigen sichtbaren Journeys werden vorher vollständig abgearbeitet.

## Code- und Architekturkonventionen

- Ahoi-eigene Dateien bleiben bei höchstens 800 physischen Zeilen. Extrahiere kohärente Services, Adapter, Modelle, Controller, Views oder Testhilfen.
- Ahoi-Logik lebt bevorzugt im Overlay und in eigenen GN-Targets. Chromium-Dateien erhalten nur kleine, dokumentierte Integrationsseams; keine großflächige Upstream-Reformatierung.
- Nutze vorhandene Chromium-Dienste, `ProfileKeyedService`, `BrowserContextKeyedService`, `TabStripModel`, `SplitViewService`, `ExtensionRegistry`, `CrxInstaller` und Importer-Seams. Erzeuge keine zweite Extension-, Tab-, Split-, History- oder Profildatenbank.
- Keine C++-Exceptions oder RTTI-Sonderwege. Nutze explizite Ergebnis-/Fehlertypen, `base::expected` beziehungsweise Chromium-übliche Callbacks, `WeakPtr` für asynchrone UI-Rückwege und Sequence-/Thread-Checks an Zustandsgrenzen.
- Kein blockierendes Datei-, Hash-, JSON- oder SQLite-I/O auf dem UI-Thread. Verwende eng begrenzte `SequencedTaskRunner`-Arbeit mit `MayBlock` und übertrage nur validierte, eigentumsgeklärte Daten zurück.
- Besitzverhältnisse sind explizit; keine ungesicherten rohen owning Pointer, keine Callback-Nutzung nach View-/Profile-Zerstörung und keine verdeckten Singleton-Lebenszeiten.
- Pfade werden kanonisiert und gegen erlaubte Roots geprüft. Snapshot-Verzeichnisse sind `0700`, Dateien `0600`; Symlinks und Traversal werden vor dem Öffnen abgewiesen.
- UI-Texte sind DE/EN lokalisiert, tastatur- und screenreader-tauglich. Disabled-Zustände nennen den Grund.
- Keine Secrets, realen Profildaten, privaten URLs/Titel, Cookies, Tokens, Auth-Header oder Vault-Inhalte in Logs, Screenshots, Crashreports, Testfixtures oder Commits.
- Jede Codeänderung referenziert eine bestehende oder neue Test-ID. Tests prüfen Vertrag und Zustandsübergang, nicht interne Zufallsdetails.
- Verwende `apply_patch` für gezielte Änderungen, bewahre fremde Worktree-Änderungen und integriere parallele Arbeit erst nach exaktem Diff-/Patchaudit.
- DCO-Sign-off, kleine logische Commits und eine deterministisch anwendbare Patchserie sind Pflicht.

## Sichtbare E2E-Reihenfolge

### Kandidat 1 – Crash-Recovery

1. Installierten signierten Kandidaten starten.
2. Echten Null-Tab-Zustand herstellen.
3. Extensions-Icon mehrfach öffnen/schließen; generische Einträge und deaktivierte Seitenaktionen prüfen.
4. HTTP-/HTTPS-Tab öffnen; Menü ohne Neustart erneut prüfen.
5. Browser komplett beenden und neu starten; beide Zustände wiederholen.
6. Neue Crashreports und Profilreste kontrollieren.

### Kandidat 2 – Extensions

1. AnyChat-Installation abbrechen, kontrolliert fehlschlagen lassen und danach erfolgreich durchführen; jeweils UI, Profilzustand und Neustart prüfen.
2. uBO-Baseline, Lite und Classic in getrennten frischen Profilen auf derselben Fixture vergleichen.
3. Nur bei bestandenem Classic-Pass Lite im Zielprofil deinstallieren und Cleanup sichtbar/read-only bestätigen.
4. Fremdes MV2, manipulierten Hash, falschen Key, Downgrade und fehlende Trust-Konfiguration fail-closed prüfen.

### Kandidat 3 – Import und Split

1. Browserübliche Importoberfläche öffnen; Arc-Quelle, Profil und Kategorien prüfen.
2. Dry Run des echten Arc-Profils ausführen, dann bewusst importieren; sichtbare Workspaces, Ordner, Tabs und rekonstruierbare Splits prüfen.
3. Browser neu starten und denselben Snapshot erneut importieren; No-op belegen.
4. Zen-Nicht-gefunden-/Capability-Zustand und temporäres Fixture prüfen, ohne realen Zen-Pass zu behaupten.
5. Zwei-, Drei- und Vier-Pane-Splits in beiden Orientierungen erstellen, Divider sichtbar und per Accessibility bedienen, Pane-Reorder/Detach/Abbruch und Neustart prüfen.

### Kandidat 4 – Daily Driver

1. Command Bar und Quick Window vollständig per Tastatur, bei inaktiver App und mit Übergabe in einen normalen Tab prüfen.
2. Normales und echtes Inkognito-Fenster mit Cookies, Login, Extension-Freigabe, Crash und Neustart gegeneinander prüfen.
3. Navigation, Popups, Upload, Datei-Drop, PDF/Druck, OAuth, Passkey, Custom Protocol, Standardbrowser und externe Linkübergabe sichtbar bedienen.
4. Download-Lebenszyklus einschließlich Pause, Netzverlust, Fortsetzen, Warnung, Abbruch, Finder und Hashprüfung durchführen.
5. Webformular-Passwörter, Mehrkontoauswahl, Autofill-/Passkey-Abgrenzung und vollständige HTTP-Auth-Reise mit synthetischen Daten prüfen.
6. Browser neu starten, Zustände und Ausschlüsse kontrollieren sowie Crashreport-Differenz und redigierte Evidenz sichern.

Jeder sichtbare Lauf erhält Ausgangszustand, exakte Schritte, Resultat, Screenshot-/Zustandsevidenz, App-/Bundle-Hash, Profiltyp und Crashreport-Differenz. Ein Screenshot allein ist kein E2E-Pass.

## Nachgelagerte Programmatik

Erst nach dem sichtbaren Pass der jeweiligen Kandidatenwelle:

- fokussierter Browser-Test für das Null-Tab-`ExtensionsMenuViewModel`;
- Ahoi Extension Policy/UI/Authorization/Package-Verifier-Tests und gezielte Chromium-Extension-Browsertests;
- Arc Discovery/Snapshot/Parser/Transaction/Split-Runtime- und WebUI-Tests;
- Zen Discovery/Capability-/Fixture-Tests;
- Split-Layout-, Resize-Hit-Test-, Accessibility- und Reorder-Tests;
- fokussierte CommandService-, Quick-Window-, OTR-/Inkognito-, Navigation-, Download-, Passwortmanager-, Autofill-, Passkey- und HTTP-Auth-Tests erst nach dem jeweiligen sichtbaren Daily-Driver-Lauf;
- anschließend Repositoryvertrag, vollständige Patchkomposition gegen frischen Pin, relevante Chromium-Regressionsziele, Packaging/Signierung/Updater-Smoke und breite lokale Gates.

GitHub-hosted CI mit null ausgeführten Schritten wegen Billing ist keine Codeaussage. Nutze lokale Gates und vorhandene Self-hosted-Infrastruktur; fordere keine bezahlte Actions-Freischaltung an.

## Definition of Done

Das Paket ist erst abgeschlossen, wenn:

- der installierte, signierte Kandidat an Quell-SHA, Patchserien-Fingerprint und Bundle-Hash gebunden ist;
- der Originalcrash sichtbar nicht mehr reproduzierbar ist und kein neuer passender Crashreport entsteht;
- Classic-uBO authentisch identifiziert, real wirksam und neustartfest ist oder der exakt verbleibende externe Trust-/Updateblocker ohne Sicherheitsabsenkung ausgewiesen ist;
- Lite nach bestandenem Classic-Pass vollständig entfernt ist; andernfalls bleibt Lite bewusst aktiv und wird nicht als Classic bezeichnet;
- AnyChat mit richtiger Store-ID, Berechtigungen, Action/Side Panel, Neustart und Cleanup-Verträgen sichtbar geprüft ist;
- echte Arc-Daten nach Backup/Dry Run sichtbar importiert und beim zweiten Lauf nicht dupliziert wurden;
- Zen korrekt erkannt beziehungsweise ehrlich als nicht vorhanden ausgewiesen und der sichere Adapterpfad vorbereitet ist;
- Split-/Resize-Flows sichtbar und anschließend programmatisch bestanden sind;
- alle 72 Daily-Driver-Verträge eine ehrliche Disposition besitzen; die 66 selbstständig sichtbaren Journeys sind gelaufen und `NAV-07`, `NAV-09`, `PASS-03`, `PASS-07`, `AUTH-23` sowie `AUTH-24` werden ohne die jeweils reale Plattform-, Mehrgeräte-, Nutzer- oder Systemauthentifizierung nicht als bestanden ausgegeben;
- alle kontrollierbaren fokussierten und breiten Gates grün sind;
- keine privaten Browserdaten oder Secrets in Evidenz oder Git gelangt sind;
- logische, signierte Commits auf dem kanonischen Branch gepusht sind;
- Implementierung, installierte Runtime, sichtbarer E2E, Programmatik, menschlich assistierte Schritte und externe Blocker getrennt berichtet werden.

Nach Abschluss dieser Welle wird der nächste noch offene kontrollierbare Block aus `AhoiBrowser-Master-Zielprompt.md` übernommen. Ein neues Produktfeature wird nur ergänzt, wenn das ursprüngliche Ziel es nicht bereits abdeckt oder der Nutzer es ausdrücklich verlangt.
