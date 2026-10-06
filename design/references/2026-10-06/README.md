# AhoiBrowser · Referenz-Screendesigns vom 6. Oktober 2026

Acht mit dem eingebauten Imagegen-Werkzeug erzeugte PNGs als Vorlagen für die laufende Desktop-Umsetzung auf macOS 27, jeweils in Hell und Dunkel.
Stilgrundlage sind [das UI-Konzept](../../../docs/design/AHOI_UI_CONCEPT.md), alle PNGs der [Referenzen vom 29. September](../2026-09-29/) und die [bestehenden Tokens](../../../overlay/chromium/src/ahoi/browser/ui/visual_style.h).
Beispielseiten verwenden Fantasienamen, einfache geometrische Favicons und `.example`-Domains; echte Marken, Logos und Personendaten sind ausgeschlossen.

## 01 · Tab-Switcher

[Hell](01-tab-switcher-hell.png) · [Dunkel](01-tab-switcher-dunkel.png)

Das über der Content-Card zentrierte Glass-Panel verwendet drei klar getrennte Bereiche mit insgesamt neun Vorschaukacheln und genügend Innenabstand, damit Titel, Domain und Seitenminiatur eine erkennbare Hierarchie bilden.
Der Tastaturfokus auf „Reiseplan“ verbindet Akzentumriss, dezente Füllung und Return-Symbol; die getrennte Fußzeile hält die Hinweise für Richtungstasten, Öffnen, Schließen und Escape lesbar.
Die Akzente #006B73 in Hell und #70D6DE in Dunkel folgen dem bestehenden Stil, während ausreichend dichte Glasflächen den Text vom Seiteninhalt absetzen.
Offener Kompromiss: Die Darstellung zeigt neun Einträge; die Dichte bei 18 Einträgen und das Scrollverhalten bleiben der Umsetzung vorbehalten.

## 02 · Mehrfachauswahl

[Hell](02-mehrfachauswahl-hell.png) · [Dunkel](02-mehrfachauswahl-dunkel.png)

Drei gespeicherte Tabs und ein Ordner erhalten dieselbe Kombination aus getönter Auswahlfläche, feinem Umriss und Häkchen, während der aktive temporäre Tab mit grauer Fläche und linkem Aktivitätsstrich unterscheidbar bleibt.
Das unmittelbar daneben liegende Glass-Kontextmenü ordnet seine fünf Aktionen unter „4 Einträge ausgewählt“ und trennt „Auswahl aufheben“ durch eine feine Linie ab.
Kleine Symbole, ruhige Abstände und kontrastierende Textfarben ergänzen die Akzentfarbe, sodass die Zustände auch ohne Farberkennung verständlich sind.
Offener Kompromiss: Die generierte Sidebarbreite und Zeilendichte weichen vom nominellen 236/36-Raster ab; bei der Umsetzung gelten die unten genannten Tokens.

## 03 · Bestätigungs-Toasts

[Hell](03-toast-hell.png) · [Dunkel](03-toast-dunkel.png)

Zwei kompakte Glass-Pills stehen mit kleinem Zwischenraum unten mittig in der Content-Card und bestätigen „Im Hintergrund geöffnet“ sowie „3 Tabs nach „Arbeit“ verschoben“.
Gerundete Enden, zurückhaltende Schatten, gut erkennbare Icons und ausreichend dichte Füllungen halten beide Meldungen auf der bewusst weißen Webseite lesbar, auch bei dunklem Browser-Chrome.
Die Text-Icon-Hierarchie vermeidet eine große Benachrichtigungskarte und lässt den Seiteninhalt im Vordergrund.
Offener Kompromiss: Die gleichzeitige Stapelung dient dem Vergleich; Anzeigezeit, Verdrängung weiterer Meldungen und Screenreader-Ansage bleiben der Umsetzung vorbehalten.

## 04 · Navigationszeile

[Hell](04-navigationszeile-hell.png) · [Dunkel](04-navigationszeile-dunkel.png)

