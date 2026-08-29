# AhoiBrowser Mobile: iOS-/iPadOS-Audit und Arc-Abgleich

Stand: 30. August 2026  
Kandidat: `codex/ahoi-mobile-completion-20260829`  
Auditprofil: frische Simulatorinstallation mit `-AhoiUITestFixture`

## Evidenzgrenze

Die Bilder in diesem Ordner stammen aus dem in dieser Welle neu gebauten und
installierten Kandidaten. Sie belegen sichtbare Simulatorzustaende, aber weder
ein physisches Geraet noch Default-Browser-Entitlement, reale CloudKit-
Replikation, Signierung, TestFlight oder Release.

Der Arc-Abgleich ist ein Produktprinzip-/Funktionsvergleich anhand aktueller
offizieller Arc-Quellen. Er ist kein pixelgenauer visueller Clone-Vergleich:
Arc wurde in dieser Welle nicht als lokale Referenz-App aufgenommen. Ahoi bleibt
eine eigenstaendige Designsprache und uebernimmt nur belegbar sinnvolle mobile
Interaktionsprinzipien.

## Durchlauf: iPhone

1. Kaltstart mit Fixture und sichtbarer Website: `iphone/01-start-fixture.jpeg`.
2. Adress-/Befehlssuche oeffnen: `iphone/02-command-search.jpeg`.
3. Tab-/Workspace-Uebersicht oeffnen: `iphone/03-tabs-workspaces.jpeg`.
4. Privates Tab anlegen und privaten Startzustand pruefen:
   `iphone/04-private-mode.jpeg`.
5. Browseraktionen oeffnen: `iphone/05-more-menu.jpeg`.
6. Mediathek/Workspaces und Geraete-Tabs oeffnen:
   `iphone/06-library-workspaces.jpeg`.

## Durchlauf: iPad

1. Kaltstart mit Fixture: `ipad/01-start-fixture.png`.
2. Adaptive Sidebar oeffnen und Vollgeraetaufnahme sichern:
   `ipad/03-sidebar-open-device.png`.
3. Befehlssuche mit Softwaretastatur oeffnen:
   `ipad/04-command-search.png`.

## Sichtbarer Nachlauf nach dem P0-Slice

1. Gruppiertes iPhone-Control-Center mit vier Schnellaktionen:
   `iphone/07-actions-after.png`.
2. Untere Ahoi-, Browser- und Datenschutzsektionen:
   `iphone/08-actions-data-after.png`.
3. Ruhiger privater Start auf Ink/Graphite:
   `iphone/09-private-after.png`.
4. Page-grosse iPad-Befehlssuche mit allen Ergebnisgruppen:
   `ipad/05-command-search-after.png`.

Alle vier Zustande wurden nach dem finalen Debug-Build neu installiert und
sichtbar inspiziert. Der Accessibility-Baum enthaelt die Schnellaktionen,
Seitenaktionen, Einstellungen und die bestaetigungspflichtige Website-Daten-
Aktion mit stabilen Identifikatoren.

## Befunde vor dem P0-Slice

| Prio | Befund | Evidenz | Wirkung | Entscheidung |
| --- | --- | --- | --- | --- |
| P0 | Das lange native Pop-up-Menue ist abgeschnitten und versteckt wesentliche Aktionen unter einer unruhigen, scrollenden Liste. | `iphone/05-more-menu.jpeg` | Schlechte Auffindbarkeit; destruktive und alltaegliche Aktionen konkurrieren. | Durch gruppiertes Browser-Control-Center mit Schnellaktionen und klaren Sektionen ersetzen. |
| P0 | Die iPad-Befehlssuche erscheint als kleine, stark durchscheinende Form ueber einer grossen Tastatur. | `ipad/04-command-search.png` | Ergebnisse, Typen und Details haben zu wenig ruhige Flaeche. | Auf Regular Width mindestens 620 x 560 pt anfordern; iPhone-Detents beibehalten. |
| P1 | Der private Startzustand ist hell und magenta-lastig. | `iphone/04-private-mode.jpeg` | Privat ist unterscheidbar, wirkt aber nicht ruhig/geschuetzt. | Privaten Start und App-Switcher-Schutz auf Ink/Graphite mit begrenztem Lavendel-Akzent umstellen. |
| P1 | Die iPhone-Tabansicht nutzt bei kleinem Bestand sehr viel leere Flaeche und flache Zeilen. | `iphone/03-tabs-workspaces.jpeg` | Schwache Tab-/Workspace-Hierarchie, skaliert visuell noch nicht. | In naechster visueller Welle kompakte Workspace-Gruppen und Tab-Karten vergleichen; kein P0-Umbau ohne groesseren Bestandsflow. |
| P1 | Sidebar und Mediathek zeigen den aktuellen mobilen Tab auch als Geraete-Tab. | `ipad/03-sidebar-open-device.png`, `iphone/06-library-workspaces.jpeg` | Kann wie eine Dublette wirken; Herkunft ist nicht sofort klar. | Geraet, Aktualitaet und lokale/remote Herkunft deutlicher beschriften; Self-Device-Regel mit realem Sync-Bestand pruefen. |

## Was von Arc sinnvoll uebernommen wird

