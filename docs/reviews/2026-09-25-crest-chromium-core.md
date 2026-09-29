# Crest wechselt auf Chromium: Bewertung für Ahoi

Stand: 25. September 2026. Nachtrag zum [Crest-Vergleich vom 12. September](2026-09-12-crest-vergleich.md).
Die daraus beauftragte Umsetzung steht im
[Zielprompt Crest-Konvergenz-Härtung](../../outputs/AhoiBrowser-Crest-Konvergenz-Haertung-Zielprompt.md).

**Belegumfang.** Gelesen wurden der Reddit-Beitrag des Crest-Autors
„Chromium and New Core“ (25.09.), der Branch `chromium-control-plane` auf
`14e7bd6d` (23.09.) mit `Documentation/Architecture/ControlPlane.md`,
`EngineAbstractionCompletion.md` und `CrestEngines/Chromium/README.md` sowie
die Experimental-Release-Notes 0.6.133/0.6.143. Crest wurde nicht installiert
oder ausgeführt; es gibt keinen Laufzeit- oder Performancevergleich.

## Was Crest ändert

- Mac-Engine standardmäßig **Chromium 152** auf Basis von ungoogled-chromium-macos;
  WebKit bleibt als alternativer Mac-Build und auf iPhone/iPad.
- Die bestehende **SwiftUI-/AppKit-Oberfläche bleibt**. Chromiums
  `BrowserWindow` wird durch eine native Implementierung ersetzt (adaptiert
  aus dem MIT-lizenzierten Mori), aktiviert über `--crest-control-plane`.
  Fork-Fläche: ein Patch mit rund 1.800 Zeilen plus rund 6.000 Zeilen Overlay.
- Domäne und Sync-Semantik (Spaces, Tabs, Ordner, Splits, Verlauf, Archiv,
  Import, Restore, Merge/Ordnung/Konflikte) wandern in einen **portablen
  .NET-Kern** hinter einer C-ABI, gemeinsam für Chromium-Mac und WebKit-iOS.
- Grundregel: Ein Kernbefehl besitzt den semantischen Übergang; Engine-Adapter
  melden korrelierte Abschlüsse und werden nie zweiter Schreiber.
  Prozesslokale Quittungen verhindern doppelte Promotion oder späte Dismisses.
- Die gebaute Engine wird unter einem Hash aller Engine-Eingaben
  veröffentlicht, UI-Änderungen brauchen keinen Chromium-Build.
- DRM vorerst per WebKit-Reload-Fallback.
- Erklärtes Fernziel: Windows, Linux und Android nur über neue UI-Schichten.

## Einordnung

Gleich wie Ahoi: Chromium auf dem Mac, WebKit mobil, CloudKit, Arc-artiges
Modell, native Chromium-Dienste, gepinnte Quelle, kleine Fork-Fläche, ein
Sync-Format. Zeile 1 des Vergleichs vom 12. September („WebKit nicht
übernehmen“) ist damit gegenstandslos; Zeilen 6, 32 und 33 (Seitenkompatibilität,
uBlock-Grenze, Extension-Quellen) beschreiben Crests alten WebKit-Stand und
gelten für den Chromium-Build nicht mehr unverändert. „Volle
Chromium-Extensions“ ist kein Alleinstellungsmerkmal mehr.

Bewusst anders bleiben:

| Crest | Ahoi-Entscheidung | Grund |
| --- | --- | --- |
| SwiftUI ersetzt Chromium-Oberfläche | nicht übernehmen | Crest muss Berechtigungs-, Dialog-, Seitenleisten- und Popup-Oberflächen nachbauen (Release-Notes); bei Ahoi bleiben sie nativ Chromium. Native Anmutung über Liquid-Glass-/Politurvorgaben. |
| zweite Mac-Engine | nicht übernehmen | verdoppelt die Abnahmematrix |
| ungoogled-chromium als Basis | nicht als Basis | kostet Safe Browsing und Widevine; Listen als Audit-Checkliste nutzen |
| WebKit-Fallback für DRM | nicht übernehmen | echtes Widevine ist Ahois Vorteil |
| .NET-Kern | nicht übernehmen | zusätzliche Laufzeit im Browserprozess; Ahois Risiko ist über Konformitätsprüfung günstiger zu senken |

Verbleibende Ahoi-Schwerpunkte: uBlock Origin Classic (Crests MV2-Status nicht
geprüft), Widevine und Safe Browsing, HTTP-Auth und Developer Toolkit,
Workspace-Sitzungen bei globalen Diensten, Browser-Setup-Sync, Remote Control.

## Übernommene Prinzipien

Die fünf Nachschärfungen H1–H5 (Sync-Konformität, eine Schreibhoheit,
Performance-Methodik, Engine-Eingabeschlüssel, Crest-Referenz und
Netzwerk-Stille) sind im Zielprompt mit Definition of Done beschrieben.
Crest steht unter MPL-2.0; Codeübernahmen brauchen eine dateibezogene Prüfung.

Quellen: [Reddit-Beitrag](https://www.reddit.com/r/CrestBrowser/comments/1wplk7h/chromium_and_new_core/),
[Branch chromium-control-plane](https://github.com/pauljoda/Crest/tree/chromium-control-plane),
[Experimental 0.6.143](https://github.com/pauljoda/Crest/releases/tag/experimental-0.6.143-chromium-control-plane-2026-09-23-1140-r9.1).
