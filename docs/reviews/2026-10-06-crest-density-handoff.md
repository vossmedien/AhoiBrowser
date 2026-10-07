# Crest-Paket A: Sidebar-Dichte (P4-028)

## Identität und Zuständigkeit

- Delegation: `2bd8907a`; eigene Cockpit-Session:
  `FE65AC60-B06B-4696-86AA-E4DD823BF36F`.
- Eigener Thread und natives Goal: `01a110d0-ddd4-7531-90ef-6631630b7324`.
  `get_goal` lieferte zunächst kein Goal; dieses Teilgoal wurde einmal neu
  angelegt und nach dem Source-Handoff abgeschlossen. Der nachträglich
  konsumierte Dateihandback ändert weder dieses Goal noch dessen Status.
  Kein fremdes Goal wurde geändert.
- Worktree: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-crest-reddit-recherche-9a68d1f3-codex-2bd8907a`.
- Branch: `cockpit/codex-2bd8907a`; Basis:
  `c75bc93cd5c576b65c8097b162ae048122e9e0c9`.
- Parent: `05EFDE54-146B-47AD-89CE-A56EF1781938`, Thread
  `01a110a1-bc0a-7ab0-a66e-3ec779a1b88c`.
- Desktop-Owner: Claude `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread
  `049a3c1d-5edf-4d2e-a173-c89f7f9e0663` im kanonischen AhoiBrowser-Repo.
  Parent und Owner übernehmen Integration, gemeinsamen Kandidaten, Build,
  sichtbare E2E, Defaultbranch/Push und Auslieferung.
- Die Cockpit-Sessionliste bestätigt den eigenen Worktree und beide Ziele.
  Ownership-Notiz an den Parent: `a8725ce9`; beim Cockpit aufgenommen.

## Aufgabenstand

- [x] Architektur, Appearance-/Profile-Prefs und reale Settings-/Views-Callers
  gegen die beauftragte Basis ermittelt.
- [x] Exklusive neue `sidebar_density*`-Dateien und fokussierten
  Regressioncheck erstellt.
- [x] Vollständiges Wiring ursprünglich als Unified-Diff geliefert.
- [x] Nach ausdrücklichem Claude-Handback über den Parent: Registrierung in
  `appearance_prefs.cc` sowie die beiden vorhandenen Ahoi-WebUI-Dateien direkt
  übernommen; diese drei Dateidiffs aus dem Handoff-Patch entfernt.
- [x] Abschließende Diff-/Referenzprüfung; Source-Paket bereit für den
  abschließenden DCO-/Lane-Commit und genau eine `cockpit_report`-Meldung.
  Commit-ID und Versandquittung stehen im Abschlussbeleg des Cockpits.

Das erste Source-Paket ist `4ddcc412953452b38c283f9c9c7834324ce22b0f`;
`cockpit_report` wurde dafür genau einmal erfolgreich zugestellt. Der spätere
Handback erlaubt `appearance_prefs.{cc,h}` und die vorhandene Ahoi-WebUI.
`appearance_prefs.h` benötigt keine Änderung. Die ergänzende Integration wird
mit einem eigenen DCO-/Lane-Commit und `cockpit_note` an denselben Parent
übergeben, ohne einen zweiten Abschlussbericht oder ein neues Goal.

## Vertrag des Source-Pakets

Quelle ist P4-028 in [der Reddit-Auswertung](2026-10-06-crest-reddit-feedback.md)
und [der aktuelle Crest-Abgleich](2026-10-06-crest-adoption.md).
Die aktuelle Referenz ist Crest Development 0.7.11/1184. Die historische
0.6-Galerie belegt keine aktuelle Runtime-Abnahme. Dieses Paket implementiert
nur die freigegebene Tab-Dichte; keine neue Fenstertransparenz.

