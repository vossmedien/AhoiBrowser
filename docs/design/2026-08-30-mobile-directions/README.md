# AhoiBrowser Mobile design directions

Stand: 30. August 2026

Figma-Vergleichsboard (interner Arbeitsstand):
<https://www.figma.com/design/7UskiiLmaOo58ipN399O5T>

Die lokale Workspace-Canvas-Quelle wurde nach dem Review auf die generische
Autorenzeile `Ahoi Redaktion` und einen abstrakten Segelboot-Avatar umgestellt.
Das Figma-Starter-Kontingent war bei dieser Korrektur erschoepft; der live
verlinkte Board-Abschnitt enthaelt deshalb noch die aeltere, rein synthetische
Autorenzeile und ist bis zum naechsten erfolgreichen Asset-Sync nicht fuer die
externe Weitergabe freigegeben. Die lokalen PNGs in diesem Ordner sind die
datenschutzbereinigte Referenz.

## Zweck und Evidenzgrenze

Das Board stellt die sichtbaren iPhone-/iPad-Zustaende des am 30. August 2026
gebauten Simulator-Kandidaten drei bewusst unterschiedlichen ImageGen-
Richtungen gegenueber. Die Entwuerfe sind Entscheidungs- und
Kommunikationshilfen. Sie sind weder implementierte SwiftUI-Zustaende noch
Runtime-, Accessibility- oder Release-Evidenz.

Die aktuelle Produktoberflaeche bleibt die verbindliche Referenz fuer
Navigation, Informationsarchitektur, Begriffe und bestehende Ahoi-Funktionen.
Arc Mobile dient nur als Prinzipreferenz fuer webseitenzentriertes Browsing,
daumennahe Suche und Workspaces; fremde Markenassets oder ein pixelgenauer
Clone sind ausgeschlossen.

## Drei Richtungen

1. **Harbor Deck** (`harbor-deck-iphone.png`) verdichtet den aktiven
   iPhone-Browser um eine ruhige, daumennahe Command-Bar und ein klares
   Workspace-Deck. Diese Richtung liegt am naechsten am heutigen Produkt und
   hat damit das geringste Migrationsrisiko.
2. **Workspace Canvas** (`workspace-canvas-ipad.png`) nutzt die iPad-Breite als
   echte Arbeitsflaeche mit persistenter Bibliothek, zentraler Website und
   Inspector. Sie ist die staerkste iPad-Uebersetzung der Desktop-
   Informationsarchitektur, darf aber nicht als verkleinerter Desktop enden.
3. **Focus Voyage** (`focus-voyage-iphone.png`) macht Suche, aktuelle Reise und
   naechste Aktion zur ruhigen Startflaeche. Sie eignet sich besonders als
   neuer Tab/Startzustand, nicht als Ersatz fuer die vollstaendige aktive
   Browseroberflaeche.

## Empfohlene Produktsynthese

- Harbor Deck als evolutionaere iPhone-Basis fuer aktive Webseiten.
- Focus Voyage als fokussierter neuer Tab und privater Startzustand.
- Workspace Canvas als eigenstaendige iPad-Regular-Width-Ausbaustufe.
- Bestehende System-SF-Pro-Typografie, Ahoi-Farbtokens, native Materialien,
  mindestens 44 x 44 pt grosse Ziele und semantische SwiftUI-Komponenten
  bleiben verbindlich.
- Vor Codeuebernahme folgt eine Auswahlentscheidung und danach ein
  screenweiser SwiftUI-Slice mit gleicher Fixture, gleicher Viewport-Groesse,
  Screenshotvergleich und Accessibility-Pruefung.

## Board-Artefakte

- `figma-current-product-evidence.png`: verifizierter Figma-Abschnitt mit vier
  Ist-Zustaenden.
- `figma-imagegen-layout-directions.png`: datenschutzbereinigtes lokales
  Vergleichsboard mit den drei Entwurfsrichtungen; nach der Anonymisierung kein
  Export des noch nicht synchronisierten Live-Abschnitts.
- Die Figma-Datei nutzt Apples verlinkte `iOS and iPadOS 26` Community Library.
  Nur die Board-Anmerkungen verwenden wegen eines serverseitigen
  Figma-Renderproblems Inter als Fallback; App und Entwuerfe behalten die
  systemnahe SF-Pro-Designsprache.
