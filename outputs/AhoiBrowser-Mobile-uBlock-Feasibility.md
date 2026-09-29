# AhoiBrowser Mobile – uBlock-Origin-Feasibility

Stand: 2026-09-01

## Kandidatenentscheidung

AhoiBrowser Mobile darf **uBlock Origin Classic nicht unverändert integrieren**.
Der aktuelle Classic-Produktpfad zielt auf Firefox/Chromium; die frühere
Safari-Portierung ist entfernt und nicht mehr unterstützt.

Das offizielle **uBlock Origin Lite (uBOL) Safari-ZIP** ist dagegen seit den
eingebetteten WebExtension-APIs von WebKit 18.4 technisch grundsätzlich auch
für einen WebKit-basierten Browser prüfbar. AhoiMobile setzt iOS 26 voraus und
liegt damit oberhalb dieser Verfügbarkeitsgrenze. Das ZIP wird nicht sofort als
Produktfeature übernommen, sondern erhält genau einen begrenzten, fail-closed
WKWebExtension-Spike.

Die Entscheidung für einen Release-Kandidaten lautet daher:

1. **uBO Classic: No-Go.** Keine alte Safari-Portierung, keine Umbenennung und
   keine Behauptung von Desktop-Parität.
2. **Offizielles uBOL Safari-ZIP: Spike.** Nur die unten gepinnte, unveränderte
   Upstream-Version wird lokal aus dem App-Bundle geladen und sichtbar geprüft.
3. **uBOL Go:** nur wenn Netzwerkregeln, kosmetische Regeln/Scripting,
   Site-Schalter, Extension-Oberflächen und Private-Isolation mit öffentlichen
   WebKit-APIs im bestehenden `WebPage`-Aufbau funktionieren.
4. **uBOL No-Go:** sobald private APIs, ein nicht begrenzter Umbau des Browserkerns,
   unvollständige Schutzwirkung oder persistierende Private-Daten nötig wären.
   Dann wird ausschließlich der unten definierte native
   `WKContentRuleList`-Blocker weitergeführt.
5. In einem Kandidaten ist immer **genau ein Blocking-Backend aktiv**. uBOL und
   der native Fallback dürfen nicht parallel Regeln anwenden.

## Gepinnte uBOL-Provenienz für den Spike

Der am Stichtag autoritativ geprüfte Upstream-Kandidat ist:

| Feld | Pin |
| --- | --- |
| Source | `https://github.com/gorhill/uBlock` |
| Release-Projekt | `https://github.com/uBlockOrigin/uBOL-home` |
| Release/Tag | `2026.825.1619` |
| Tag-Commit | `080d4a2c9d8264e076daa512cf7bbd97f8a2ca6b` |
| Asset | `uBOLite_2026.825.1619.safari.zip` |
| Asset-Größe | `9,490,102` Bytes |
| Asset-Digest | `sha256:89dbaf3bfe913b77e959ac8473190b0992cd37c43714bf628713de13dce5bd94` |
| Code-Lizenz | GPL-3.0 |

Der Digest stammt aus den Metadaten des offiziellen GitHub-Releases. Er ist
ein Integritäts-Pin, aber **keine separate kryptografische
Publisher-Signatur** von Raymond Hill. Der Spike muss Tag, vollständigen
Commit, Assetname, Bytegröße und SHA-256 in seiner Provenienz festhalten. Ein
anderes oder nachträglich verändertes Asset ist fail-closed abzulehnen.

Das veröffentlichte Asset wurde zusätzlich bytegenau heruntergeladen und
lokal geprüft: Größe und SHA-256 stimmen mit den Release-Metadaten überein.
Das ZIP enthält unmittelbar `manifest.json`, `LICENSE.txt`, Locales,
HTML/CSS/JavaScript und deklarative Rulesets; es enthält weder `.app` noch
`.appex` oder `Info.plist` und keine Symlink-Einträge. Es ist damit tatsächlich
ein WebExtension-Ressourcenpaket und nicht bloß eine fertige Safari-App-Hülle.
Sein Safari-Manifest verlangt mindestens Safari/WebKit `18.6`; AhoiMobiles
iOS-26-Mindestziel liegt auch oberhalb dieser konkreten Paketgrenze.