| Profil-Pref-Wert | Deutsch / Englisch | Zeile (DIP) | Schrift-Delta | Icon-Delta | Vertikaler Surface-Inset | Mindesthöhe Split-Pane |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | Kompakt / Compact | 32 | −1 | −2 | 1 | 30 |
| 1 | Standard / Standard | 36 | 0 | 0 | 2 | 30 |
| 2 | Großzügig / Comfortable | 44 | +2 | +2 | 4 | 38 |

`ahoi.appearance.sidebar_density` ist ein Integer im vorhandenen
`appearance::RegisterProfilePrefs`. Standard ist der bisherige Default;
unbekannte Werte und fehlende Registrierung werden beim Lesen auf Standard
abgebildet. Die Standard-Geometrie referenziert `visual_style`, die vorhandenen
unterschiedlichen Icon-Basisgrößen bleiben erhalten. Kleine Status-/Emblem-
Dekorationen behalten ihre eigene Semantik.

Der Browser-Host beobachtet den Pref seines Chromium-Profils. Eine Views-
Property projiziert nur den gewählten Wert an seine vorhandenen Nachfahren;
es gibt keinen globalen Dichtezustand und keine zweite Pref-/Theme-Engine.
Das virtualisierte Tree-Layout, Runtime-Zeilen, Remote-Zeilen und native Splits
lesen dieselben Metriken. Bevorzugte Größen, Paint und hit-testbare Row-Bounds
folgen der tatsächlichen Projektion. Bestehende Tree-Selektions-, Tastatur-,
AX-, Editor- und Fokusobjekte bleiben erhalten. Veraltete Drag-Probes und
Geometrieanimationen werden verworfen, ohne persistente Daten zu verändern.

Parent-Korrektur bei der gemeinsamen Wiring-Prüfung: Der ursprüngliche Patch
fügte dem bereits 800-zeiligen Host-Header eine zusätzliche Callback-Methode
und einen zweiten Registrar hinzu und verletzte damit das bestehende
Modularitätslimit (802 Zeilen). Der aktualisierte Patch verwendet einen
gemeinsamen `appearance_pref_change_registrar_` für Page Tint und Dichte.
Der Dichte-Callback wird lokal in der vorhandenen Host-Initialisierung
gebunden; der Header bleibt bei 800 Zeilen. Keine neue Hilfsklasse oder
Änderung der Pref-/Lifetime-Semantik. Die anschließende Prüfung aller
betroffenen Quellen fand außerdem die Runtime-Zeilen bei 806 Zeilen.
Ihr redundanter Inset-Zwischenzustand entfällt: Alle drei aktuellen Presets
ändern Icon-Größe und Inset zusammen. Die notwendige Größenänderungsprüfung
bleibt erhalten, denn das native `ImageViewBase::SetImageSize` invalidiert
auch bei identischer Größe. Die aktuelle Kopplung und der künftige
Erweiterungspunkt sind im Source kommentiert. Ein unnötiger Titel-Alias
entfällt; die tatsächliche Runtime-Zeilen-Datei bleibt ebenfalls bei 800.
Parent-Diff sowie erneute gemeinsame Anwendbarkeit, Whitespace,
Sourcegrenzen und innere Patchsyntax PASS; Compile-/Runtime-Gates offen.

Der bestehende Root-Drop-Bereich und die Action-Slot-Breiten bleiben erhalten.
Kompakt-Zeilen bleiben mindestens 32 DIP, Split-Panes mindestens 30 DIP.
Der temporäre Close-Hit-Target behält mindestens seine bisherige Standardhöhe
von 32 DIP; gespeicherte Action-Slots bleiben bei ihrem bisherigen 24-DIP-Maß.
Neue Settings-Aktionen verwenden native Chromium-Dropdown-/Button-Elemente,
Beschriftungen und einen höflich angekündigten Reset-Status.

