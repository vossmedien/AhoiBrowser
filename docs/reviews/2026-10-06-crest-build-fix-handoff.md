# Compilerkorrekturen für den vollständigen Crest-Kandidaten

## Tatsächlicher Fehler und Zuständigkeit

Der wiederaufgenommene guarded Build von `47885f36` endete am 6. Oktober
2026 um **20:15:16 UTC mit Exit 1**. Direkter lesender Nachweis um 20:35:24
UTC: Ziel `MacbookPro2026.fritz.box`, Runner `47404` beendet, `phases.log`
terminal und `build-ssh.log` mit Produkt-Compilerfehlern. Die früheren
Fortschrittsangaben belegen keine Fertigstellung. Das ursprüngliche Log bleibt
unter `~/inhouse/evidence/ahoi-47885f36/` erhalten.

Root liefert ausschließlich diesen [Patch](2026-10-06-crest-build-fix-handoff.patch)
und aktualisierte Übergaben im eigenen Branch. Gemeinsame Sidebar-Dateien,
Build, GUI und Auslieferung bleiben bei Session `68E66C9E-7E52-4842-B328-03D9CD6D3057`,
Thread/Goal `01a11179-cb6b-7dc1-9e53-f3d72fefc198`. Keine Prozesse beendet,
Builds gestartet oder gemeinsam besessenen Produktdateien geändert.

## Kleine Korrektur ohne geänderten Auswahlvertrag

Die vollständige Fehlerauswertung enthält sechs eindeutige Fehlerstellen
mit vier Ursachen:

| Ursache | Korrektur im vorhandenen Modul |
| --- | --- |
| Nichtleere virtuelle Standardmethoden im Header, `sidebar_tree_view_delegate.h:33/34`, vom nativen Chromium-Clang-Plugin abgelehnt | Beide Standarddefinitionen nach `sidebar_tree_view_delegate.cc` verschieben. Leere Reihenfolge und fehlender aktiver Knoten bleiben gleich; Datei bereits im bestehenden GN-Target. |
| `auto*` kann den `raw_ptr` aus `View::children()` nicht ableiten, `browser_sidebar_host_multi_select.cc:59/86` | Beide vorhandenen Schleifen verwenden `views::View*`, wie die anderen Sidebar-Traversierungen. Reihenfolge und Auswahlzustand bleiben gleich. |
| Rückgabetyp `ImageSkia` beim Häkchenzeichnen unvollständig, `sidebar_multi_selection_paint.cc:32` | Fehlenden direkten Include `ui/gfx/image/image_skia.h` ergänzen. |
| Bool-Rückgabe eines direkt mit `WeakPtr` gebundenen Methodenaufrufs, `bind_internal.h:1036`, Caller in `browser_sidebar_host_presentation.cc` | Vorhandenes Guard-Lambda-Muster verwenden: gültiger Host ruft `OnRuntimeMultiSelect` auf; abgelaufener Host liefert `false`. Keine rohe Hostreferenz und keine geänderte Klicksemantik. |

Version/Vertrag: Chromium **155.0.8059.26**, Pin
`16c3e55476d3564bea713314b2fff638749ce3e6`, tatsächlicher Build-Clang
`llvmorg-24-init-7747-g62397f8b-27`. Native Diagnose und passende primäre
Definitionen geprüft: [View-Kinder](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/ui/views/view.h),
[WeakPtr-Bind-Vertrag](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/base/functional/bind_internal.h).
Kein neues Framework, keine neue GN-Abhängigkeit und kein zweiter Auswahlzustand.

## Integration und offene Abnahme

Patch-Basis: `75398749d17bde08b349bb32771cae21ee0495ce`.
Die fünf betroffenen Source-Dateien sind zwischen dem fehlgeschlagenen
`47885f36` und dieser Basis bytegleich (Git-Diff leer).
Source-Commits des Crest-Pakets zuerst übernehmen. Danach aktuelle Patches:

1. Buildfix
2. Dichte
3. Tabgruppen
4. Ordner-Vorschau
5. Settings-Hilfe
6. Workspace-Hilfe
7. Panels

Der aktuelle Tabgruppen-Patch setzt den Guard-Lambda-Fix voraus. Alle sieben
Patches gemeinsam gegen diese Basis in einer temporären Source-Kopie
angewendet: **Anwendbarkeit, erzeugter Overlay-Whitespace, Sourcegrenzen und
innere Patchsyntax PASS** (42 betroffene Zielpfade). Temporäre Kopie entfernt;
kanonischer Source unverändert. Keine Compilation oder Browserabnahme behauptet.

Desktop-Owner übernimmt die gemeinsame Source und verwendet die erhaltenen
Outputs über den guarded Buildpfad nach aktueller Kapazitätsprüfung. Danach
sichtbare Mehrfachauswahl-/Switcher-Reisen und übrige betroffene Crest-Wege
am exakten Kandidaten, fokussierte native Checks und eine native `codex review`.
Compilerpass, gemeinsame Installation, Runtime-Abnahme, Integration/Push und
Lieferung bleiben offen. Eine separate Zwischenlieferung entfällt gemäß Nutzersteuerung.
