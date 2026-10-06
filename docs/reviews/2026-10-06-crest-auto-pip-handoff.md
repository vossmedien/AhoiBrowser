# Native Auto-PiP-Fähigkeit für den gemeinsamen Ahoi-Kandidaten

## Verhalten und Umsetzung

Ahoi bietet Chromiums browserinitiiertes automatisches Picture-in-Picture auch für geeignete Videoplayer an, die keinen eigenen MediaSession-Handler für `enterpictureinpicture` registrieren. Dafür ergänzt `ahoi/app/startup_policy.cc` die bereits vorhandene Feature-Liste um `BrowserInitiatedAutomaticPictureInPicture`. Es entstehen keine eigenen Medienobserver, Player, PiP-Fenster oder Persistenzmodelle.

Die native Berechtigung `AUTO_PICTURE_IN_PICTURE` bleibt maßgeblich. Die Aktivierung setzt weder eine globale noch eine Website-Freigabe auf Allow. Ask/Block/Allow, der native Dialog, Medien-Eignung, sichere URL, vorhandenes PiP und Rückkehr in den Ausgangstab werden weiterhin von Chromium behandelt. Der Nutzer kann automatisches PiP über die native Website-Einstellung erlauben oder blockieren.

## Bezug zur tatsächlichen Browserbasis

Geprüft am gepinnten Chromium-Commit `16c3e55476d3564bea713314b2fff638749ce3e6` (155.0.8059.26):

- [Blink-Feature](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/third_party/blink/renderer/platform/runtime_enabled_features.json5): `BrowserInitiatedAutomaticPictureInPicture` ist vorhanden, aber noch `experimental`.
- [Nativer TabHelper](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.cc): Browserinitiierter Eintritt folgt der MediaSession-Fähigkeit; Eignungs- und ContentSetting-Prüfung bleiben erhalten. Rückkehr beendet ausschließlich das automatisch geöffnete PiP über den bestehenden WindowManager.
- [Öffentliche Funktionsbeschreibung](https://developer.chrome.com/blog/automatic-picture-in-picture-media-playback): Website-initiiertes Auto-PiP und seine Berechtigungsoberfläche existieren bereits. Browserinitiiertes Auto-PiP ergänzt den Fall ohne Website-Handler.

Da diese spezielle native Fähigkeit am Pin noch experimentell ist, bleibt ihre sichtbare Abnahme offen. Eine erfolgreiche Compilation oder die Feature-Zeichenkette allein wäre kein Verhaltensnachweis. Bei einer konkreten Regression wird nur diese zusätzliche Aktivierung zurückgenommen; das vorhandene manuelle und Website-initiierte PiP bleibt bestehen.

## Source-Prüfung und offene Abnahme

- Eigener Diff und `git diff --check`: PASS.
- Bestehende Startup-Policy-Regression erweitert: wiederholte Anwendung, konflikthafte Disable-/Trial-Einträge und Erhalt fremder Argumente. Der Test ist vorbereitet, wegen der vereinbarten Build-Ownership hier nicht kompiliert/ausgeführt.
- Keine ContentSetting-Defaults, Privacy-Defaults oder Website-Freigaben geändert.
- Keine Builds, Inhouse-Zugriffe, Installationen oder Runtime-Tests durch diese Session.

Desktop-Owner übernimmt den Commit in den nächsten gemeinsamen Kandidaten und prüft zuerst sichtbar:

1. Geeignetes Video nach bewusstem Start, ohne registrierten Website-PiP-Handler: beim Tabwechsel nativer Auto-PiP-/Berechtigungsweg, ohne erzwungenes Allow. Ablehnen und Block müssen wirksam bleiben.
2. Nach ausdrücklichem Allow öffnet Auto-PiP beim Tabwechsel; Rückkehr in den Ausgangstab schließt das automatisch geöffnete Fenster. Manuelles Schließen erzeugt keine sofortige Wiederöffnung.
3. Manuell geöffnetes PiP und ein bereits vorhandenes anderes PiP werden nicht unerwartet ersetzt oder geschlossen; pausierte/ungeeignete Medien lösen kein Auto-PiP aus.
4. Workspace- und Split-Wechsel erhalten den richtigen Tab-/Sitzungskontext und Playback. Reguläres Website-initiiertes PiP bleibt funktionsfähig.
5. Danach bestehende fokussierte Startup-Policy-/Medienregressionen ausführen. Kandidatenrevision und tatsächliche Ergebnisse im Desktop-Checkpoint festhalten.

Eine besser auffindbare Verknüpfung zur vorhandenen nativen Auto-PiP-Einstellung folgt nach dem WebUI-Handoff des Dichte-Workers. Source-Übergabe bedeutet noch keine ausgelieferte Funktion.