Die Auswahl gilt sofort für alle Fenster dieses Profils. Reset läuft über den
vorhandenen Settings-Handler mit dessen URL-/Profil-/Off-the-record-Grenze und
ruft ausschließlich `ClearPref(kSidebarDensityPref)` auf. Durch Richtlinien
gesperrte Einstellungen werden nicht geändert. Deutsche und englische Texte
werden über bestehende GRIT-/Settings-Pfade geliefert. Die neuen GRIT-IDs
folgen dem vorhandenen Fingerprint-Verfahren, gegen zwei vorhandene
DE-Übersetzungen gegengeprüft.

## Integration

Aktualisierter Parent-Handoff vom 6. Oktober: Der Runtime-Row-Hunk ist gegen
den kanonischen Stand `25604916` mit Mehrfachauswahl-Korrektur `8681500b`
angepasst. Der neue Selection-Callback und dessen AX-/Paint-Zustand bleiben
erhalten. Die unveränderte Erstellung von Tab-Rolle, zugänglichem Namen und
Tooltip liegt jetzt im vorhandenen `sidebar_runtime_tab_support`-Modul;
die Auswahl bleibt in der Runtime-Zeile. Dadurch hält die erweiterte
Runtime-Datei weiterhin genau 800 Zeilen. Keine neue Klasse, kein neuer
Zustand und keine weitere GN-Abhängigkeit. Der aktuelle Patch umfasst
21 bestehende Dateien statt der ursprünglichen 19.

Alle sechs Parent-Handoffs wurden danach gegen `ca1b641a` in der Reihenfolge
Dichte → Tabgruppen → Ordner → Settings-Hilfe → Workspace-Hilfe → Panels in
einer temporären Source-Kopie gemeinsam angewendet: Anwendbarkeit, erzeugter
Overlay-Whitespace, Sourcegrenzen und innere Patchsyntax PASS. Die temporäre
Kopie wurde entfernt; kanonische Produktdateien blieben unverändert.
Build, sichtbare Abnahme und native Review bleiben beim Desktop-Owner offen.

Die fünf neuen Dateien liegen unter
`overlay/chromium/src/ahoi/browser/ui/appearance/sidebar_density*`.
[Das Wiring-Patch](2026-10-06-crest-density-handoff.patch) ändert erst beim
Parent die verbleibenden 21 bestehenden Dateien. Die Registrierung und die
zwei WebUI-Dateien sind seit dem Handback direkt im Worktree implementiert:

- Appearance: bestehende GN-Targets; Registrierung bereits direkt übernommen.
- Sidebar: Host-Observer, Tree-Projektion, Saved-/Runtime-/Remote-Zeilen,
  Split-Layout und dessen bestehende Callers, Close-Hit-Target sowie GN.
- Settings: vorhandener Handler und dessen GN-Abhängigkeit. Dessen Reset-
  Backend liegt außerhalb des Handbacks und bleibt ausdrücklich im Patch;
  Dropdown und Reset-Frontend sind direkt in `ahoi_page.ts`/`ahoi_page.html.ts`.
- `patches/chromium/0001-ahoi-m153-integration-seams.patch`: Settings-Pref-
  Allowlist, Settings-Strings, GRIT-Definitionen und deutsche Übersetzungen.
  Die inneren Hunk-Längen und nachfolgenden Zielpositionen sind angepasst.

Nach Übernahme des Source-Commits im koordinierenden Integrations-Worktree:

```sh
git apply --check docs/reviews/2026-10-06-crest-density-handoff.patch
git apply docs/reviews/2026-10-06-crest-density-handoff.patch
```

Das Patch ist gegen die genannte Basis erzeugt; bei inzwischen geändertem
Wiring muss der Parent nur diese Integrationsstellen mit dem Desktop-Owner
abgleichen. Der Worker hat ausschließlich den ausdrücklich übermittelten
Pref-/WebUI-Handback konsumiert. Die Source-Commits allein aktivieren noch
keine Browserfunktion: Rest-Wiring einschließlich Reset-Backend, GN und
GRIT muss vor dem gemeinsamen Kandidaten übernommen werden.

## Verifikation und offene Abnahme