Die beschriftete Vergleichstafel zeigt in beiden Themes eine eingeklappte Reveal-Notch über rein weißer und dunkler Webseite sowie darunter die ausgeklappte schwebende Navigationszeile.
Neutrale dunkle beziehungsweise helle Notch-Farben sichern ihre Erkennbarkeit, während die Maßangaben 32×4 sichtbar und 44×24 Hit-Area die kleine Darstellung vom größeren Interaktionsziel unterscheiden.
Die ausgeklappte Fläche folgt dem Radius-14- und Abstand-12-Vertrag; ihr weicher Schatten läuft an den gerundeten Ecken aus, statt einen eckigen Container zu bilden.
Offener Kompromiss: Maßlinien und gestrichelte Hit-Area-Rahmen sind erklärende Annotationen, und ihre illustrative Vergrößerung gehört nicht in die Produktoberfläche.

## Verwendete Tokens

Die Werte waren Vorgaben im Prompt; die generierten Raster sind visuelle Referenzen und kein Nachweis pixelgenauer Tokengeometrie oder gemessener Kontrastverhältnisse.
Für die Implementierung sind die Werte aus `visual_style.h` maßgeblich.

| Token | Wert / Verwendung |
| --- | --- |
| `kSidebarWidthDefault` / `kSidebarWidth` | 236 px |
| `kSidebarHorizontalInset` | 12 px |
| `kSidebarTabRowHeight` | 36 px |
| `kSidebarIconSize` | 16 px |
| `kContentCardInset` | 12 px |
| `kContentCardCornerRadius` | 14 px |
| `kPanelCornerRadius` | 18 px |
| `kDefaultAccent` | #006B73, Hell |
| `kDefaultAccentDark` | #70D6DE, Dunkel |
| `kCommandBarWidth` | 640 px, bestehender Größenbezug; keine zusätzliche Command Bar dargestellt |
| `kWorkspaceDialogWidth` | 420 px, bestehender Modalbezug; kein zusätzlicher Dialog dargestellt |
| `kDialogPadding` | 24 px, Innenabstandsbezug für das Switcher-Panel |
| `kNavigationSurfaceHorizontalInset` / `kNavigationSurfaceTopGap` | 12 px |
| `kNavigationSurfaceCornerRadius` | 14 px |
| `kNavigationRevealNotchVisualWidth` / `kNavigationRevealNotchVisualHeight` | 32×4 px |
| `kNavigationRevealNotchWidth` / `kNavigationRevealNotchHeight` | 44×24 px |
| `kContentCardShadowElevation` / `kPanelShadowElevation` / `kNavigationSurfaceShadowElevation` | 4 / 16 / 6, Orientierung für weiche Schatten |

## Erzeugung und Maße

Pro Datei wurde ein eigener Imagegen-Aufruf verwendet; zwei gezielte Imagegen-Edits verengten anschließend die helle Auswahl-Sidebar und vereinheitlichten Farbe und Text des hellen Verschiebe-Toasts, ohne handgezeichnete Ersatzbilder.
Die Ausgaben in 1586×992 px wurden mit `sips -z 900 1440` vollständig auf 1440×900 px resampelt, ohne Beschnitt oder JPEG-Zwischenstufe, und als verlustfrei kodierte PNGs gespeichert.
Die PNG-Kodierung ist verlustfrei; die Größenanpassung verändert das Pixelraster.
Alle acht Enddateien wurden mit `sips -g pixelWidth -g pixelHeight` auf exakt 1440×900 px geprüft; weitere Tests, Builds oder Installationen waren nicht Teil dieses Auftrags.

<details>
<summary>Verwendeter Imagegen-Prompt-Satz</summary>

Die folgenden gemeinsamen Vorgaben, Theme-Vorgaben und Oberflächen-Prompts bilden den verwendeten Prompt-Satz.
Die zusätzliche Koordinatenpräzisierung kam ab `02-mehrfachauswahl-dunkel.png` hinzu; die beiden spezifischen Ergänzungen für dunkle Toasts und dunkle Navigation stehen am Ende.

### Gemeinsame Vorgabe

