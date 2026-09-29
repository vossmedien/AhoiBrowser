# AhoiBrowser — Screendesign-Referenzen · 29.09.2026

## Ziel und Verbindlichkeit

Ein ruhiger, eigenständiger Browser mit zusammenhängendem milchigem Glasrahmen, vertikaler Navigation und klaren, opaken Webkarten. Desktop und iOS teilen Material, Akzent und Hierarchie. Die PNGs sind bildgenerierte Art-Direction-Referenzen, keine Produkt-Screenshots oder pixelgenauen Implementierungsvorlagen. Die folgenden Zahlen sind verbindliche Designziele; Rasterbilder können davon abweichen. SF Pro wurde im Prompt vorgegeben; eingebettete Fontmetadaten oder exakt reproduzierbare Glyphen liefert das Bildwerkzeug nicht. Keine fremden Assets wurden eingelesen. Fotografien, illustrative Wellenzeichen und Inhalte sind generiert, keine endgültige Logoentscheidung. Alle Beispieldomains enden auf `.example`.

Nur gelesen: `outputs/AhoiBrowser-Master-Zielprompt.md`, Abschnitte „Liquid Glass und Accessibility“, „Theme“, „Web-Popups als sichere Overlays“, sowie `overlay/chromium/src/ahoi/browser/ui/appearance/glass_material.h`. Es gab keine übernehmbaren Reste im Referenzordner.

## Geometrie und Raster

Alle Maße sind logische pt/DIP bei 1×, unabhängig von PNG-Pixelmaßen. Desktop-Zielraster 1440 × 900; generierte Originalauflösungen stehen in `VALIDATION.json`. Die Originale bleiben unverzerrt erhalten. iOS: Zielviewport je Telefon 393 × 852, zwei Zustände pro Referenzbild.

| Element | Vorgabe |
|---|---|
| Webkarte, Sidebar, Navigation, MiniPlayer | Radius 14 |
| Freie Panels, Command Bar, Peek-Rahmen, Dialog | Radius 18 |
| Innere Zeilen, Felder und Auswahlflächen | Radius 8, 6 Einzug im 14er-Container |
| Fenster außen | Radius 22; Ampeln 12 Durchmesser, 8 Abstand, links 20, oben 18 |
| Grundraster | 4; Abstände 8 / 12 / 16 / 24 / 32 |
| Sidebar | 236 breit; 12 Innenabstand; Zeile 36 hoch; Abschnittsabstand 24 |
| Workspace-Umschalter | 44 hoch, Name und 3 Punkte; aktive Auswahl zusätzlich Häkchen |
| Webbereich | 12 Rahmen/Gutter, 14 Kartenradius, vollständig opak |
| Schwebende Navigation | 36 hoch, 12 horizontaler Einzug; Reveal-Notch 32 × 4, Trefferfläche 44 × 24 |
| Command Bar | 640 breit, mindestens 400 hoch, 18 Radius, 16 Padding; Suche 48, Ergebnisse 44–52 hoch |
| Peek | 760 × 640 Zielgröße; rechte Aktionsleiste 96 breit, Abstand 12; Origin-Zeile 40 |
| Workspace-Dialog / HTTP Auth | 420 / 480 breit, 24 Padding, Felder 36 hoch, Buttons 32 hoch |
| Einstellungen | Inhaltsbreite bis 920; Zeilen mindestens 64, 20 horizontaler Einzug |
| MiniPlayer | Sidebarbreite minus 24, Höhe 104; Cover 40, Text 13, Fortschritt 3 |
| Schnellfenster | 760 × 600, ohne Sidebar; Webkarte 14; Übernahmeaktion außerhalb der Webseite |
| iOS | 16 Außenabstand; 12 Kartengutter; 44 × 44 Mindest-Touchziel; Safe Areas respektieren |