Für den Spike wird das ZIP als unveränderte, gebündelte Ressource über
`WKWebExtension(resourceBaseURL:)` geladen. Es gibt weder einen Laufzeit-
Download noch selbstaktualisierenden Extension-Code. Ein späteres Update
erfolgt nur über einen neuen, erneut geprüften und durch Apple/Ahoi signierten
App-Build.

## Warum uBOL heute prüfbar ist

WebKit stellt seit iOS/iPadOS 18.4 `WKWebExtensionController` für
WebKit-basierte Browser bereit. Die iOS-26-SDK-Oberfläche von
`WebPage.Configuration` bietet ebenfalls einen `webExtensionController`; er
kann damit vor Erzeugung einer Ahoi-`WebPage` an deren Konfiguration gebunden
werden. Das aktuelle uBOL-Safari-Manifest verwendet Manifest V3 und unter
anderem:

- `declarativeNetRequest`;
- `declarativeNetRequestWithHostAccess`;
- `scripting`;
- `storage` und `unlimitedStorage`;
- `activeTab` und `alarms`.

Damit ist die frühere pauschale Aussage, eine Safari Web Extension könne nur
in Safari und nicht in einem eingebetteten WebKit-Browser laufen, für den
aktuellen Ahoi-iOS-26-Zielstand nicht mehr richtig.

## Zentrales WKWebView-/WKWebExtensionTab-Risiko

AhoiMobile erzeugt seine Seiten derzeit über SwiftUI `WebPage`. Die öffentliche
`WebPage`-API gibt den zugrunde liegenden `WKWebView` nicht an Ahoi heraus und
`WebPage` ist nicht öffentlich als `WKWebExtensionTab` ausgewiesen.

Ein vollständiger WebExtension-Host benötigt jedoch Window-/Tab-Adapter,
Lifecycle-Nachrichten und typischerweise
`WKWebExtensionTab.webView(for:)`. WebKit weist ausdrücklich darauf hin, dass
ohne die passende Webview kritische Funktionen wie Content-Injektion und
-Modifikation fehlen können. Daraus folgt:

- DNR-Netzwerkregeln könnten bereits über den an die Seitenkonfiguration
  gebundenen Controller funktionieren;
- kosmetische Filter, Scriptlets, Popup-/Options-Zuordnung, `activeTab` und
  tabbezogene Berechtigungen könnten unvollständig bleiben;
- ein Wechsel auf einen eigenen `WKWebView`-Wrapper wäre eine größere
  Architekturänderung und gehört **nicht** automatisch zum Spike.

Der Spike darf deshalb nicht allein aufgrund eines erfolgreich geladenen ZIPs
als bestanden gelten.

## Begrenzter uBOL-Spike

Der Spike bleibt vollständig in `apps/AhoiMobile` und umfasst höchstens diese
Integrationsseams:

- neu `Sources/AhoiMobileCore/MobileWebExtensionRuntime.swift`: gepinntes ZIP
  und Manifest prüfen, Extension-Kontext laden, getrennte Normal-/Private-
  Controller verwalten und Statusfehler veröffentlichen;
- neu `Sources/AhoiMobileCore/MobileWebExtensionHost.swift`: öffentliche
  `WKWebExtensionControllerDelegate`-, Window- und Tab-Adapter sowie
  Extension-Action/Popup/Options;
- `Sources/AhoiMobileApp/AppEntry.swift`: Runtime vor Erzeugung des ersten Tabs
  asynchron vorbereiten;
- `Sources/AhoiMobileCore/MobileBrowserControllerWebPageLifecycle.swift`:
  Controller vor `WebPage`-Erzeugung in `WebPage.Configuration` einsetzen;
