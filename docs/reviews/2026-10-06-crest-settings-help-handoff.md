# Automatisches PiP finden und Ahoi freiwillig ausprobieren

Source-Handoff für Pakete 5 und 6 des [Integrationsplans](2026-10-06-crest-integration-plan.md). Aufbauend auf dem übernommenen Dichte-Frontend `181a062a`/`8b033837`; die Settings-WebUI ist durch den konsumierten Desktop-Handback freigegeben. Das gemeinsame GRIT-/Patchstack-Wiring liegt ausschließlich im [Handoff-Patch](2026-10-06-crest-settings-help-handoff.patch).

Die Ahoi-Einstellungen verlinken zur bestehenden nativen Website-Einstellung `chrome://settings/content/autoPictureInPicture`. Die Beschriftung erklärt die Medien-Eignung und die weiterhin zuständigen Website-Berechtigungen Fragen/Zulassen/Blockieren. Der [Auto-PiP-Source-Commit](2026-10-06-crest-auto-pip-handoff.md) verändert weder diese Entscheidungen noch den Settings-Store. Die Route wurde im offiziellen Chromium-Source am exakten M155-Pin [nachgelesen](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/resources/settings/route.ts).

Die aufklappbare freiwillige Aufgabenhilfe enthält vier manuelle Schritte: eigenen Übungs-Workspace erstellen, zwei Seiten in einem Ordner speichern und sortieren, Mehrfachauswahl/Split View ausprobieren, über Workspace-Wechsel und Befehlsleiste zurückfinden. Native Checkboxen halten den Fortschritt nur in der geöffneten Settings-Seite fest. „Übungshäkchen zurücksetzen“ leert ausschließlich diese Checkboxen; es gibt keine Workspace-, Tab-, Profile-, Sync- oder Netzwerk-Aktion. Eine Übungs-Workspace-Factory ist dafür nicht erforderlich. Erstellen und späteres Löschen laufen über die bestehenden Dialoge und ihre bestehenden Bestätigungen.

## Integration und Source-Prüfung

Der Commit ändert direkt nur `ahoi_page.html.ts` und `ahoi_page.ts`. Native Elemente und die vorhandenen CSS-Regeln werden verwendet; keine neue Abhängigkeit, Build-Datei, Handler-Nachricht oder Pref. Neun neue Texte haben englische GRIT-Definitionen und deutsche Übersetzungen sowie Einträge im vorhandenen Settings-Stringprovider. Die Message-IDs wurden mit dem offiziellen [GRIT-Fingerprint](https://github.com/chromium/chromium/blob/16c3e55476d3564bea713314b2fff638749ce3e6/tools/grit/grit/extern/tclib.py) berechnet und gegen zwei bestehende Dichte-IDs geprüft.

Der GRIT-Patch setzt den **bereits angewendeten Dichte-Handoff** voraus. Reihenfolge im gemeinsamen Source-Kandidaten:

```sh
git apply docs/reviews/2026-10-06-crest-density-handoff.patch
git apply docs/reviews/2026-10-06-crest-tabgroups-handoff.patch
git apply docs/reviews/2026-10-06-crest-folder-preview-handoff.patch
git apply --check --whitespace=error-all docs/reviews/2026-10-06-crest-settings-help-handoff.patch
git apply docs/reviews/2026-10-06-crest-settings-help-handoff.patch
```

Am 6. Oktober 2026 wurden alle vier Patches in dieser Reihenfolge auf eine isolierte Kopie der betroffenen Source-Dateien angewendet: jeweils Anwendbarkeit/Source-Whitespace PASS, Syntax der daraus entstandenen inneren Chromium-Patchserie PASS. Die reservierten Sidebar-/Session-/Patchstack-Dateien im tatsächlichen Worktree blieben unberührt. Frontend-Diff, vorhandene Locale-/CSS-/Routing-Referenzen und eigene Schreibgrenze geprüft; kein Build oder Browserlauf. Keine lokalen Mock-Tests für diese reversible statische Hilfeseite.

## Offene Abnahme beim Desktop-Owner

Gemeinsamen Kandidaten bauen und exakt diesen installierten Browser sichtbar prüfen: Deutsch/Englisch, Settings-Suche und schmale Fenster, Hilfe per Tastatur öffnen, Checkboxen setzen und nur diese zurücksetzen. Existierende Workspaces/Tabs/Anmeldungen müssen dabei unverändert bleiben. PiP-Link muss zum nativen Website-Dialog desselben Profils führen; Ask/Allow/Block dürfen durch Öffnen des Links nicht geändert werden. Die tatsächliche Auto-PiP-Reise bleibt zusätzlich entsprechend ihrem eigenen Handoff offen. Compiler-/GRIT-, Runtime-, Defaultbranch-/Push- und Auslieferungsabnahme wird nicht aus den Source-Checks abgeleitet.