Split 2: zwei Spalten. Split 3: drei Spalten, unter 1200 pt eine große linke und zwei gestapelte rechte Karten. Split 4: vier Spalten nur bei genügend Breite, sonst 2×2. Mindestbreite pro Pane 280; bei Platzmangel Layoutwechsel statt abgeschnittene Bedienelemente. Jede Gruppe bleibt genau eine Sidebar-Zeile. Nur aktives Pane erhält 2-pt-Akzentkontur und steuert die dezente Seitenfarbe.

## Materialstufen

Milchaufhellung ist **keine Deckkraft**: Theme-Oberfläche Richtung Weiß mischen, hell 55 %, dunkel 12 %. Workspace-Akzent höchstens 8 % Anteil in der milchigen Basis.

| Host / Rolle | Materialvertrag aus glass_tokens |
|---|---|
| Fensterhintergrund | Ein nativer Glass-Host unter dem Chrome; Tint alpha hell 0,42 / dunkel 0,40; Foundation alpha 0,30 |
| Gedockte Sidebar | Veil alpha 0,22 auf demselben Host; kein zweiter Blur |
| MiniPlayer | Veil alpha 0,40 auf Fensterhost |
| Command Bar, eigenes Fenster | Native Panel-Tint 0,30 / 0,34; Command-Veil 0,32; kein zusätzlicher Compositor-Blur |
| Navigation, Peek-Chrome, schwebende Sidebar über Web | Compositor-Glas, Tint mindestens 0,62; Opazität bei schwierigem Untergrund erhöhen |
| Dichte Formulare | Ziel-Veil 0,86, Eingaben opak; abgeleitete Designentscheidung für Auth, kein neuer Code-Token |
| WebContents / Peek-WebContents | Alpha 1,0, kein Blur, keine Materialbeschichtung |
| Transparenz reduziert / Glass aus | Alpha 1,0 für alle Flächen, Blur 0, Refraction 0 |

Nur ein Blur pro Pixel. Für nicht-native Prototypen wäre 24-pt-Backdrop-Blur bei 110 % Sättigung ein visueller Startwert, kein zusätzlicher Effekt über nativem Glas. Sidebar, Hintergrund und umlaufender Rahmen wirken als eine Fläche. Hairline hell Weiß 45 %, dunkel Weiß 14 %, 1 Gerätepixel. Schatten: Fenster `0 16 48 rgba(10,28,34,.16)` hell / `.32` dunkel; freies Panel `0 12 32 rgba(8,25,30,.18)` / `.40`; Webkarte `0 2 8 rgba(8,25,30,.06)` / `.18`. Kein Glow um Fließtext.

## Typografie, Palette und Zustände

Browser-Chrome ausschließlich SF Pro Text / SF Pro Display, System-Font-Auflösung; keine mitgelieferten Fontdateien. Sidebar 13/18 regular 400, aktive Zeile medium 500. Gruppenlabel 11/16 semibold 600. Adresse 13/18 regular. Dialogtitel 20/25 semibold. Einstellungen H1 28/34 bold 700; Zeilentitel 14/20 medium; Hilfetext 12/17 regular. Command-Eingabe 20/28 regular; Ergebnis 14/20 medium; Herkunft 12/16 regular. iOS Large Title 34/41 bold, Body 17/22, Caption 12/16; Dynamic Type bis Accessibility-Größen mit einspaltigen Tabkarten. Editorial-Serifen im fiktiven Webinhalt sind Webseiten-Gestaltung, kein Browser-Chrome.

| Semantik | Hell | Dunkel |
|---|---|---|
| Opaker Rahmen/Fallback | #E8EFF0 | #17262C |
| Opake Web-/Formfläche | #FAFBFA | #111A1F |
| Primärtext | #142D35 | #EDF4F5 |
| Sekundärtext | #4E626A | #ADBFC5 |
| Akzent / Fokus | #006B73 | #70D6DE |
| Auswahlfläche opak | #CDE7E8 | #244A53 |
| Trennlinie opak | #BBCDD0 | #40565E |
| Fehler | #B42332 | #FF9AA4 |