- `Sources/AhoiMobileCore/MobileBrowserController.swift`: Öffnen, Auswahl,
  Navigation und Schließen von Tabs an den Host melden; den privaten Kontext
  nach dem letzten Private-Tab vollständig verwerfen;
- `Sources/AhoiMobileCore/MobileBrowserActionsSheet.swift`: Extension-Aktion
  und expliziter Site-Schalter;
- `Sources/AhoiMobileCore/CompanionSettingsView.swift`: Version, Provenienz,
  Berechtigungsumfang und Aktivierungsstatus sichtbar machen.

Nicht Bestandteil des Spikes sind beliebig installierbare Nutzer-Extensions,
ein Extension-Store, Remote-Code, private WebKit-APIs oder eine automatische
Portierung von uBO Classic.

### Harte Go-Kriterien

Alle Punkte müssen auf demselben installierten iOS-26-Kandidaten grün sein:

1. Das gepinnte ZIP wird aus dem Bundle geladen; Name, Version, Manifest-
   Berechtigungen und SHA-256 stimmen mit der Allowlist überein.
2. DNR blockiert die definierten Werbe-/Tracker-Requests, während erlaubte
   Kontrollrequests weiter funktionieren.
3. Ein kosmetisches Fixture wird verborgen und mindestens ein für den
   Basisschutz erforderlicher uBOL-Scripting-Pfad funktioniert. Ein reiner
   Netzwerkblocker ist kein vollständiger uBOL-Go.
4. Extension-Action, Popup oder gleichwertige offizielle uBOL-Oberfläche und
   Optionszustand gehören korrekt zum ausgewählten Tab.
5. Deaktivieren pro Site lädt die Seite kontrolliert neu und lässt nur diese
   Site passieren; erneutes Aktivieren stellt den Schutz wieder her.
6. Normale Einstellungen überleben einen Neustart. Private Tabs, Storage,
   Site-Ausnahmen und Extension-Zustände hinterlassen nach Schließen des letzten
   Private-Tabs und App-Neustart keine wiederherstellbaren Daten.
7. Öffnen, Schließen, Tabwechsel, Zurück/Vor, Reload, externe URL und
   Wiederherstellung verursachen weder falsche Tab-Zuordnung noch Crash oder
   sichtbare Navigationsrennen.
8. Die Integration benutzt ausschließlich öffentliche SDK-APIs und erfordert
   keinen Umbau auf einen eigenen Webview-Kern außerhalb des genehmigten
   Spikes.

### Harte No-Go-Kriterien

Ein einzelner Punkt beendet den Produktpfad zugunsten des nativen Fallbacks:

- DNR funktioniert nur teilweise oder erst nach dem ersten Seitenload;
- Cosmetic-/Scripting-Funktionen können mangels `WKWebView`-/Tab-Brücke nicht
  korrekt gebunden werden;
- Popup, Optionen oder Site-Ausnahmen verändern den falschen Tab;
- Private-Daten werden persistent oder mit dem normalen Profil geteilt;
- das ZIP benötigt zusätzliche Laufzeitdownloads oder ausführbaren Remote-
  Code;
- Host-Berechtigungen können nicht explizit, nachvollziehbar und minimal
  vergeben werden;
- private APIs, Signaturumgehung oder irreführende uBO-Classic-Parität wären
  nötig;
- die Integration führt zu reproduzierbaren Abstürzen oder nicht begrenzbarer
  Seiten-Breakage.

## Nativer `WKContentRuleList`-Fallback

Der native Pfad ist ein eigenständiger **Ahoi Content Blocker**, nicht „uBlock
Origin“. Er ist kleiner, kontrollierbarer und der verbindliche Fallback bei
einem uBOL-No-Go.

### Dateischnitt

Neu:

- `Sources/AhoiMobileCore/MobileContentBlockingModels.swift`;
- `Sources/AhoiMobileCore/MobileContentRuleListStore.swift`;
- `Sources/AhoiMobileCore/MobileContentBlockingCoordinator.swift`;
- `Sources/AhoiMobileCore/Resources/ContentBlocking/fallback-rules.json`;
- `Sources/AhoiMobileCore/Resources/ContentBlocking/manifest.json`;
- `Sources/AhoiMobileCore/Resources/ContentBlocking/THIRD_PARTY_NOTICES.md`;
- `scripts/build-content-rules.py` als ausschließlich offline laufender,
  deterministischer Generator;
- fokussierte Policy-/Store-Tests und sichtbare UI-Journeys unter den
  bestehenden Mobile-Testtargets.

Anpassen:

- `AppEntry.swift`: Bundle-Fallback vor dem ersten Tab vorbereiten;
- `MobileBrowserControllerWebPageLifecycle.swift`: kompilierte Rule-Listen vor
  dem ersten Load an den jeweiligen `WKUserContentController` hängen;
- `MobileBrowserController.swift`: Status, Seiten-Ausnahmen und Private-
  Lifecycle verwalten;
- `MobileBrowserPreferences.swift`: globalen Normalmodus-Schalter speichern;
- `CompanionSettingsView.swift` und `AhoiMobileBrowserView.swift`: globalen
  Zustand, Provenienz und Fehler darstellen;
- `MobileBrowserActionsSheet.swift`: Site-Schalter für den aktuellen Tab;
- `Resources/Localizable.xcstrings`: sichtbare Texte und Accessibility-Labels.

### Regel- und Laufzeitvertrag

1. Filterquellen werden außerhalb der App aus einer expliziten Allowlist in
   kanonisches WebKit-JSON übersetzt. In der App läuft kein allgemeiner
   ABP-/uBO-Parser.
2. V1 erlaubt nur begrenzte WebKit-Aktionen, zunächst `block` und optional
   geprüfte `css-display-none`-Regeln. Cookie-Blockierung, HTTPS-Umschreibung,
   Redirects, Headermanipulation und beliebige Scriptlets bleiben draußen.
3. Ein durch die App-Signatur geschützter Bundle-Regelsatz ist immer vorhanden.
   WebKit-Compilerfehler aktivieren nie einen halbfertigen Satz, sondern den
   letzten gültigen Satz beziehungsweise den Bundle-Fallback.
4. Jede `WebPage` erhält die Listen vor ihrer ersten Navigation. Ein späteres
   Anhängen mit sichtbarer Erstlade-Lücke ist nicht kandidatenfähig.
5. Ein Site-Schalter hängt sämtliche zugehörigen Listen für diesen Tab atomar
   ab beziehungsweise wieder an und lädt kontrolliert neu. Eine separate
   Ausnahme-Liste darf nicht darauf vertrauen, Regeln einer anderen
   `WKContentRuleList` mit `ignore-previous-rules` zu überstimmen.
6. Normale globale Einstellungen dürfen persistent sein. Private Site-
   Ausnahmen sind ausschließlich in-memory und werden mit dem letzten
   Private-Tab gelöscht.
7. Es werden weder blockierte URLs noch besuchte Hosts, private Ausnahmen,
   Navigationshistorie oder Seiteninhalte protokolliert oder übertragen.

## Sicherheits- und Updatevertrag

Remote-Updates sind optional und erst nach einem grünen Bundle-Kandidaten
zulässig. Ein Update darf ausschließlich Ahoi-generiertes, deklaratives
Content-Rule-JSON liefern; es darf niemals JavaScript, ein WebExtension-ZIP oder
sonstigen ausführbaren Code nachladen.

Das signierte Manifest enthält mindestens:

- Schema- und Kanalversion;
- monotone Paketversion und Anti-Rollback-Zähler;
- Erzeugungs- und Ablaufzeitpunkt;
- minimale Ahoi-App-Version;
- `keyID` und Ed25519-Signatur;
- Output-SHA-256, Bytegröße und Regelanzahl;
- für jede Quelle URL, exakte Revision, SHA-256, SPDX-Ausdruck und Copyright-
  Hinweis.

