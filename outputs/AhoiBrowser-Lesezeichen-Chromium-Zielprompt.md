# AhoiBrowser: Lesezeichen und Chromium-Aktualisierung

Stand: 2026-09-05. Präzisierung nach technischer/UI-Review und der ausdrücklichen
Nutzerentscheidung für dieselbe Desktop-/Mobile-Lesezeichensammlung. Zusätzlich
ist die beanstandete Auf-/Zuklappbewegung der Sidebar-Ordner zu verbessern.
Der ursprüngliche Umfang bleibt vollständig erhalten.

## Zielprompt

Implementiere im kanonischen AhoiBrowser-Repository eine kompakte, horizontal
scrollbare Lesezeichenzeile oben in der Desktop-Sidebar, inspiriert von der
Referenz des Nutzers. Ordner öffnen native, kaskadierende Menüs. Temporäre Tabs,
persistente Workspace-Seiten und Lesezeichen behalten ihre jeweilige Identität,
Lebensdauer und Datenquelle. Die Oberfläche folgt den semantischen Ahoi-Tokens,
funktioniert mit Maus, Trackpad und Tastatur und bleibt auch in einer schmalen
Sidebar verständlich. Ordnernamen sind lesbar, URL-Ziele haben verständliche
Beschriftungen beziehungsweise vollständige Tooltips, Überlauf ist erkennbar,
und die Verwaltung ist dauerhaft erreichbar. Leere, große und tief verschachtelte
Sammlungen sind vollwertige Zustände.

Verwende Chromium-Komponenten dort, wo sie zum Vertrag passen: kanonisches
BookmarkModel/MergedSurfaceService, native Bookmark-Menüs und Kontextaktionen,
Navigation, Berechtigungen und Schutz vor dem unbestätigten Öffnen sehr großer
Ordner. Vermeide eine zweite Persistenz- oder Sync-Implementierung und unnötige
Upstream-Patches. Baue Untermenüs bei Bedarf auf. Erhalte Scrollposition und
Tastaturfokus bei kosmetischen Aktualisierungen; aktualisiere Favicons gezielt.
Modeländerungen, Löschung, Menüabbruch, Fensterabbau und verzögerte Callbacks
dürfen weder ungültige Zeiger noch verlorene Bedienzustände erzeugen. Das
Scrollen der Leiste darf keinen Workspace-Wechsel auslösen.

Desktop und Mobile müssen dieselbe logische Lesezeichensammlung anbieten. Die
Nutzerentscheidung dafür ist ausdrücklich erteilt; eine Mediathek mit
gespeicherten Workspace-Seiten ist kein Ersatz. Chromiums natives BookmarkModel
bleibt die Desktop-Autorität. Binde es über einen klar abgegrenzten Adapter an
den bestehenden verschlüsselten, lokalen Ahoi-Sync-Vertrag an und ergänze die
mobile Projektion/Bedienung gemeinsam mit dem Mobile-Owner. Verwende stabile
Identitäten, Ordnerhierarchie, deterministische Reihenfolge, Feldkonfliktlösung
und dauerhafte Löschinformationen. Änderungen dürfen keine Rückkopplungsschleifen,
Dubletten oder wiederauferstandenen Löschungen erzeugen. Erweiterungen müssen
bestehende Workspace-Daten und ältere Datensätze verlustfrei erhalten; weder
eine zweite Sync-Engine noch ein stilles Umdeuten gespeicherter Seiten ist erlaubt.
Profile, verwaltete Einträge, privates Browsen, Sync-Opt-in, Account-/Schlüsselwechsel
und unbekannte Versionen behalten explizite sichere Grenzen. Eine neue
Production-CloudKit-/Release-Freigabe wird aus dieser Produktentscheidung nicht
abgeleitet.

Prüfe und verbessere mit dem Desktop-Owner die beanstandete Auf-/Zuklappanimation
der normalen Workspace-Ordner in der Sidebar. Diese Desktop-Zuordnung ist die
transparent mitgeteilte Arbeitsannahme. Die Bewegung folgt den vorhandenen
semantischen Motion-Tokens, bleibt kurz und unterbrechbar und erhält Fokus,
Scrollanker und eine stabile Hierarchie ohne sichtbare Sprünge, Überlappung oder
Flackern. Schnelles Richtungswechseln und verschachtelte Ordner gehören dazu.
Reduced Motion muss die Bewegung sofort abschließen; Animation ist ausschließlich
Präsentation und darf weder die Datenmutation verzögern noch native Drag-Sessions
stören. Keine parallele Animationsarchitektur und keine überlappenden Writes.

Behebe mit dem Desktop-Owner auch den gemeldeten Renderingfehler am Übergang
zwischen Sidebar, gerundeter Toolbar und Inhaltsfläche: Die im Screenshot vom
2026-09-05 um 09:19:18 markierte dunkle rechteckige Stufe darf nicht zurückbleiben.
Reproduziere den tatsächlichen Fenster-/Sidebar-Zustand und korrigiere die
zuständige Bounds-/Clipping-/Hintergrundgeometrie, ohne den Fehler durch eine
zusätzliche Abdeckfläche zu verdecken. Referenz:
`artifacts/computer-use/bookmarks-coordination-20260905/user-sidebar-seam-091918.png`.