Primärbutton hell: #006B73 mit #FFFFFF; dunkel: #70D6DE mit #142D35. Workspace-Punkte zusätzlich Amber #B86C16 und Violett #7961B3; niemals alleiniger Zustandsindikator. Seitenfarb-Tönung nur dezent, max. 8 %; bei hohem Kontrast aus, Nutzer-Aus bleibt erhalten. Hauptfarbe global, optional Workspace-Akzent; Appearance „System“, „Hell“, „Dunkel“.

- Ruhe: neutrale Fläche, unveränderte Textdeckung.
- Hover: 6 % Primärtext als Flächenoverlay hell, 8 % dunkel; keine Layoutverschiebung.
- Aktiv/gedrückt: 12 % Akzentoverlay; ausgewählt zusätzlich medium Text und Häkchen/Seitenmarkierung.
- Tastaturfokus: 2 pt Akzentkontur mit 2 pt Abstand; nicht durch Glass-Highlight ersetzen.
- Deaktiviert: separate gedämpfte Farbe #718187 / #75878E, keine Hoverreaktion; Grund als Hilfetext, sofern nicht offensichtlich.
- Fehler: 1 pt rote Feldkontur plus ausgeschriebene Meldung, Fokus bleibt im Formular.

## Verhalten und Accessibility

Transparenz reduzieren schlägt Glass-AN: vollständig opake Ersatzfarben, identische Maße, Reihenfolge und Funktionen, keine durchscheinende Tapete. Einstellungen zeigen „Durch Systemeinstellung reduziert“ als Status; die gespeicherte Glass-Präferenz bleibt bestehen. Kontrast erhöhen: Seitenfarb-Tönung deaktivieren, 1 Gerätepixel semantische Außenkontur auf schwebenden Flächen, Textziel 7:1; Standardziel 4,5:1 für normalen Text und 3:1 für große Schrift / Bedienelemente. Bei Glas muss die Umsetzung den tatsächlichen zusammengesetzten Hintergrund prüfen; die PNGs beweisen keinen dynamischen Kontrast.

Bewegung reduzieren: kein Zoom, Parallax, Feder- oder Refraktionseffekt; Zustandswechsel unmittelbar, optional höchstens 80 ms Überblendung. Normal: Hover 120 ms, Panel-Einblendung 180 ms ease-out mit höchstens 4 pt Versatz. Fokusreihenfolge Sidebar → Navigation → Webkarte; bei modalem Dialog Fokus einfangen, Escape schließen/abbrechen, Fokus zum Auslöser zurückgeben. VoiceOver benennt Iconaktionen, Workspace-Auswahl und Kontostatus. Auth-Passwort bleibt maskiert; Fehlermeldungen werden angekündigt.

Peek hat unveränderlich sichtbare Origin und außen rechts „Schließen“, „Als Tab öffnen“, „Split“. Auf iOS liegen diese Aktionen oberhalb der unteren Safe Area. Öffnen als Tab übernimmt denselben Inhalt ohne Reload; Split nennt bei vier belegten Panes „Vier Ansichten sind bereits geöffnet“. Sicherheitsrelevante Popups, die nicht eingebettet werden dürfen, nutzen ein separates Fenster. Schnellfenster: Escape schließt, ⌘↵ übernimmt ins Hauptfenster; Webseiteninhalt bleibt opak.

## Motive und Dateien