Die App enthält nur den öffentlichen Ahoi-Update-Schlüssel. Sie akzeptiert
Updates ausschließlich über eine fest erlaubte HTTPS-Origin, folgt keinen
Redirects außerhalb dieser Allowlist und validiert vor Aktivierung Signatur,
Hash, Größe, Schema, Version, Ablauf, Aktionen, Regex-Grenzen und
Quellenmanifest. Erst ein erfolgreich mit `WKContentRuleListStore`
kompilierter Kandidat wird atomar aktiviert. Schlüsselrotation benötigt eine
vorher signierte Übergangsregel; ein unbekannter Schlüssel ist fail-closed.

Updateanfragen enthalten keine aktuelle URL, keinen Host, keine Blockstatistik,
keine Ausnahme und keine andere Browsing-Metadaten. GitHub-Asset-Hashes allein
ersetzen diesen Signaturvertrag für Ahoi-Regelupdates nicht.

## Lizenz- und Markenvertrag

- uBO-Code und uAssets stehen unter GPL-3.0; Ahoi muss bei einer gebündelten
  uBOL-Distribution die korrespondierenden Quell-, Lizenz- und Notice-Pflichten
  mit exakten Revisionen erfüllen.
- Einzelne von uBO referenzierte Drittlisten besitzen abweichende,
  nichtkommerzielle oder ungeklärte Bedingungen. Es gibt keine pauschale
  Freigabe des gesamten uAssets-/Filterlisten-Ökosystems.
- Für den nativen Blocker wird jede Liste einzeln allowgelistet. Fehlen
  eindeutiger Lizenztext, Redistributionsrecht, Revision oder Provenienz, wird
  sie nicht ausgeliefert.
- Die App-Store-Verträglichkeit der GPL-Verteilung, Notices und
  Quellbereitstellung bleibt vor einem öffentlichen Build ein juristisches
  Gate gemäß `docs/LEGAL.md`.
- Der native Fallback wird ausschließlich „Ahoi Content Blocker“ genannt. Der
  Name und das Erscheinungsbild von uBlock Origin dürfen ohne separate Marken-
  und Herkunftsprüfung nicht für eine Ahoi-Eigenimplementierung verwendet
  werden.

Viele uBO-Filter sind zudem technisch nicht verlustfrei konvertierbar:
prozedurale kosmetische Filter, Scriptlets, HTML-/Headerfilter, Redirect-
Ressourcen und dynamische Regeln haben kein direktes
`WKContentRuleList`-Äquivalent. Der native Pfad verspricht daher bewusst keine
uBO-Parität, keinen Logger, keine dynamische Matrix und keinen Element-Picker.

## Sichtbare E2E-Matrix vor programmatischen Tests

Die Abnahme beginnt nach Build, Installation und minimalen Safety-Preflights
mit sichtbaren Journeys auf **dem exakt selben Kandidaten**. Erst danach laufen
Unit-, Integrations-, Repository- und statische Tests. Nach einer größeren
Korrektur wird zuerst die betroffene sichtbare Journey wiederholt.

