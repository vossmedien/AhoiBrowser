# Ordner-Vorschau: alle Seiten statt nur letzter Besuche

Source-Handoff für Paket 2 des [Integrationsplans](2026-10-06-crest-integration-plan.md), Basis `c75bc93cd5c576b65c8097b162ae048122e9e0c9`. Sämtliche Sidebar-Dateien bleiben beim Desktop-Owner Claude. Diese Session ändert sie ausschließlich über das beigefügte [Wiring-Patch](2026-10-06-crest-folder-preview-handoff.patch).

## Verhalten und Integrationsumfang

Die bisherige Vorschau filtert den Ordnerinhalt anhand der letzten 365 Tage Browser-History und zeigt maximal sechs Treffer. Unbesuchte Seiten und Einträge außerhalb der begrenzten History-Abfrage fehlen. Unterschiedliche Baumknoten mit derselben URL werden bereits vor der Anzeige zusammengefasst.

Der Patch stellt alle gültigen gespeicherten Seiten des Ordners einschließlich verschachtelter und eingeklappter Ordner bereit. Das autoritative `is_temporary`-Flag verhindert, dass geschlossene temporäre Session-Knoten als gespeicherte Links erscheinen. Die Reihenfolge folgt den `sort_key`-Werten der Geschwister. Jeder Eintrag behält seine Baum-UUID; Aktivieren verwendet unverändert `ActivateSavedPage`, ohne einen zweiten Tab-Store. Verschiedene Knoten derselben URL bleiben einzeln erreichbar. History liefert nur noch optionale Besuchszeit-Beschriftungen. Unbesuchte Seiten zeigen den Host beziehungsweise das URL-Schema.

Suche arbeitet über den vollständigen Inhalt. Sechs native Zeilen pro Seite und die Schaltflächen „Zurück“/„Weiter“ begrenzen die sichtbare Geometrie und Favicon-Anfragen auch bei 10.000 Seiten. Diese Seitennavigation ersetzt die im Plan vorgeschlagene Scroll-Liste, damit kein vollständiger Ordner als Views erzeugt wird. Der Baum wird iterativ durchlaufen; tiefe Verschachtelung wächst nicht auf dem C++-Callstack. Die bestehende einmalige History-Abfrage bleibt auf 2.048 Ergebnisse begrenzt und filtert keine Seite mehr heraus.

Asynchrone Favicon-Antworten aktualisieren das Icon einer vorhandenen Zeile und erhalten deren Tastaturfokus. Ein aktiviertes Popup bleibt für Tastatureingaben offen, wenn der Mauszeiger es verlässt; die native Deaktivierung schließt es anschließend. Beim Wechsel zwischen Ordnern wird die Vorschau erst nach dem nativen Close-Callback neu geöffnet; die vorhandene Generation, WeakPtr- und Anchor-Prüfung verwirft veraltete Ergebnisse. Aktivieren oder explizites Invalidieren verhindert das Wiederöffnen.

Sieben Dateien sind im Patch enthalten: Host-Query, Host-State/-Header, Vorschau-View/-Header, ein fokussierter Views-Regressioncheck und dessen Eintrag im bestehenden `unit_tests`-Target. Keine zusätzlichen Targets, Dependencies, Profile, Sync-Daten oder GRIT-Änderungen. Neue kleine Bedientexte folgen der bestehenden Deutsch-/Englisch-Locale-Konvention der Sidebar.

## Übernahme und Abnahme

Nach Cherry-Pick dieses Handoff-Commits in den gemeinsamen Integrationsstand:

```sh
git apply --check --whitespace=error-all docs/reviews/2026-10-06-crest-folder-preview-handoff.patch
git apply docs/reviews/2026-10-06-crest-folder-preview-handoff.patch
```

Änderungen am Host-State/-Header und `BUILD.gn` mit den Dichte- und Tabgruppen-Handoffs zusammenführen. Keinen bereits bearbeiteten Sidebar-Stand ersetzen.

Source-Prüfung am 6. Oktober 2026: Patch-Anwendbarkeit und Source-Whitespace gegen die genannte Basis PASS; eigener Diff auf Ownership, vorhandene Callers und zusätzliche Dependencies geprüft. Kein Build, Test-Binary, Inhouse-Zugriff oder sichtbarer Browserlauf dieser Session. Compile- und Runtime-Abnahme bleiben offen.

Der enthaltene, hier nicht ausgeführte Regressioncheck prüft sieben unbesuchte Knoten derselben URL über zwei Seiten, Aktivierung der siebten UUID und Erhalt der Zeile beim Favicon-Update. Ein 10.000-Seiten-Fall prüft die auf sechs begrenzten anfänglichen Icon-Anfragen. Nach sichtbarer Abnahme am exakten gemeinsamen Kandidaten:

```sh
out/AhoiDev/ahoi_sidebar_unittests \
  --gtest_filter='SidebarRecentLinksViewTest.*'
```

Sichtbar offen: verschachtelte und unbesuchte gespeicherte Seiten, keine geschlossenen temporären Session-Knoten, Suche nach einem Eintrag außerhalb der ersten Seite, Seitennavigation per Tastatur/VoiceOver einschließlich Mauszeiger außerhalb des aktivierten Popups, identische URLs mit unterschiedlichen UUIDs, vorhandenes Runtime-Tab ohne Reload aktivieren, schnelles Wechseln zwischen Ordnern, Schließen während laufender Abfragen und Verhalten großer Ordner. Die Auslieferung wird erst mit Kandidatenrevision und diesen Ergebnissen im Desktop-Checkpoint abgenommen.