| Motiv | Hell | Dunkel |
|---|---|---|
| 01 Hauptfenster, Sidebar, Navigation, MiniPlayer | [01-hauptfenster-hell.png](01-hauptfenster-hell.png) | [01-hauptfenster-dunkel.png](01-hauptfenster-dunkel.png) |
| 02 Command Bar | [02-command-bar-hell.png](02-command-bar-hell.png) | [02-command-bar-dunkel.png](02-command-bar-dunkel.png) |
| 03 Link-Peek | [03-link-peek-hell.png](03-link-peek-hell.png) | [03-link-peek-dunkel.png](03-link-peek-dunkel.png) |
| 04 Split 2×2 | [04-split-2x2-hell.png](04-split-2x2-hell.png) | [04-split-2x2-dunkel.png](04-split-2x2-dunkel.png) |
| 05 Workspace-Menü und Anlegen | [05-workspace-hell.png](05-workspace-hell.png) | [05-workspace-dunkel.png](05-workspace-dunkel.png) |
| 06 HTTP-Auth mit Kontoliste | [06-http-auth-hell.png](06-http-auth-hell.png) | [06-http-auth-dunkel.png](06-http-auth-dunkel.png) |
| 07 Einstellungen › Erscheinungsbild | [07-erscheinungsbild-hell.png](07-erscheinungsbild-hell.png) | [07-erscheinungsbild-dunkel.png](07-erscheinungsbild-dunkel.png) |
| 08 iOS Home/Tab-Übersicht und Peek-Sheet | [08-ios-hell.png](08-ios-hell.png) | [08-ios-dunkel.png](08-ios-dunkel.png) |
| 09 Transparenz reduzieren | [09-transparenz-reduziert-hell.png](09-transparenz-reduziert-hell.png) | — |
| 10 Zusätzlich: Schnellfenster | [10-quick-window-hell.png](10-quick-window-hell.png) | [10-quick-window-dunkel.png](10-quick-window-dunkel.png) |

## Werkzeug, Modell und Prompts

Verwendet: eingebautes `image_gen.imagegen`, ein Aufruf pro Motiv/Variante, `transparent_background=false`. Das Werkzeug legt keinen Modellbezeichner offen; deshalb wird kein unbestätigtes Modell behauptet. Kein API-/CLI-Fallback, keine HTML-Ersatzmotive, keine externen Bild- oder Markenassets. PNG-Originale wurden aus dem Werkzeug-Ausgabeordner unverändert in diesen Ordner kopiert.

Vollständige reproduzierbare Prompttexte: [PROMPTS.json](PROMPTS.json), Felder `base`, `themes`, `motifs`. Jeder Prompt ist `base + " " + themes[hell|dunkel] + " " + motif[1]`. Für Motiv 10 stammt der Motivtext aus [QUICK_WINDOW_PROMPT.json](QUICK_WINDOW_PROMPT.json), gleiche Basis/Themes. Motiv 09 überschreibt ausdrücklich alle Glasvorgaben durch opake Flächen. Die erste Generierung hatte lediglich ein zusätzliches Leerzeichen vor LIGHT. Keine Referenzbilder oder Edit-Prompts verwendet.

## Bildgrenzen und Übergabe

Generative Abweichungen dürfen nicht unbesehen in Produktcode übertragen werden: Sidebarbreiten, Radien, Symbole und Settings-Navigation variieren zwischen Bildern; die oben definierten Werte und die vertikale Settings-Navigation sind maßgeblich. Workspace-Menü plus Dialog ist eine Zustandszusammenstellung; in der Umsetzung schließt das Menü vor Öffnen des modalen Dialogs. Einstellungen verwenden eine interne Browseradresse, nicht die generierte `kuestenwege.example`-Adresse. „Return“ auf Command-Keycaps ist als ↵ zu normalisieren; Fachbegriffe Tabs, Workspace, Split und Liquid Glass bleiben wie im Auftrag. Schnellfenster-Übernahme ist Browser-Chrome, auch wenn die generierte Fläche optisch mit der Karte verschmilzt. Keine Barrierefreiheit oder tatsächliche SF-Pro-Fontrendereigenschaft wird allein durch Rasterbilder zertifiziert.

Prüfung: jedes Bild direkt nach Generierung visuell betrachtet; deutsche Beschriftungen, Motivinhalt, Hell/Dunkel, getrennte Web-/Glasflächen und Aktionspositionen überprüft. Dateivollständigkeit, PNG-Chunk-Prüfsummen, Abmessungen, eindeutige Hashes, Tabellenlinks und Commit-Scope werden leichtgewichtig geprüft; Ergebnisse in VALIDATION.json und REVIEW.md. Kein Produktcode betroffen, daher keine Produkt-Tests/Builds/App-Starts.
