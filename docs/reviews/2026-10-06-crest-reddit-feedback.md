# Crest: Reddit-Feedback vom 6. Oktober 2026

Die fünf verlinkten Beiträge wurden am 6. Oktober 2026 mit AppleScript in einem normalen Safari-Privatfenster auf dem Entwicklungs-Mac gelesen, ohne Reddit-Anmeldung. Eingeklappte Antworten wurden geöffnet; tiefere Antworten in Beitrag 1 und 5 wurden über die angebotenen Kommentar-Permalinks gelesen. Die fünf Galeriebilder von Beitrag 4 und die eingebetteten Beispielbilder wurden ebenfalls gelesen. Texte und Bilder wurden nicht als Rohdaten im Repository abgelegt.

**Statusmaßstab:** „umgesetzt“ bedeutet im Thread als vorhanden beschrieben oder im veröffentlichten RC-Bild gezeigt; es ist keine eigene Funktionsprüfung von Crest. „geplant“ bezeichnet eine erklärte Umsetzungsabsicht oder das vorgestellte Konzept. „abgelehnt“ umfasst ausdrücklich vorläufig beibehaltenes Verhalten. „offen“ bezeichnet eine bloß erwogene Änderung, eine ungeklärte Fehlermeldung oder eine fehlende Antwort. Der Status gilt zum Zeitpunkt des jeweiligen Threads; spätere Crest-Versionen werden daraus nicht abgeleitet.

| Beitrag | Veröffentlichungsdatum | Gelesene Kommentare |
| --- | --- | --- |
| 1 · UI-Hinweise | 20.09.2026 | 7 von 7 |
| 2 · Vorschläge | 18.09.2026 | 1 von 1 |
| 3 · Tab-Switcher | 17.09.2026 | 3 von 3 |
| 4 · Customization Update | 14.09.2026 | 13 von 13 |
| 5 · Sub-Spaces | 13.09.2026 | 7 von 7 |

Die Daten stammen aus den Zeitangaben der geöffneten Beiträge. Alle 31 Kommentare waren lesbar. Zustimmungen, Dank und Einladungen ohne konkrete Browserfunktion sind nicht als zusätzliche Funktionspunkte gezählt.

## 1. Crest is missing a lot of small browser UI hints