| Journey | Sichtbarer und gebundener Nachweis |
| --- | --- |
| Kalter Start, Schutz aktiv | Einstellungen zeigen Backend, Version und gültige Provenienz; die erste Fixture-Navigation besitzt keine ungeschützte Erstladephase. |
| Netzwerkblockierung | Werbe-/Tracker-Fixture bleibt aus und ihr Request fehlt im gebundenen Fixture-Log; erlaubter Kontrollrequest und Hauptinhalt funktionieren. |
| Kosmetische Regel | Markiertes Testelement ist bei uBOL beziehungsweise einem unterstützten nativen CSS-Regelsatz nicht sichtbar, der restliche Inhalt bleibt bedienbar. |
| Site-Ausnahme | Ausschalten zeigt die Testressource nach kontrolliertem Reload und lässt den Fixture-Request ankommen; Einschalten blockiert ihn nach erneutem Reload. |
| Tab-Isolation | Zwei Sites in zwei Tabs behalten getrennte Ausnahmezustände bei Wechsel, Zurück/Vor, Reload und Tab-Schließen. |
| Normal/Private | Normale Regeln funktionieren in beiden Modi; Private-Ausnahmen und Storage sind nach Schließen des letzten Private-Tabs und Neustart verschwunden. |
| Relaunch/offline | Der Kandidat startet ohne Netz mit dem gebündelten beziehungsweise letzten gültigen Regelsatz und zeigt den korrekten Status. |
| Manipuliertes Update | Falsche Signatur, Hash, Ablauf oder Rollback wird sichtbar als abgelehnt ausgewiesen; der letzte gültige Schutz bleibt aktiv. |
| Breakage-Kontrolle | Login-/Formular-, Medien-, Download-, Permission- und externe-Link-Fixtures bleiben nutzbar; Ausnahmen reparieren bewusst blockierte Testfälle. |
| Lifecycle/Stabilität | Mehrfaches Öffnen/Schließen, Wiederherstellung und Wechsel normal/privat verursachen keinen Crash, falschen Tabbezug oder Regelverlust. |

Für den uBOL-Go müssen zusätzlich Popup/Options, Scripting und kosmetische
Filter sichtbar grün sein. Bleibt davon ein Pflichtpfad wegen der
`WKWebExtensionTab`-/`WKWebView`-Grenze rot, wird derselbe Kandidat nicht als
uBOL-fähig bezeichnet und der native Fallback übernimmt.

Wenn sichtbares E2E wegen eines echten Simulator-, Geräte-, Signing- oder
externen Gates nicht möglich ist, wird dieser konkrete Blocker ausgewiesen.
Unabhängige programmatische Tests dürfen trotzdem laufen, gelten aber nicht
als Ersatz für eine grüne E2E-Matrix.

## Autoritative Quellen

- uBlock Origin Repository und GPL-3.0:
  <https://github.com/gorhill/uBlock>
- eingestellte Classic-Safari-Portierung:
  <https://github.com/gorhill/uBlock/blob/master/platform/safari/README.md>
- offizielles uBOL-Safari-Buildverfahren:
  <https://github.com/gorhill/uBlock/blob/master/platform/mv3/README.md>
- offizielles Safari-Manifest von uBOL:
  <https://github.com/gorhill/uBlock/blob/master/platform/mv3/safari/manifest.json>
- gepinnter uBOL-Release:
  <https://github.com/uBlockOrigin/uBOL-home/releases/tag/2026.825.1619>
- WebKit 18.4 Embedded-WebExtension-Ankündigung:
  <https://webkit.org/blog/16574/webkit-features-in-safari-18-4/>
- Apple `WKWebExtensionController`:
  <https://developer.apple.com/documentation/webkit/wkwebextensioncontroller>
- Apple `WKWebExtensionTab.webView(for:)`:
  <https://developer.apple.com/documentation/webkit/wkwebextensiontab/webview(for:)>
- Apple `WKContentRuleListStore`:
  <https://developer.apple.com/documentation/webkit/wkcontentruleliststore>
- WebKit-Semantik von Content-Blocker-Aktionen und
  `ignore-previous-rules`:
  <https://webkit.org/blog/3476/content-blockers-first-look/>
- uAssets-Lizenz:
  <https://github.com/uBlockOrigin/uAssets/blob/master/LICENSE>
- offizielle uBO-Filterlisten-Lizenzmatrix:
  <https://github.com/gorhill/uBlock/wiki/Filter-list-licenses>
- offizielle uBO-Filtersyntax und nicht native Konstrukte:
  <https://github.com/gorhill/uBlock/wiki/Static-filter-syntax>