Prüfe den aktuellen vollständig ausgerollten Mac-ARM64-Stable-Upstream über
offizielle Quellen und führe ein verfügbares, sicheres Chromium-Update über den
vorhandenen reproduzierbaren Roll-Pfad durch. Nutze gefilterten Commit-/Tree-
Fetch, gezielte Hydration und Preflight ohne Checkout-Wechsel; prüfe danach
DEPS, Toolchain, Workarounds, Patch-Serie, Build und Laufzeit. Erhalte eine
konkrete Rückkehrmöglichkeit. Ein entdeckter Kandidat oder grüner Preflight ist
noch keine aktualisierte Anwendung. Optimiere nachgewiesene unnötige Kosten
des Update-/Build-Pfads, ohne Integritätsprüfungen zu schwächen.

Arbeite auf derselben linearen Branch-Basis wie die laufenden Desktop- und
Mobile-Aufgaben. Bearbeite ausschließlich klar zugeordnete Dateien. Ein
kurzlebiger, sauberer Build-Snapshot darf dieselben Commits materialisieren;
er ist keine zweite Entwicklungsbasis. Koordiniere Checkout-, Overlay-, Build-
und Installationsbesitz ausdrücklich und beachte die CPU-Gates. Alle neuen
oder umgebauten Module bleiben unter 800 Zeilen und erhalten eng gefasste,
verständliche Verantwortlichkeiten.

Erstelle zuerst einen ausführbaren Kandidaten und prüfe dessen sichtbare
End-to-End-Journeys. Führe erst danach passende programmatische Verhaltenstests
aus. Nach wesentlichen Korrekturen wiederhole die betroffene sichtbare Journey.
Schmale Sicherheitsprüfungen neuer Build-Werkzeuge dürfen dem Build vorausgehen.
Tests müssen tatsächliches Verhalten absichern, nicht bloß Quelltextfragmente
bestätigen. Liefere die genaue Source-/Chromium-Version, Build-Provenienz,
sichtbare Belege und Testergebnisse; benenne fehlende externe oder Gerätebelege
präzise. Committe und pushe nur die eigenen Änderungen nach bestandenen Gates.

## Verbindliche Abnahme

- Desktop: Leiste sichtbar; Überlauf in beide Richtungen bedienbar; Ordner und
  Unterordner öffnen; URL und mehrere Seiten öffnen; Abbruch mit Escape;
  Tastaturfokus und Scrollposition bleiben bei Favicon-Updates erhalten;
  Verwaltung und native Kontextaktionen sind erreichbar.
- Regression: persistente/temporäre Tabs, Workspace-Swipe und Sidebar-Suche
  behalten ihre bestehenden Verträge; Menü-Löschung und Fensterabbau sind sicher.
- Ordnerbewegung: die beanstandete Desktop-Auf-/Zuklappbewegung ist am exakten
  installierten Kandidaten sichtbar verbessert, auch bei Verschachtelung,
  schnellem Richtungswechsel, Scrollposition und Reduced Motion.
- Rendering: der gemeldete dunkle Absatz an der Sidebar-/Toolbar-/Inhaltsgrenze
  ist im reproduzierten Fensterzustand sichtbar behoben; Rundung und Flächen
  schließen korrekt an.
- Mobile/Sync: dieselbe Lesezeichensammlung ist über den echten Desktop-Adapter
  und die mobile Ansicht erreichbar. Ordner, Öffnen, Änderungen, Reihenfolge,
  Offline-Konflikte, Neustart und Löschung konvergieren ohne Vermischung mit
  Workspace-Seiten. Historische Library-Tests oder zwei simulierte Swift-Peers
  allein beweisen keinen Chromium-/Mobile-Roundtrip. Externe Account-, Geräte-
  oder Provisioning-Grenzen bleiben ausdrücklich benannt und gelten nicht als Pass.
- Chromium: offizieller aktueller Kandidat und vorhandener Pin sind belegt;
  die tatsächlich ausgelieferte Version ist durch Build und Laufzeit bestätigt.
- Qualität: sinnvolle Verhaltenstests, Dateigrenzen, keine fremden Änderungen,
  eine gemeinsame lineare Historie und nachvollziehbare Abnahmeevidenz.

## Review-Ausgangsbefunde

- `c3f599a`: Ein verschachtelter Referenz-Konstruktor ruft eine nicht-statische
  Methode seiner äußeren Klasse ohne Instanz auf; der letzte Kandidat besitzt
  noch keine erfolgreiche Build-Receipt.
- Eigene Menüs materialisieren rekursiv den gesamten Unterbaum. Chromiums
  `BookmarkMenuDelegate` bietet bedarfsweise Untermenüs und native Kontextaktionen.
- Ein Favicon-Ereignis baut sämtliche Buttons neu auf und kann Fokus verlieren.
- Die bisherige mobile Prüfung belegt Ahoi-Saved-Pages, nicht die identische
  Chromium-Bookmark-Sammlung.
- Die Overlay-Komposition schreibt den großen isolierten Git-Index einmal je
  Overlay-Datei. Die gemessene Verifikationsphase dauerte über 36 Minuten;
  gebündelte Index-Einträge sind als Integrität erhaltende Optimierung zu prüfen.