```text
Use case: ui-mockup. Asset type: hochauflösendes Referenz-Screendesign für AhoiBrowser auf macOS 27. Erzeuge genau ein flaches, frontales PNG im Querformat, exakt 1440×900 Pixel. Keine Perspektive, kein Laptop, kein Smartphone, keine Textur auf dem Screenshot, keine Wasserzeichen. Bestehender Stil der hier gezeigten Ahoi-Referenzen vom 29.09.2026: ruhiges hochwertiges Liquid Glass, sehr dezenter türkisfarbener Hintergrund, feine Glaskanten, weiche Schatten, klare macOS-Systemschrift für das Browser-Chrome, neutrale Serifenschrift nur in Beispiel-Webseiten. KEINE echten Marken/Logos/Favicons, keine Personendaten, ausschließlich Fantasieseiten mit .example-Domains und einfachen geometrischen Symbolen. Alle UI-Texte Deutsch. Kein horizontales Tabband, keine KI-Schaltflächen.
Komposition als vollflächiges Browserfenster mit höchstens 12 px Außenraum: linke Arc-artige Seitenleiste exakt 236 px breit, 12 px horizontaler Innenabstand, 36 px hohe Tabzeilen, kleine geometrische Favicons 16 px, aktive Zeile mit sehr dezenter grauer Glasfüllung. Drei native Fensterschaltflächen oben links, darunter Workspace "Privat", Abschnitte "Gespeichert" und "Temporäre Tabs", unten "Einstellungen". Rechts opake Content-Card mit 12 px Abstand und Radius 14 px; wenig Browser-Chrome darüber. Schwebende Panels Radius 18 px. Glas nur im Browser-Chrome, Webseiten bleiben klar und opak. Bekannte Tokens: Command Bar 640 px breit, modale Standarddialoge 420 px breit mit 24 px Innenabstand. Die neue Oberfläche darf ihre eigene passende Breite haben; nicht versehentlich einen Standarddialog zeigen. Text scharf, gut lesbar, saubere deutsche Umlaute.
```

### Zusätzliche Koordinatenpräzisierung

```text
Verbindliche Layoutgeometrie bezogen auf das finale 1440×900-Bild: Sidebar von x=0 bis x=236, nicht 300 oder 320 px breit! Haupt-Content-Card beginnt bei x=248 und endet x=1428. Die Sidebar-Zeilen von x=12 bis224 und je exakt36 px hoch; 14 px Schrift, 16 px Icons. Kein skalierter Ausschnitt mit übergroßer Sidebar. Die Sidebar darf nur 16,4 % der Gesamtbreite beanspruchen. Das echte Design folgt visual_style.h, die ältere Orientierung mit22% ist hier überholt.
```

### Theme-Vorgaben

```text
Farbvariante HELL: transluzentes sehr blasses Aqua-Glas, primärer UI-Text tiefes Anthrazit #17262B, sekundär #53666C, Akzent exakt #006B73. Weiße Content-Card. Kontrast geht vor Transparenz.
Farbvariante DUNKEL: dunkles rauchiges blaugrünes Glas #152B32 bis #263C44, primärer UI-Text #F0F5F7, sekundär #B8C7CE, Akzent exakt #70D6DE. Subtile helle Glaskanten, kaum Glanz. Hintergrundwebseite dunkel, sofern die spezielle Aufgabenbeschreibung weiß verlangt bleibt deren Fläche rein weiß. Kontrast geht vor Transparenz.
```

Der erste helle Tab-Switcher verwendete zusätzlich eine opake weiße Content-Card und eine helle Beispielwebseite; seine dunkle Variante wurde mit der oben genannten dunklen Theme-Vorgabe erzeugt.

### Tab-Switcher

