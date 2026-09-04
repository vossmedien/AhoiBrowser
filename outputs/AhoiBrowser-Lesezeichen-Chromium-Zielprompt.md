# AhoiBrowser: Lesezeichen und Chromium-Aktualisierung

Stand: 2026-09-05. Präzisierung des aktiven Ziels nach der zweiten technischen
und UI/UX-Review; der ursprüngliche Umfang bleibt vollständig erhalten.

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

Prüfe den mobilen Zugriff anhand der tatsächlichen Datenmodelle und der
sichtbaren Navigation. Eine Mediathek mit gespeicherten Workspace-Seiten ist
nicht automatisch ein Zugriff auf Chromium-Lesezeichen. Dokumentiere diesen
Unterschied und kläre die gewünschte gemeinsame Sammlung, bevor eine neue
plattformübergreifende Datenmigration oder Sync-Schema-Erweiterung erfolgt.
Ergänze fehlende mobile Bedienwege in Abstimmung mit dem aktiven Mobile-Agenten.

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
- Mobile: Quelle und Umfang des Lesezeichenzugriffs sind belegt; eine etwaige
  gemeinsame Sammlung ist tatsächlich plattformübergreifend erreichbar.
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