**Datum:** 20. September 2026. **Quelle:** [Originalbeitrag](https://www.reddit.com/r/CrestBrowser/comments/1wljavg/crest_is_missing_a_lot_of_small_browser_ui_hints/).

**Kernaussage:** Kleine, vorübergehende Rückmeldungen fehlen oder sind schwer erkennbar; dadurch bleibt nach einer Aktion unklar, ob sie funktioniert hat. pauljoda unterscheidet bereits vorhandenes Verhalten von fehlenden Hinweisen und verweist auf sein bestehendes Benachrichtigungssystem.

| Punkt | Funktion, Wunsch oder Problem mit Kurzbeleg | Antwort von pauljoda | Status |
| --- | --- | --- | --- |
| P1-01 | Rückmeldung beim Öffnen eines Hintergrund-Tabs: „no confirmation“. Das Kommentarbild konkretisiert den Wunsch mit „New background tab opened!“. | Nach anfänglichem Missverständnis hält er zusätzliche ereignisgesteuerte Hinweise für sinnvoll und leicht ergänzbar; „keep in mind“ ist noch keine feste Zusage. | offen |
| P1-02 | Vollbild-Hinweis: „press Esc to exit“. | Bestätigt, dass dieser Hinweis fehlt; sagt lediglich „perhaps should be added“. | offen |
| P1-03 | Zieladresse beim Link-Hover: „not seeing the destination URL“. | Laut ihm bereits unten links vorhanden: „that also is present, bottom left“. | umgesetzt |
| P1-04 | Benachrichtigung über Pointer-Lock/Mausfang: „no pointer-lock / mouse-capture notification“. | Bestätigt das Fehlen zusammen mit dem Vollbild-Hinweis; Ergänzung wird erwogen. | offen |
| P1-05 | Mehr kurzlebige Bestätigungen für Browseraktionen: „very few small confirmation toasts“. | Die Engine steuert bestehende Hinweise; bittet um weitere Beispiele. Sein eigenes System kann beliebigen Text anzeigen und weitere Ereignisse aufnehmen. | offen |
| P1-06 | Einstellbares Öffnen von Links im Hintergrund oder mit Wechsel zum neuen Tab: „either follow the tab“. | Beschreibt die vorhandene Einstellung; als Auslöser nennt er Cmd-Klick und Mausrad-Klick. | umgesetzt |
| P1-07 | Kopierbenachrichtigung über Cmd+Shift+C: „tab copied“. | Nennt dies als bereits nutzbares Beispiel seines Benachrichtigungssystems. | umgesetzt |
| P1-08 | Beitrag zur neuen Tab-Benachrichtigung: „make PR with new tab notification?“. | Beiträge sind willkommen, sollen aber erst nach dem laufenden Umbau der Basislogik erfolgen. Die konkrete Benachrichtigung wird nicht als fertig zugesagt. | offen |
| P1-09 | Herauslösen der Anwendungslogik in eine engineübergreifende Steuerung: „core app logic to a control plane“. | Beschreibt den bereits bearbeiteten Branch; erst nach dessen Landung soll die Basis für Beiträge stabiler sein. | geplant |
| P1-10 | Einbau von Chromium: „swapping in chromium“. | Nennt dies als Teil desselben laufenden Architekturumbaus, noch nicht als gelandete Änderung. | geplant |

Entwicklerbelege: [erste Antwort](https://www.reddit.com/r/CrestBrowser/comments/1wljavg/comment/pazhiu6/), [Toast-System](https://www.reddit.com/r/CrestBrowser/comments/1wljavg/comment/pazm5vs/), [Architektur und Beiträge](https://www.reddit.com/r/CrestBrowser/comments/1wljavg/comment/pazva0j/).

## 2. Few Suggestions

**Datum:** 18. September 2026. **Quelle:** [Originalbeitrag](https://www.reddit.com/r/CrestBrowser/comments/1wjpfca/few_suggestions/). Genannte Version: **0.6.28 (1125)**.

**Kernaussage:** Der Beitrag nennt kleine Gestaltungsinkonsistenzen, Fehlverhalten beim Space-Wechsel und fehlende Bedienoptionen. pauljoda kündigt einige Korrekturen an, begründet bewusst begrenzte Ordnerformen und lässt mehrere größere UI-Änderungen offen.

| Punkt | Funktion, Wunsch oder Problem mit Kurzbeleg | Antwort von pauljoda | Status |
| --- | --- | --- | --- |
| P2-01 | Gleiche Abstände zwischen Tabs und Ordnern: „margin between tabs should be same as folders“. | Abstände gehören zur laufenden Überarbeitung; er will sie prüfen. | geplant |
| P2-02 | Symbole mit derselben Farbe wie Text: „icons should be colored with text“. | Nimmt die Symbolfarben zusammen mit den Abständen in die Überarbeitung auf. | geplant |
| P2-03 | Gleichwertige Einstellbarkeit von Name und Symbol: „option for name and icon“. | Weitere direkte Bearbeitungsmöglichkeiten für diese Eigenschaften seien vorgesehen. | geplant |
| P2-04 | Doppeltippen auf die Sidebar aktiviert in der genannten Version kein Vollbild: „doesn't go to full screen“. | Die Titelleiste störte die Webseitenoberkante und wurde deaktiviert; die native Doppeltipp-Funktion muss wieder eingebaut werden. | geplant |
| P2-05 | Beim Wechsel zu einem Space mit geöffneten Einstellungen erscheint nur eine mittig stehende Sidebar: „only sidebar centered in the middle“. | Bestätigt den Fehler; das Laden der SwiftUI-Inhaltskarte braucht Anpassungen. | geplant |
| P2-06 | „+ New Tab“ übernimmt den Pill-Eckenradius nicht: „doesn't change corner radius“. | Diesen Button will er an die gewählte Form angleichen. | geplant |
| P2-07 | Ordner übernehmen die vollständige Pill-/Capsule-Form nicht: „folders … doesn't change corner radius“. | Die Rundung bleibt begrenzt: Bei variabler Ordnerhöhe könne eine Kapsel zum Kreis werden und unpassend aussehen. | abgelehnt, vorerst |
| P2-08 | Download-Funktion ist hinter dem Archivsymbol schwer auffindbar: „forgettable where downloads are“. | Erwägt einen situationsabhängigen Download-Pfeil oder ein anderes Symbol, entscheidet sich aber nicht fest. | offen |
| P2-09 | Gemeinsames Menü für Archiv, Verlauf und Downloads: „archive, history, and downloads“. | Erklärt das vorhandene Verhalten: Normalerweise öffnet sich das Archiv, bei Download-Badge stattdessen die Download-Ansicht. | umgesetzt |
| P2-10 | Angeheftete Seite zusätzlich per einfachem Favicon-Klick auf ihre gespeicherte Adresse zurücksetzen: „single click on page favicon“. | Gute Idee; er habe dies vermutlich ohnehin beabsichtigt, aber noch nicht umgesetzt. | geplant |
| P2-11 | Adressleiste, Zurück/Vorwärts und Neuladen oben platzieren, damit sie bei versteckter Sidebar erreichbar bleiben: „to top of the browser“. | Eine obere Leiste sei für später wahrscheinlich, ausdrücklich aber nicht Teil des nächsten Zyklus. Keine feste Zusage. | offen |
| P2-12 | Space-Namen ausblenden oder Ordner-Einklappen von der Namenszeile entkoppeln: „disable this name?“. | Behält die Zeile wegen ihrer Container- und Menüfunktion vorerst bei. Eine weniger auffällige Alternative wird nur erwogen. | abgelehnt, vorerst |
| P2-13 | Aufblitzen der Sidebar beim Space-Wechsel vermeiden: „it flashes a bit“. | Kann es nicht reproduzieren und bittet um eine Bildschirmaufnahme; die Systemeinstellung „Reduced Motion“ könnte die Animation beeinflussen. | offen |
| P2-14 | Farben beim Space-Wechsel weich überblenden: „bluring colors“. | Laut ihm bereits an den Fortschritt des Wechsels gekoppelt: „blend the color based on progress“. | umgesetzt |

Entwicklerbeleg für alle Punkte: [Antwort von pauljoda](https://www.reddit.com/r/CrestBrowser/comments/1wjpfca/comment/pakgx1x/). Die drei eingebetteten Bilder illustrieren Abstände/Formen, Navigation und die isoliert stehende Einstellungs-Sidebar; sie nennen keine weiteren Wünsche.

## 3. Tab switcher, please!

**Datum:** 17. September 2026. **Quelle:** [Originalbeitrag](https://www.reddit.com/r/CrestBrowser/comments/1winzkd/tab_switcher_please/).

**Kernaussage:** Gewünscht ist ein Tab-Switcher im Stil von Arc einschließlich Schließen des fokussierten Tabs über W. pauljoda plant ihn im 0.7-Zyklus, stellt aber Bugfixes voran und bezeichnet das Konzept noch als unfertig.

| Punkt | Funktion, Wunsch oder Problem mit Kurzbeleg | Antwort von pauljoda | Status |
| --- | --- | --- | --- |
| P3-01 | Tab-Switcher mit dem im Beitrag genannten Kürzel: „Ctrl + T tab switcher“. | „As part of the 0.7 cycle“ geplant; zunächst werden Fehler behoben. Die konkrete Tastenkombination bestätigt er nicht gesondert. | geplant |
| P3-02 | Im aktiven Switcher den fokussierten Tab mit W schließen: „pressing W closes the focused tab“. | „The W to close is on that list“; Gesamtplanung noch nicht abgeschlossen. | geplant |
| P3-03 | Paginierte Bereiche nach Tab-Kategorie: „paginated sections … like pins, saved“. | Will Bereiche für die jeweils geöffneten Pins, gespeicherten Tabs und weitere Kategorien. | geplant |
| P3-04 | Switcher vollständig mit der Tastatur bedienen: „keyboard navigation“. | Bestandteil seines beschriebenen Konzepts. | geplant |
| P3-05 | Zwischenlösung für eine ähnliche Funktion: „some kind of workaround“. | Auf diese spätere Nachfrage ist im Thread keine Entwicklerantwort vorhanden. | offen / keine Antwort |

Entwicklerbeleg: [0.7-Plan](https://www.reddit.com/r/CrestBrowser/comments/1winzkd/comment/pad5qz2/).

## 4. 0.6 - The Customization Update - RC in Dev Channel Now

**Datum:** 14. September 2026. **Quelle:** [Originalbeitrag mit fünf Galeriebildern](https://www.reddit.com/r/CrestBrowser/comments/1wg3t13/06_the_customization_update_rc_in_dev_channel_now/).

**Kernaussage:** pauljoda stellt den Release Candidate für Crest 0.6 mit umfangreicher Anpassbarkeit, besserer Tab-/Ordnerorganisation, Übersetzung, Erweiterungskompatibilität und Alltagskorrekturen vor. Die Kommentare melden verbleibende Darstellungs- und Bedienprobleme und schlagen weitere Optionen vor.

**Veröffentlichungsstand im Thread:** Der RC liegt im Dev Channel und in TestFlight vor. Die Dev-Builds tragen zunächst noch 0.5-Versionsnummern; 0.6 soll mit dem Stable-Release folgen. Stable wird als baldiger nächster Schritt angekündigt, nicht als schon veröffentlicht behauptet.

### Vollständige Liste aus dem Release-Text und den Begleitbildern

Alle folgenden Zeilen sind Aussagen oder gezeigte Einstellungen von pauljoda im Release-Beitrag. Der jeweilige Kurzauszug ist zugleich der Entwicklerbeleg; **„umgesetzt“ gilt hier für den angekündigten/gezeigten RC**. Die Bilder konkretisieren verfügbare Optionen, belegen aber nicht für jedes einzelne Bedienelement eine erstmalige Einführung in 0.6. Abweichende Fehlermeldungen folgen getrennt darunter. Allgemeine Werbeaussagen ohne konkrete Funktion werden nicht als zusätzliche Features gezählt.

Bildquellen: [G1 · Übersicht](https://i.redd.it/jgjpkz9fnhph1.png), [G2 · Einrichtung](https://i.redd.it/7dskcbxlnhph1.jpg), [G3 · Getting Started](https://i.redd.it/asdf1bxlnhph1.jpg), [G4 · Look and Feel](https://i.redd.it/jfc6baxlnhph1.jpg), [G5 · Crest Studio](https://i.redd.it/2y1g6bxlnhph1.jpg). Diese Verweise führen zu den veröffentlichten Bildern; keine Bilddatei liegt im Repository.

#### Wappen, Identität und Vorschau

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda / Bildbeschriftung | Status |
| --- | --- | --- | --- |
| P4-001 | Echte heraldische Symbole und erweitertes Wappenmaterial | „true heraldic symbols“ | umgesetzt |
| P4-002 | Auswahl und Bearbeitung der Wappenform; in G5 „Plate“, gezeigte Auswahl „Shield“ | „shapes“ / „Plate“ | umgesetzt |
| P4-003 | Wappenmuster | „patterns“ | umgesetzt |
| P4-004 | Unabhängige Wappenfarben, in G5 drei Farbslots | „independent color choices“ | umgesetzt |
| P4-005 | Wappenfarben wahlweise an Space-Farben koppeln | „Follow Space colors“ (G5) | umgesetzt |
| P4-006 | Emoji als Identität | „use emoji“ | umgesetzt |
| P4-007 | Monogramm als Identität | „monograms“ | umgesetzt |
| P4-008 | Durchsuchbarer SF-Symbols-Picker für die Space-Identität | „searchable SF Symbols picker“ | umgesetzt |
| P4-009 | Vorlage als Ausgangspunkt, danach anpassen oder eigenen Entwurf erstellen; G2/G5 nennen Winter, Lion, Storm, Dragon, Meadow, Iron, River, Sun und Vigil | „Start with a template and adjust it“ | umgesetzt |
| P4-010 | Wappenvorschau bleibt beim Bearbeiten sichtbar | „preview stays visible“ | umgesetzt |
| P4-011 | Entwurf über Shuffle variieren | „Shuffle“ (G2/G5) | umgesetzt |
| P4-012 | Identitätsstil zwischen Wappen und Icon wählen | „Identity style“: „Crest“, „Icon“ (G2/G5) | umgesetzt |
| P4-013 | Space-Namen im Studio bearbeiten bzw. in der Einrichtung per Namensklick ändern | „Space name“ (G5); „Click the name to rename“ (G2) | umgesetzt |
| P4-014 | Zurücksetzen auf die gewählte Vorlage | „Reset controls return to this preset“ (G2/G5) | umgesetzt |
| P4-015 | Live-Vorschau von Sidebar und Seite in Look and Feel | „live preview of the sidebar and page“ | umgesetzt |
| P4-016 | Farbpalette des Crest-App-Icons wählen, passend zu Vorlagen | „different palette for Crest's app icon“ | umgesetzt |

#### Fenster, Sidebar, Adressfeld, Tabs und Pins

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda / Bildbeschriftung | Status |
| --- | --- | --- | --- |
| P4-017 | Sidebar-Größe anpassen | „sidebar size“ | umgesetzt |
| P4-018 | Sidebar auf die rechte Seite verschieben | „move the sidebar to the right“ | umgesetzt |
| P4-019 | Fenster-/Seitenrand anpassen bis zu randlosen Seiten | „borderless pages“; „Window border“ (G4) | umgesetzt |
| P4-020 | Transparenz auch beim fokussierten Fenster aktivieren | „Focused window transparency“ (G4) | umgesetzt |
| P4-021 | Transparenz über Regler einstellen | „Transparency“ (G4) | umgesetzt |
| P4-022 | Seitenanimation beim Space-Wechsel ein-/ausschalten | „Animate pages when switching Spaces“ (G4) | umgesetzt |
| P4-023 | Adressfeld folgt wahlweise dem Space-Akzent | „Follow Space accent“ (G4, Address field) | umgesetzt |
| P4-024 | Farbfüllung des Adressfelds | „Color fill“ (G4) | umgesetzt |
| P4-025 | Rand des Adressfelds | „Border“ (G4, Address field) | umgesetzt |
| P4-026 | Akzentumriss beim Bearbeiten des Adressfelds | „Accent outline while editing“ (G4) | umgesetzt |
| P4-027 | Eckenradius der Tabs/Pins: Square, Soft, Round, Capsule | „Corner radius“ (G1/G4) | umgesetzt |
| P4-028 | Tab-Skalierung: Compact, Default, Comfortable; Text, Symbole und Abstände skalieren gemeinsam | „Tab scale“ (G4) | umgesetzt |
| P4-029 | Pin-Layout; in den Bildern ist „Balanced rows“ sichtbar | „Pin layout“ (G1/G4) | umgesetzt |
| P4-030 | Farbige Tab-Ränder: nur ausgewählte, Pins oder alle Tabs | „Color borders“: „Selected“, „Pins“, „All tabs“ (G4) | umgesetzt |
| P4-031 | Akzentumriss ausgewählter Tabs | „Accent outline on selected tabs“ (G4) | umgesetzt |
| P4-032 | Website-Farben für Pins verwenden | „Website colors for pins“ (G4) | umgesetzt |
| P4-033 | Tabs/Pins folgen wahlweise dem Space-Akzent | „Follow Space accent“ (G4, Tabs and pins) | umgesetzt |
| P4-034 | Füllung angehefteter Tabs | „Pinned fill“ (G4) | umgesetzt |
| P4-035 | Leuchten des ausgewählten Tabs | „Selected tab glow“ (G4) | umgesetzt |
| P4-036 | Hover-Tönung | „Hover tint“ (G4) | umgesetzt |
| P4-037 | Standard-Seitenzoom über kontinuierlichen Regler von 25 % bis 500 %; Seiten mit Standardzoom aktualisieren sich sofort. Page-Zoom-Befehle überschreiben ihn vorübergehend, Actual Size kehrt zum Standard zurück. | „25% to 500%“; „Actual Size returns here“ (G4) | umgesetzt |
| P4-038 | Einstellungen einzeln zurücksetzen, ohne andere Änderungen zu verlieren | „Individual settings can be reset“ | umgesetzt |
| P4-039 | Einstellungsgruppe zurücksetzen | „Reset“ bei Window bzw. Tabs and pins (G4/G1) | umgesetzt |
| P4-040 | Gesamtes Look and Feel zurücksetzen | „Reset All Look and Feel…“ (G4) | umgesetzt |

Die Bilder zeigen nur einzelne Werte und die ausgewählte Pin-Layout-Option. Nicht sichtbare Dropdown-Optionen oder weitere Wertebereiche werden daraus nicht erfunden.

#### Ordner und gemeinsame Tab-Aktionen

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda / Bildbeschriftung | Status |
| --- | --- | --- | --- |
| P4-041 | Eigene Ordnersymbole | „Folders … icons“ | umgesetzt |
| P4-042 | Emoji für Ordner | „icons and emoji“ | umgesetzt |
| P4-043 | Dauerhafte farbige Ordnerhervorhebung | „persistent color highlights“; „Always show folder highlights“ (G4) | umgesetzt |
| P4-044 | Tab-Anzahl am Ordner optional anzeigen | „optional tab counts“ | umgesetzt |
| P4-045 | Farbige Ordnerränder optional anzeigen | „Show folder color borders“ (G4) | umgesetzt |
| P4-046 | Ordnernamen an Ordnerfarbe anpassen | „titles that can match the folder color“ | umgesetzt |
| P4-047 | Farbintensität und Textfarbe von Ordnern je Space anpassen | „part of each Space's appearance“ (G4) | umgesetzt |
| P4-048 | Eigene Ordnersymbole statisch statt mit animierter Ordnergrafik zeigen | „custom folder icons can stay static“ | umgesetzt |
| P4-049 | Offene Tabs in benannten, farbigen Ordnern organisieren | „named, colored folders“ | umgesetzt |
| P4-050 | Verschachtelte Ordner in Open Tabs | „including nested folders“ | umgesetzt |
| P4-051 | Ordner zwischen Saved und Open verschieben | „Move folders between Saved and Open“ | umgesetzt |
| P4-052 | Vollständige Open-Ordnerstruktur geräteübergreifend synchronisieren | „keep their structure synced between devices“ | umgesetzt |
| P4-053 | Mehrfachauswahl von Tabs und Ordnern auf dem Mac mit Command-/Shift-Klick | „Command-click or Shift-click“ | umgesetzt |
| P4-054 | Ganze Auswahl in einem Schritt verschieben | „move the selection in one step“ | umgesetzt |
| P4-055 | Auswahlaktionen aus Sidebar-Menüs auf iPhone/iPad | „Selection actions … on iPhone and iPad“ | umgesetzt |
| P4-056 | Split View aus gespeicherten/angehefteten Tabs erzeugt Kopien; Originale bleiben bestehen | „makes copies, leaving the originals in place“ | umgesetzt |

**Sync-Grenze laut pauljoda:** Beim Upgrade müssen alle Geräte aktualisiert werden; ältere Builds zeigen die neuen Open-Ordner möglicherweise nicht richtig. Dies ist eine ausdrücklich genannte Kompatibilitätsgrenze der neuen Struktur.

#### Navigation, Link-Verhalten und Peek

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda | Status |
| --- | --- | --- | --- |
| P4-057 | Command Palette ergänzt Adressen aus dem aktuellen Space | „complete addresses from the current Space“ | umgesetzt |
| P4-058 | Vorschlag mit Tab oder Pfeil rechts annehmen | „Tab or Right Arrow“ | umgesetzt |
| P4-059 | Auf dem Mac einen Link per Mausziehen ohne Modifier als Live-Peek öffnen | „without a modifier key“ | umgesetzt |
| P4-060 | Zieladresse beim Link-Hover anzeigen | „shows its destination before you open it“ | umgesetzt |
| P4-061 | Link-Öffnungseinstellungen greifen konsistenter | „Link-opening preferences behave more consistently“ | umgesetzt |
| P4-062 | Beim Verschieben eines Tabs in einen anderen Space wahlweise folgen oder im bisherigen Space bleiben | „either follow it or leave you where you are“ | umgesetzt |
| P4-063 | Gespeicherte/angeheftete Tabs per Doppelklick auf die gespeicherte Adresse zurücksetzen | „return to their saved address with a double-click“ | umgesetzt |
| P4-064 | Gespeicherte/angeheftete Tabs optional beim Schließen zurücksetzen | „optionally reset when closed“ | umgesetzt |

#### Übersetzung und Picture in Picture

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda | Status |
| --- | --- | --- | --- |
| P4-065 | Seitentext lokal mit Apples Übersetzungssystem auf Mac, iPhone und iPad übersetzen | „on device using Apple's translation system“ | umgesetzt |
| P4-066 | Seitensprache erkennen | „detect the language“ | umgesetzt |
| P4-067 | Nach Übersetzung wieder das Original anzeigen | „show the original again“ | umgesetzt |
| P4-068 | Automatisch übersetzen, optional und mit bereits installierten Sprachpaaren | „language pairs already installed on the device“ | umgesetzt |
| P4-069 | Natives Picture in Picture auf dem Mac | „native Picture in Picture“ | umgesetzt |
| P4-070 | Video beim Tab-Wechsel sichtbar halten; automatischer PiP-Eintritt optional | „Automatic entry is optional“ | umgesetzt |

#### Erweiterungen und Berechtigungen

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda | Status |
| --- | --- | --- | --- |
| P4-071 | Erweiterungs-Seitenpanels neben der Webseite | „side panels can stay beside your pages“ | umgesetzt |
| P4-072 | Auswahl eines Erweiterungs-Panels je Space | „a selected panel for each Space“ | umgesetzt |
| P4-073 | Verbesserte Extension-Anmeldung | „improves sign-in“ | umgesetzt |
| P4-074 | Verbesserte Kompatibilität mit Extension-Tab-Gruppen | „tab groups“ | umgesetzt |
| P4-075 | Verbesserte Extension-Popups | „popups“ | umgesetzt |
| P4-076 | Verbesserter Seitenlesezugriff für Erweiterungen | „page reading“ | umgesetzt |
| P4-077 | Verbesserte Screenshot-Kompatibilität, einschließlich Browserwerkzeugen von Claude und ChatGPT | „screenshots, including browser tools used by Claude and ChatGPT“ | umgesetzt |
| P4-078 | Berechtigungsdialoge stapeln sich nicht mehr wiederholt | „no longer stack repeatedly“ | umgesetzt |
| P4-079 | Zuverlässiger gespeicherte Berechtigungsentscheidungen | „remembered choices are more reliable“ | umgesetzt |
| P4-080 | Extension-Installation erklärt die gewährten Zugriffe | „explains the access you're granting“ | umgesetzt |
| P4-081 | Engere Cookie-Isolation für Extension-Panels | „tightens cookie … isolation“ | umgesetzt |
| P4-082 | Engere Space-Isolation für Extension-Panels | „Space isolation for extension panels“ | umgesetzt |
| P4-083 | Mehr Privatsphäre beim Laden von Website-Symbolen | „improves privacy when loading website icons“ | umgesetzt |

#### Einrichtung und Alltag

| Punkt | Funktion oder Einstellung | Kurzzitat von pauljoda / Bildbeschriftung | Status |
| --- | --- | --- | --- |
| P4-084 | Vorlagen bei der Einrichtung eines neuen Space | „New Space setup has templates“ | umgesetzt |
| P4-085 | Fokussierte Bedienelemente bei der Space-Einrichtung | „focused controls“ | umgesetzt |
| P4-086 | Live-Vorschau bei der Space-Einrichtung | „a live preview“ | umgesetzt |
| P4-087 | Interaktiver Getting-Started-Tab erklärt Tabs, Pins und Ordner | „teaches tabs, pins, and folders“ | umgesetzt |
| P4-088 | Split-View-Übung auf dem Mac | „Split View practice“ | umgesetzt |
| P4-089 | Erweiterungsanleitung auf dem Mac | „extension guidance on Mac“ | umgesetzt |
| P4-090 | Übungen verändern nur den Beispiel-Space | „Practice changes affect only this example Space“ (G3) | umgesetzt |
| P4-091 | Übungen zurücksetzen | „Reset practice“ (G3) | umgesetzt |
| P4-092 | Interaktive Pin-Übung über Kontextmenü oder Übungsbutton | „Pin everyday apps“; „Pin Gmail“ (G3) | umgesetzt |
| P4-093 | Einstellungen öffnen auf dem Mac als nativer Tab | „Settings opens in a native tab on Mac“ | umgesetzt |
| P4-094 | Einstellungen bleiben auf iPhone/iPad als schließbares Sheet | „dismissible Settings sheet“ | umgesetzt |
| P4-095 | Sidebar, Seitenkarten, Farben und Space-Icons bewegen sich beim Space-Wechsel gemeinsam | „move together“ | umgesetzt |
| P4-096 | Schnelle Wischfolgen enden zuverlässiger im Zielzustand | „rapid swipes settle more predictably“ | umgesetzt |
| P4-097 | Benachbarte Sidebars bleiben vorbereitet | „nearby sidebars stay ready“ | umgesetzt |
| P4-098 | Seiten werden schon beim Beginn des Inhaltsladens sichtbar statt erst am Navigationsende | „instead of waiting for navigation to finish“ | umgesetzt |
| P4-099 | Download-Animation auf dem Mac vom Klickpunkt zum Archiv | „animates from the click position toward Archive“ | umgesetzt |
| P4-100 | Download-Rückmeldung unten links bei versteckter Sidebar | „bottom-left feedback when the sidebar is hidden“ | umgesetzt |
| P4-101 | Schärfere canvasbasierte Editoren | „sharper canvas-based editors“ | umgesetzt |
| P4-102 | Genauere Farben besuchter Links | „more accurate visited-link colors“ | umgesetzt |
| P4-103 | Korrekturen für direkte Audio-Links | „direct audio … links“ | umgesetzt |
| P4-104 | Korrekturen für direkte Video-Links | „direct … video links“ | umgesetzt |
| P4-105 | Zuverlässigerer Start auf Mobilgeräten | „more reliable mobile startup“ | umgesetzt |
| P4-106 | Website-Uploads auf iPhone/iPad aus Fotos | „uploads offer Photos“ | umgesetzt |
| P4-107 | Website-Uploads auf iPhone/iPad aus der Kamera | „camera“ | umgesetzt |
| P4-108 | Website-Uploads auf iPhone/iPad aus Dateien | „Files“ | umgesetzt |
| P4-109 | Native Link-Menüs auf iPhone/iPad wiederhergestellt | „native link … menus are restored“ | umgesetzt |
| P4-110 | Native Bild-Menüs auf iPhone/iPad wiederhergestellt | „image menus are restored“ | umgesetzt |
| P4-111 | Sichtbarer Clear-Open-Tabs-Button; gespeicherte und angeheftete Tabs bleiben bestehen | „leaves saved and pinned tabs alone“ | umgesetzt |

### Weitere Pläne sowie konkrete Probleme und Wünsche aus den Kommentaren

| Punkt | Funktion, Wunsch oder Problem mit Kurzbeleg | Antwort von pauljoda | Status |
| --- | --- | --- | --- |
| P4-K01 | Weitere heraldische Symbole nach dem Kernbestand | Im Beitrag „will likely expand in the future“; keine verbindliche Zusage oder Liste. | offen |
| P4-K02 | Ordnersymbole werden abgeschnitten, überwiegend bei Icon- statt Emoji-Optionen: „Folder icons are getting cut off“. | Konnte das Abschneiden nicht reproduzieren. | offen |
| P4-K03 | Pin-Ränder verlieren beim Laden die Farbe bzw. erhalten unpassende helle/dunkle Farben: „lose their color once the tab is loaded“. | Erkennt die übrigen beschriebenen Darstellungsprobleme an und will damit experimentieren; kein Fix oder Termin bestätigt. | offen |
| P4-K04 | Auch ungeladene Pins erhalten unpassende Farben, etwa Mint statt der Website-Palette: „mint green border“. | Dieselbe allgemeine Anerkennung; keine gesonderte Lösung genannt. | offen |
| P4-K05 | Mit Tint Folder Title sind Ordnernamen schwer lesbar: „hard to read“. | Erkennt die übrigen Probleme an; noch kein belegter Fix. | offen |
| P4-K06 | Ordnernamen mit mittlerer oder fetter Schrift: „medium or bold“. | Nur allgemeine Bereitschaft, die Vorschläge auszuprobieren. | offen |
| P4-K07 | Ordnertext dunkler als der Hintergrund statt heller: „darker than the folder background color“. | Keine konkrete Umsetzung bestätigt. | offen |
| P4-K08 | Zu geringe Ordnersymbol-Auswahl ohne Emoji: „selection is very limited“. | Nimmt dies mit den übrigen Vorschlägen auf. | offen |
| P4-K09 | SF-Symbols-Picker auch für Ordner statt nur Space-Identität: „have an SF Symbol picker“. | Keine feste Zusage; nur allgemeines Experimentieren. | offen |
| P4-K10 | Alternativ mehr kuratierte Ordnersymbole: „curated symbols“. | Keine konkrete Auswahl oder Umsetzung zugesagt. | offen |
| P4-K11 | Altes iPadOS-Tastaturlayout ohne Leertaste noch vorhanden: „without the space bar“. | Sollte nicht mehr auftreten; fragt nach Startseite und Screenshot, akzeptiert eine angebotene Video-Demonstration. | offen |
| P4-K12 | Optional breite horizontale Adressleiste oben: „stretch out horizontally in the top bar“. | Erwägt Experimente im nächsten Zyklus; muss insbesondere das randlose Layout bedenken. Keine verbindliche Umsetzung. | offen |
| P4-K13 | Reddit-Beitragstitel funktionieren teilweise nicht mit Drag-Peek: „post titles refusing to work with Peek on drag“. | Keine Antwort im Thread. | offen / keine Antwort |
| P4-K14 | Ordnerinhalt per Hover zeigen, ohne den Ordner zu öffnen: „Hover on Tab Folder shows it's contents“. | Keine Antwort im Thread; das Bild zeigt die gewünschte Vorschau als Beispiel eines anderen Browsers. | offen / keine Antwort |
| P4-K15 | Aus der Hover-Vorschau einen Tab anklicken und öffnen: „pick a Tab and open it on click“. | Keine Antwort im Thread. | offen / keine Antwort |
| P4-K16 | Ordner beim Wechsel zu einem Tab außerhalb automatisch einklappen; zuletzt aktivierten Ordner-Tab sichtbar lassen: „Folder keeps open … close it manually“. | Keine Antwort im Thread. | offen / keine Antwort |

Entwicklerbelege: [Darstellungsprobleme und Symbolwünsche](https://www.reddit.com/r/CrestBrowser/comments/1wg3t13/comment/p9tiohp/), [iPad-Tastatur](https://www.reddit.com/r/CrestBrowser/comments/1wg3t13/comment/p9ugp7w/), [obere Adressleiste](https://www.reddit.com/r/CrestBrowser/comments/1wg3t13/comment/p9w2m2u/). [Unbeantwortete Peek-/Ordnerwünsche](https://www.reddit.com/r/CrestBrowser/comments/1wg3t13/comment/p9t3vvj/).

## 5. Sub-Spaces - Idea for sharing context between spaces

**Datum:** 13. September 2026. **Quelle:** [Originalbeitrag](https://www.reddit.com/r/CrestBrowser/comments/1wf8a1l/subspaces_idea_for_sharing_context_between_spaces/).

**Kernaussage:** pauljoda schlägt Sub-Spaces vor: getrennt gestaltbare Sidebars innerhalb einer Profilgruppe sollen Logins und Erweiterungen teilen. Das Konzept wird als mögliche Hauptfunktion von 0.7 diskutiert; wiederholte Anmeldung und Einrichtung pro Profil werden als Hindernis für den Alltagsgebrauch genannt.

| Punkt | Funktion, Wunsch oder Problem mit Kurzbeleg | Antwort bzw. Konzept von pauljoda | Status |
| --- | --- | --- | --- |
| P5-01 | Bisher ein Profil pro Space: „1:1 profile to space“. | Beschreibt dies als bisheriges, bewusst bevorzugtes Trennungsmodell. | umgesetzt |
| P5-02 | Beliebig viele Sub-Spaces innerhalb eines Space: „any number of … Sub spaces“. | Vorgestelltes Konzept, möglicherweise Hauptfunktion von 0.7; kein fertiger Release. | geplant |
| P5-03 | Jeder Sub-Space besitzt eine eigene anpassbare Sidebar: „another customizable sidebar“. | Ein gemeinsamer Space lässt sich auf mehrere Sidebars verteilen. | geplant |
| P5-04 | Eigener Name je Sub-Space: „set a name“. | Wie bei unabhängigen Spaces vorgesehen. | geplant |
| P5-05 | Eigenes Wappen je Sub-Space: „crest“. | Wie bei unabhängigen Spaces vorgesehen. | geplant |
| P5-06 | Eigene Farben je Sub-Space: „colors“. | Wie bei unabhängigen Spaces vorgesehen. | geplant |
| P5-07 | Gemeinsame Sitzungen, Cookies und Logins innerhalb der Gruppe: „share logins“. | In der späteren Antwort ausdrücklich bestätigt; die gemeinsame technische Basis soll wiederholte Anmeldung vermeiden. | geplant |
| P5-08 | Gemeinsame Erweiterungen innerhalb der Gruppe: „sessions and extensions“. | Später bestätigt: gemeinsame Erweiterungen sollen wiederholte Installation und Einrichtung vermeiden. | geplant |
| P5-09 | Profilgleiche Sub-Spaces müssen in der Gesamtliste nebeneinander liegen: „must within that group“. | Zwischen ihnen kann kein unabhängiger Space eingeschoben werden. | geplant, feste Konzeptgrenze |
| P5-10 | Reihenfolge der Sub-Spaces innerhalb der Gruppe ändern: „rearrange sub space order“. | Umsortieren ist nur innerhalb der Gruppe vorgesehen. | geplant |
| P5-11 | Auswahl markiert die gesamte Profilgruppe: „outline the entire group“. | Äußere Markierung der Gruppe beim Wechsel vorgesehen. | geplant |
| P5-12 | Zweite eingerückte Auswahl markiert den aktiven Space: „inset second layer selector“. | Innere Markierung des gerade angezeigten Gruppenmitglieds vorgesehen. | geplant |
| P5-13 | Gemeinsamer Editorbereich macht den geteilten Kontext erkennbar: „editor all in the same space section“. | Sidebar-Identitäten bleiben separat, der gemeinsame Sitzungs-/Erweiterungskontext soll klar sein. | geplant |
| P5-14 | Durch Spaces und Sub-Spaces in einer linearen Liste scrollen: „5 total spaces in the switcher“. | Beispiel: Solo-Space, Haupt-Space mit zwei Sub-Spaces, weiterer Solo-Space ergeben fünf Einträge. Es bleibt der bisherige Scrollablauf. | geplant |
| P5-15 | Pins wahlweise gemeinsam nutzen: „optionally pinned tabs“. | Frühe Antwort noch unentschieden und eher getrennt; spätere Antwort beschreibt ausdrücklich optional gemeinsame Pins. Details bleiben unfertig. | geplant |
| P5-16 | Andere Nutzereinstellungen separat halten: „user settings are independent“. | Technische Basis gemeinsam, individuelle Gestaltung/Einstellungen getrennt; gemeinsame Pins werden als Option konkretisiert. | geplant |
| P5-17 | Wiederholte Anmeldung in jedem Profil ist lästig: „log into all my accounts separately“. | Sub-Spaces sollen dies durch geteilte Logins lösen; im Thread noch keine Umsetzung. | geplant |
| P5-18 | Dieselben Erweiterungen mehrfach installieren: „install the same extensions again“. | Das geteilte Erweiterungsumfeld soll dies vermeiden. | geplant |
| P5-19 | Erweiterungen je Profil erneut konfigurieren: „configure them separately“. | Sagt, Sub-Spaces würden die genannten Einrichtungsprobleme lösen. | geplant |
| P5-20 | Pins für jedes Profil erneut einrichten: „set up pinned tabs for each profile too“. | Spätere Antwort nennt optional gemeinsame Pins als Lösung. | geplant |

Entwicklerbelege: [Reihenfolge und Scrollen](https://www.reddit.com/r/CrestBrowser/comments/1wf8a1l/comment/p9kmpgl/), [frühe Pin-/Einstellungsabgrenzung](https://www.reddit.com/r/CrestBrowser/comments/1wf8a1l/comment/p9kusbs/), [spätere Konkretisierung gemeinsamer Logins, Erweiterungen und optionaler Pins](https://www.reddit.com/r/CrestBrowser/comments/1wf8a1l/comment/p9l64s2/).

## Lesbarkeit und Abschlussgrenzen

Alle fünf Beiträge, die 31 angezeigten Kommentare sowie relevante Bilder waren ohne Login lesbar. Beim ersten Versuch zeigte Chrome auf der neuen Oberfläche eine Prüfung der Menschlichkeit und leitete die alte Oberfläche zur Anmeldung um. Die anschließende Safari-Lektüre der neuen Oberfläche funktionierte einschließlich aller Kommentarantworten; daher blieb kein beauftragter Inhalt wegen dieser anfänglichen Zugriffshürde ungelesen.

Es wurde nichts gepostet, abgestimmt oder gefolgt, kein Cookie-Banner bestätigt und der Build-Mac nicht bedient. Der Bericht enthält ausschließlich die Quellenbefunde. Die Bewertung gegen Ahois Repository, aktuelle Commits und Produktentscheidungen liegt bei der auftraggebenden Session.