```text
Subject: Tastatur-Tab-Switcher auf ⌃T, zentriert über der Content-Card (also rechts der Sidebar zentriert), Glass-Overlay ca. 800×610 px mit 18 px Radius und 24 px Innenabstand, sanfter Scrim nur hinter dem Overlay. Hintergrundwebseite Fantasiename "Küstenpfad", Titel "Zeit für neue Wege.", dezentes Küstenfoto, Domain kuestenpfad.example. Sidebar darf klar sichtbar bleiben.
Panel-Header links "Tabs wechseln", darunter klein "Workspace Privat"; oben rechts kleines Keycap "⌃T". Drei klar getrennte, ruhige Abschnitte, jeweils exakt eine Reihe mit drei Kacheln. Insgesamt nur neun Einträge sichtbar. Jede Kachel hat eine kleine realistische Miniatur einer Webseite, darunter ein einfaches geometrisches Favicon, klar lesbaren Titel und kleinere .example-Domain. Abschnittsüberschriften exakt "Angeheftet/Gespeichert", "Temporäre Tabs", "Splits".
Erste Reihe: "Küstenpfad" / kuestenpfad.example / Küstenfoto; "Reiseplan" / reiseplan.example / ruhige Reiseroute; "Notizfeld" / notizfeld.example / Notizseite. Zweite Reihe: "Dünenlicht" / duenenlicht.example / Dünenfoto; "Wolkenblick" / wolkenblick.example / Wetterseite; "Leseraum" / leseraum.example / ruhige Artikelseite. Dritte Reihe: "Planung · 2 Ansichten" / kuestenpfad.example / Miniatur mit 2 Panes; "Ideen · 3 Ansichten" / notizfeld.example / Miniatur mit 3 Panes; "Überblick · 4 Ansichten" / reiseplan.example / Miniatur 2×2.
Deutlicher Tastatur-Fokus exakt auf der mittleren Kachel der ersten Reihe "Reiseplan": 2 px Akzentumriss, dezente Akzentfüllung und kleines Return-Symbol, sodass Fokus nicht nur durch Farbe vermittelt wird. Alle anderen Kacheln ruhig. Fußzeile durch feine Linie getrennt, exakt die Gruppen "↑↓ / ←→ wählen", "↵ öffnen", "W schließen", "esc", jeweils mit kleinen Keycaps und ausreichend Abstand. Keine Suche oder Command Bar zusätzlich zeigen.
```

### Mehrfachauswahl

```text
Subject: Mehrfachauswahl in linker Seitenleiste, mit geöffnetem Kontextmenü unmittelbar rechts neben der Sidebar. In "Gespeichert" exakt DREI gespeicherte Tabzeilen und EIN Ordner ausgewählt. Zeilen "Küstenpfad", "Reiseplan", "Notizfeld" mit geometrischen Favicons, sowie Ordner "Reiseideen" mit Ordnersymbol und Disclosure-Pfeil. Alle vier gleichartige Auswahlmarkierung: sanfte Akzenttönung, feiner Akzentumriss und kleines identisches quadratisches Häkchen ganz rechts. Kein fünftes Häkchen. Zeilen je 36 px hoch, keine riesigen Abstände.
Darunter Abschnitt "Temporäre Tabs": "Dünenlicht" als EIN aktiver Tab, klar UNTERSCHIEDLICH markiert: graue Glasfüllung und kleiner vertikaler Aktivitätsstrich links, kein Häkchen und kein farbiger Auswahlumriss; danach "Wolkenblick" und "Leseraum" ohne Markierung. So sind Mehrfachauswahl und aktive Seite ohne Farberkennung unterscheidbar.
Kontextmenü kompakte native Glass-Fläche, ca. 300 px breit, Radius 18 px, 12 px Innenrand, lesbar und direkt rechts von den markierten Zeilen. Überschrift exakt "4 Einträge ausgewählt", darunter feine Trennlinie. Menüeinträge exakt in dieser Reihenfolge, jeweils passende einfache Line-Icons: "In Split öffnen", "In Workspace verschieben ▸", "Offene Tabs schließen", "Archivieren", danach Trennlinie und "Auswahl aufheben". Menü NICHT modal zentrieren, keine globale Abdunkelung, keine zusätzlichen Optionen.
Rechts klare Fantasie-Webseite "Dünenlicht", Domain duenenlicht.example, ruhige Editorial-Seite mit Titel "Raum für neue Ideen.", Foto einer unbewohnten Düne, darunter dezenter Artikeltext. Seite gibt ruhigen Hintergrund und bleibt in 12 px inset/14 px radius Card. Alle Texte Deutsch.
```

### Toasts