Die abschließende Source-Prüfung umfasst äußere Patch-Anwendbarkeit,
Syntax der angepassten inneren Chromium-Patchserie, Diff-Whitespace,
Datei-Ownership, GN-/Include-/UI-String-Referenzen und den eigenen Diff.
Keine Builds, Test-Binaries, Inhouse-Zugriffe, Computer Use oder Installationen
durch diesen Worker.

Prüfung des ersten Source-Pakets am 6. Oktober 2026: äußeres
`git apply --check` PASS; Source-
Whitespace-Check PASS; angepasste innere Patchserie mit `git apply --numstat`
vollständig parsebar; alle neuen Ahoi-Includes, GN-Source-Referenzen und acht
Settings-/GRIT-Keys auflösbar. Der eigene Index enthält ausschließlich die
fünf neuen Source-/Regressiondateien und die beiden Handoff-Dateien; Basis
und Branch entsprechen dem Auftrag. Patch-Dateien wurden beim allgemeinen
Whitespace-Check ausgenommen, weil Unified-Diff-Kontextzeilen syntaktisch ein
einzelnes Leerzeichen enthalten; der Source-Teil wurde zusätzlich mit
`git apply --check --whitespace=error-all` geprüft.
Der Referenzscanner erfasste zunächst auch unveränderte generierte Mojo-
Header und meldete einen falschen Dateibefund. Er wurde auf neue Include-
Referenzen eingegrenzt; diese Prüfung PASS. Kein Produktfehler oder Testlauf.

Nach dem Dateihandback: direkte Pref-/WebUI-Diffs gegen den ursprünglichen
Handoff geprüft; der Rest-Patch verliert ausschließlich diese drei Dateidiffs
und bleibt mit 19 Dateien unverändert. `git apply --check`, Source-Whitespace
und die erweiterte Ownership-Grenze PASS. Fünf Dateien im Ergänzungsdiff:
Registrierung, zwei WebUI-Dateien sowie die beiden aktualisierten Handoffs.
Die alte Source-/Regression-Evidenz gilt für die unveränderten Inhalte weiter;
die Compile-/Runtime-Gates bleiben offen.

`sidebar_density_unittest.cc` wird durch das Wiring in das vorhandene
`ahoi_sidebar_unittests`-Target aufgenommen. Es deckt ab:

- Standard, ungültigen Pref-Wert, Live-Pref-Benachrichtigung, gezielten Reset
  mit Erhalt anderer Appearance-Prefs und Policy-Sperre.
- Alle drei Presets in einer echten gespeicherten Tree-Zeile und Remote-Zeile,
  Preferred-Size-/Virtualisierungsprojektion, Split-Mindesthöhen und Close-
  Hit-Target; wiederholte Font-Anwendung und Rückkehr zum Standard ohne Drift.
- Native Tab-/TreeItem-AX-Rollen, erhaltene Tastatur-Fokussierbarkeit und
  unabhängige Host-Semantik.

Dieser Check ist im Source-Paket enthalten und hier **nicht ausgeführt**.
Nach dem sichtbaren Journey des exakten gemeinsamen Kandidaten ist sein
fokussierter Lauf vorgesehen:

```sh
out/AhoiDev/ahoi_sidebar_unittests \
  --gtest_filter='SidebarDensityPrefsTest.*:SidebarTreeViewTest.DensityChangesSavedAndRemoteRowsWithoutFontDrift'
```

Offen beim Parent/Owner: guarded Overlay-/Patch-/GN-/Build-Abnahme,
DE-/EN-Settings per Tastatur/AX, Live-Wechsel aller drei Presets über mehrere
Fenster und unterschiedliche Profile, Saved-/Temporary-/Remote-/Split-Tabs,
Sidebar-Resize, Scroll-/Drag-Geometrie, laufendes Umbenennen/Fokus, gezielter
Reset und Persistenz nach Neustart. Danach Integration/Push/Auslieferung über
den bestehenden Desktop-Pfad. Keine Browser-Runtime-Abnahme wird behauptet.