- Suche/Adresse als primaerer, daumennaher Einstieg und als lokale
  Befehlssuche fuer offene Tabs, Verlauf und Ahoi-Bibliothek.
- Website-Flaeche vor Browserchrome; kompakte Bottom-Bar und klare,
  stufenweise Browseraktionen.
- Swipe-/Pull-Interaktionen nur dort, wo sie nicht mit Web-Navigation oder
  Accessibility kollidieren.
- Tabs und Spaces/Workspaces als mobile Organisationsschicht statt einer
  verkleinerten Desktop-Tabstrip-Kopie.
- Website-Tint als dezenter Kontext, Reader/Translate/Handoff und
  plattformnative Medienfunktionen als spaetere, messbare Ausbaustufen.

## Was bewusst nicht uebernommen wird

- kein Browse-for-Me-, Call-Arc- oder anderer KI-Zwang im Browserkern;
- kein stilles automatisches Archivieren ohne explizite, reversible Ahoi-Regel;
- kein breit eingebauter Content-Blocker ohne eigene Regel-, Update-,
  Kompatibilitaets- und Datenschutzstrategie;
- keine Kontopflicht fuer lokales Browsing und keine Zertifikatsumgehung;
- keine pixelgenaue Arc-Kopie, keine fremden Markenassets oder Begriffe.

## Desktop-zu-Mobile-Migrationsmatrix

| Desktop-Funktion | Mobile-Entscheidung | Prioritaet | Mobile Form |
| --- | --- | --- | --- |
| Workspaces, Ordner, Saved Pages | Migrieren | P0/P1 | iPhone-Mediathek, iPad-Sidebar, stabile IDs und konfliktfeste Reihenfolge |
| Normale Tabs und Verlauf | Migrieren | P0 | verschluesselte, bidirektionale Publikation; private Daten strikt ausgeschlossen |
| `Link an Mac`, Fokus, bestaetigtes Schliessen | Migrieren | P1 | signierte Zielgeraete-Aktionen mit klarer Bestaetigung und Fehlerstatus |
| Cmd+T/Command-Palette | Uebersetzen | P0 | daumennahe Adress-/Befehlssuche; Hardwaretastaturkuerzel auf iPad |
| Sidebar | Uebersetzen | P0 | Sheet/Mediathek auf iPhone, kollabierbare persistente Sidebar auf iPad |
| Tab-Rename, Save, Move, Duplicate, Find, Textzoom, Desktop Site | Migrieren | P0/P1 | gruppierte Actions, Swipe-/Context-Actions mit Undo wo passend |
| Zwei Seiten nebeneinander | Spaeter pruefen | P2 | hoechstens zwei speichersichere WebKit-Seiten auf iPad |
| Drei-/Vier-Pane-Splits, DevTools, Extensions, Header-/CSP-/CORS-Editor | Nicht als v1-Kopie migrieren | Nicht v1 | Desktop-Funktion bleibt Desktop; nur klarer Plattformaequivalent bei echtem Bedarf |

## Accessibility- und QA-Grenze

Die sichtbaren Zustande wurden mit Simulator-Screenshots und dem verfuegbaren
Accessibility-Baum inspiziert. Das ist kein vollstaendiger VoiceOver-Pass.
Vor einem Release sind VoiceOver auf echtem iPhone/iPad, Dynamic Type bis zu
Accessibility-Groessen, Reduce Transparency, Reduce Motion, Farbkontrast,
Switch Control, Hardwaretastatur und Pointer separat auszufuehren. Symbol-only
Controls muessen Namen/Wert/Hinweis und mindestens 44 x 44 pt Zielgroesse
behalten.

## Ausgefuehrte technische Gates

- Debug-Simulatorbuild, iPhone-Ziel: erfolgreich.
- Release-Simulatorbuild, iPad-Ziel, `arm64` und `x86_64`: erfolgreich.
- `AhoiMobileCoreTests`: 56 ausgefuehrt, 0 Fehler, 2 wegen benoetigtem
  CloudKit-Entitlement uebersprungen.
- `AhoiMobileUITests`: 3 Journeys, 0 Fehler; Fixture/Control-Center/Privat-
  Restore, Offline/Retry und unsicheres URL-Schema.
- `AhoiCloudKitSpikePackageTests`: 36 Tests, 0 Fehler.
- Swift-Package-Metadaten: `defaultLocalization` gesetzt und die falsche
  macOS-Plattformzusage entfernt; AhoiMobile ist explizit iOS 26.

## Aktuelle offizielle Arc-Quellen

- <https://arc.net/search>
- <https://resources.arc.net/hc/en-us/articles/20887042551831-Arc-for-iOS-Arc-Search>
- <https://resources.arc.net/hc/en-us/articles/20272860828823-Arc-Sync>
- <https://arc.net/blog/arc-search-hidden-features>
- <https://resources.arc.net/hc/en-us/articles/23528454620311-Arc-Search-for-iOS-Release-Notes>
- <https://apps.apple.com/us/app/arc-search-find-it-faster/id6472513080>

Der ausfuehrliche verbindliche Umsetzungsauftrag liegt in
`outputs/AhoiBrowser-Mobile-Zielprompt.md`.