```text
Subject: Zwei kompakte Bestätigungs-Toasts unten mittig in der Content-Card, horizontal exakt auf die Webseiten-Spalte rechts der Sidebar zentriert, 24 px über deren Unterkante und 8 px Abstand zwischen beiden Pills. Ganz bewusst eine rein WEISSE Beispiel-Webseite in beiden Chrome-Themes, um Lesbarkeit auf weiß zu belegen. Fantasieseite "Notizfeld", Domain notizfeld.example, weißer Hintergrund, schlichte Artikelseite mit Serifentitel "Platz für klare Gedanken.", zurückhaltende graue Textabsätze, wenige sanfte Linien, KEINE Personen und kein Foto; unteres Drittel frei und wirklich weiß.
Oberer Toast ca. 260×44 px mit einfachem Icon für Hintergrund-Tab, Text exakt "Im Hintergrund geöffnet". Unterer Toast ca. 350×44 px mit einfachem Häkchen-Icon, Text exakt: 3 Tabs nach „Arbeit“ verschoben. Beide kompakte Glass-Pills mit vollständig runden Enden, feiner kontrastierender Rand, weicher vollständig gerundeter Schatten und ausreichend dichter Glasfüllung. 14 px mittelstarke Systemschrift, klare dunkle bzw. helle Textfarbe passend zur Pill-Füllung. Nur das Icon trägt Akzent. Kein Close-Button, kein Modal, keine große Benachrichtigungskarte, keine erklärenden Außenbeschriftungen. Alle Browser-Tokens beibehalten. Sidebar hat Workspace "Privat", gespeicherte "Küstenpfad", "Reiseplan", "Notizfeld", Ordner "Reiseideen"; aktiver "Notizfeld"-Tab grau markiert. Die WEISSE Webseite bleibt auch im dunklen Browser-Chrome weiß.
```

Ergänzung für Dunkel:

```text
WICHTIG: Die beiden Toast-Pills DUNKLES dichtes Rauchglas mit heller Schrift und #70D6DE Icon, auch über der weißen Webseite. Keine grüne Erfolgsfarbe. Beide Meldungen exakt ohne Punkt am Satzende.
```

### Navigationszeile

```text
Subject: Eine klar beschriftete Vergleichskomposition für die korrigierte schwebende Navigationszeile. Ein einziges Bild mit ruhigem Ahoi-Glass-Rahmen, links normaler 236 px Sidebar; rechts im Contentbereich eine saubere Referenztafel mit DREI großen Beispielansichten untereinander, nicht drei komplette Browserfenster.
Oberhalb Überschrift "Navigationszeile". Erste Beispielansicht Label "a · Eingeklappt — weiße Webseite", darunter rechteckige opake rein weiße Webfläche mit Radius 14 und 12 px Abstand: am oberen Mittelpunkt ausschließlich eine dezente aber deutlich erkennbare dunkelgraue Reveal-Notch, exakt 32 px breit und 4 px hoch, Enden voll gerundet. KEINE Toolbar neben der Notch. Eine feine gestrichelte Annotation um die Notch kennzeichnet die transparente unsichtbare 44×24 px Hit-Area, mit Beschriftung "32 × 4 sichtbar · 44 × 24 Hit-Area"; Annotation ist Dokumentation, kein tatsächlicher UI-Rand.
Zweite Beispielansicht Label "a · Eingeklappt — dunkle Webseite", darunter opake fast schwarze Webfläche #11181D, gleicher Radius und Abstände: gleiche einzelne 32×4 px Notch am oberen Mittelpunkt, diesmal hellgrau und gut sichtbar. Erneut dezent annotierte 44×24 px Hit-Area und exakt dieselbe Maßbeschriftung. Auch hier ausschließlich die Notch als Browser-Chrome.
Dritte Beispielansicht Label "b · Ausgeklappt — schwebende Zeile", darunter opake Webfläche mit viel Ruhe. 12 px unter dem oberen Webflächenrand eine einteilige schwebende Glass-Navigationszeile, 12 px Abstand zu beiden Seiten, ca. 48 px hoch, Eckenradius exakt 14 px. Links Zurück, Vorwärts, Neu laden; in der Mitte Schloss und "notizfeld.example"; rechts Teilen, Plus, Menü. Sämtliche Icons einfache generische Line-Icons. Weicher Schatten folgt der gerundeten Form und läuft rund an ALLEN vier Ecken aus, KEIN rechteckiger Schattencontainer, KEINE gerade harte Schattenkante außerhalb der Rundung. Kleine beschriftete Bezugslinie "Radius 14 · weicher gerundeter Schatten", sowie "12 px Abstand" zur Außenkante.
Ruhige aufgeräumte Referenztafel, nicht verkleinern bis unlesbar; alle drei Zustände eindeutig vergleichbar. Nur diese drei Zustände, kein großer schwarzer Notch-Kasten und keine ausfahrende horizontale Tabzeile. Sidebar und Glasmaterial konsequent wie Grundstil, alle Beschriftungen Deutsch. Akzent sparsam in Annotationen, Notch selbst kontrastiert neutral zur Webseite.
```

Ergänzung für Dunkel:

```text
Die Vergleichstafel selbst ist dunkel mit heller Schrift; die erste Beispiel-Webfläche bleibt REIN WEISS und ihre Notch dunkelgrau, die zweite Beispiel-Webfläche bleibt fast schwarz und ihre Notch hellgrau. Die dritte Webfläche ist dunkel, die ausgeklappte Navigation dunkles Rauchglas. Alle drei Beschriftungen sowie beide Hit-Area-Maße vollständig im Bild. Seitenleiste schmal, keine zusätzlichen Zustandsscreens.
```

### Gezielte Korrektur-Prompts

Die jeweiligen hellen Ausgangsentwürfe wurden als Edit-Ziel übergeben.

```text
Use case: precise-object-edit. Bearbeite ausschließlich die Layoutgeometrie dieses hellen AhoiBrowser-Screendesigns. Finale Leinwand genau 1440×900. Verkleinere die aktuell ca.300 px breite Seitenleiste auf exakt236 px Breite: ihre rechte Kante muss bei x=236 liegen, und die Content-Card beginnt bei x=248. Reduziere die Zeilen der Sidebar auf je36 px Höhe, 12 px Innenabstand,16 px geometrische Favicons und14 px Schrift. Das Kontextmenü bleibt unmittelbar rechts neben der neuen Sidebar, mit derselben Überschrift „4 Einträge ausgewählt“ und denselben fünf Aktionen. Drei Tabs Küstenpfad, Reiseplan, Notizfeld und Ordner Reiseideen bleiben mit Häkchen ausgewählt; Dünenlicht bleibt aktiver Tab mit Aktivitätsstrich ohne Häkchen. Verschiebe den gesamten Webseiteninhalt passend, ohne Abschneiden. Halte Bildinhalt, Texte, Auswahlsemantik, helle Liquid-Glass-Farben, #006B73-Akzent, Fotos und Stil unverändert. Panels Radius18, Content-Card Radius14 und12 px Abstand. Kein neues UI, keine Marken, keine Personendaten.
```

```text
Use case: precise-object-edit. Ändere an diesem hellen AhoiBrowser-Screendesign NUR den unteren Bestätigungs-Toast. Seine grünliche Füllung wird zur gleichen sehr blassen neutralen Aqua-Glass-Füllung wie beim oberen Toast. Sein grünes kreisförmiges Häkchen wird ein einfaches Häkchen-Line-Icon in exakt #006B73, ohne grüne Kreisfüllung. Der untere Text lautet exakt ohne Satzpunkt: 3 Tabs nach „Arbeit“ verschoben. Korrekte deutsche öffnende und schließende Anführungszeichen. Der obere Toast bleibt unverändert mit „Im Hintergrund geöffnet“. Beide bleiben an derselben Stelle unten mittig mit denselben kompakten Pill-Rundungen, weichen Schatten und lesbarer dunkler Schrift. Sidebar, Toolbar, weiße Webseite, alle anderen Texte, Layout, Fantasiesymbole und Stil vollkommen unverändert. Finale Leinwand1440×900 PNG, kein neues UI und keine Marken.
```

</details>
