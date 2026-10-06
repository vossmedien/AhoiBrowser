# AhoiMobile ADR0012 Step1: Popup und Site-Berechtigungen

Rechercheauftrag `AHOI-ADR0012-POPUP-PERMS-20261006`, Delegation `db98dafe`.
Abruf: 6. Oktober 2026, ab 21:46 UTC. **Übergabe zur Root-Prüfung;
keine Produkt-, Runtime-, Rechts- oder App-Store-Abnahme.**

## Ergebnis und Bindung

Der kleinste öffentliche Popup-Weg bleibt im vorhandenen SwiftUI-Browser:
`performAction(for: nil)` öffnet die Default-Action der gebündelten Fixture;
der Controller-Delegate präsentiert WebKits fertigen `popupViewController`.
Das belegt nur deren Storage-Popup, keine tabbezogene Extension-Unterstützung.
Explizite Site-Rechte lassen sich direkt am jeweiligen Extension-Kontext setzen.
Die derzeitigen pauschalen Grants müssen für diesen Nachweis entfallen.
**Site-deny allein beweist keine Abschaltung der globalen DNR-Blockregel.**

| Beleg | Tatsächlich beobachteter Stand |
| --- | --- |
| Sichtbare Cockpit-Session | `EF33B317-43F5-43CB-B040-DAB61F4ECE36`, „AhoiBrowser · Mobile-Popup-/Berechtigungsrecherche“; `cockpit_sessions(cwd:)` erkennt „du“, eigenen Worktree und Delegation. |
| Native Ausführung | Thread `01a1132e-81bb-78b1-8311-390b0ada834c`; laufender `codex` PID 6449. Eigener Rollout `rollout-2026-10-06T23-46-17-01a1132e-81bb-78b1-8311-390b0ada834c.jsonl`: `session_meta` nennt `codex-tui`, CLI `0.160.1`, Anbieter `openai`; `turn_context` belegt tatsächlich `gpt-6.1-sol`, `xhigh`, `never`, `danger-full-access` und diesen CWD. |
| Account | Live-Zuordnung in `Terminal Cockpit/Session Insight/sessions.json`: diese Session und dieser native Thread → Account `DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5`. Aktives `CODEX_HOME` und nativer Rollout liegen unter genau dessen `Accounts/<UUID>/CodexHome`. Bezeichnung „Caeli“ stammt aus dem Auftrag; keine zusätzliche serverseitige Organisationsabfrage, kein Credential-/Accountwechsel. |
| CWD / Branch / Basis | `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-mobile-popup-berechtigungsrecherche-db98`; `cockpit/mobile-popup-berechtigungsrecherche-db98`; `c744ad3b61d9e389d7283852c478813ef49fbf11`. `pwd`, Git HEAD/Branch/Worktree und native Metadaten stimmen überein. |
| Eigenes natives Goal | Erstes `get_goal`: leer; einmal `create_goal` mit dem unveränderten Auftragsziel. Danach `get_goal`: `active`, `createdAt=1791323203`, identischer Thread/Zieltext. Die native Antwort liefert `threadId`, keinen separaten `goalId`; Goal-Identität ist dieser Thread plus Anlagezeit. Abschluss/Verbrauch stehen in der nativen Rückgabe. |
| Ownership | Eigener Worktree zunächst sauber, Berichtspfad neu; keine Überschneidung in `cockpit_sessions`. Root `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread `01a11179-cb6b-7dc1-9e53-f3d72fefc198`, behält Source/Build/UI/Release und sein Master-Goal. Keine fremden Goal-Aufrufe oder Änderungen. |

Gelesen: anwendbare `AGENTS.md`, README, aktueller Abschnitt des
[Mobile-Checkpoints](../ACTIVE_MOBILE_CHECKPOINT.md), [ADR0012 §4](../decisions/0012-workspace-merge-and-mobile-web-extensions.md),
[bestehende Machbarkeitsgrenze](../../outputs/AhoiBrowser-Mobile-uBlock-Feasibility.md),
relevante Mobile-/Privacy-Architektur und DCO-/Lane-Regeln.
Der Runtime-Pfad wurde zuerst per `rg --files` bestätigt. Gelesener Flow:
`Sources/AhoiMobileCore/{MobileWebExtensionRuntime,CompanionExtensionSetupSection,MobileBrowserControllerWebPageLifecycle,MobileBrowserController,AhoiMobileBrowserView,MobileBrowserActionsSheet}.swift`,
alle fünf Dateien unter `WebExtensionSpike/`, Runtime-Tests und
`MobileWebExtensionSpikeUITests` einschließlich `MobileADR0012UITestSupport`.
Alle genannten Pfade liegen unter `apps/AhoiMobile/`.

Bytevergleich mit Main-Kandidat `13423f52ced6744842dfd2fea16f9aadda1e5f3f`:
Runtime, Setup, Page-Lifecycle, BrowserController, ActionsSheet, Fixture und
beide Test-Flows sind identisch. `AhoiMobileBrowserView` enthält hier zusätzlich
nur den Harbor-Callback `recentTabPreview`; Popup-relevante Sheet-/onDismiss-Seams
sind unverändert. Die gesamte Mobile-Basis ist wegen weiterer Sync-/Flick-Arbeit
**nicht** gleichzusetzen. Checkpoint/Root melden Files-Import/Load/Unload sichtbar
belegt; finale `13423f52`-Abnahme offen. Hier wurde nichts ausgeführt oder abgenommen.

## Quellen und versionierter SDK-Vertrag

Lokal gelesen: `/Applications/Xcode.app`, `xcodebuild -version` →
**Xcode 27.0 / 27A266a**; `xcrun --sdk iphoneos` → **27.0 / 24A430**,
SDK `.../Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk`.
Unter `System/Library/Frameworks/WebKit.framework/` wurden Controller-, Delegate-,
Action-, Context-, Tab-, MatchPattern-, Configuration-Header und
`Modules/WebKit.swiftmodule/arm64e-apple-ios.swiftinterface` abgeglichen.
Die Swift-Schnittstelle nennt Apple Swift **6.4 / swiftlang-6.4.0.31.4**,
WebKit-Modul **7625.1.29.10.28**, Target `arm64e-apple-ios27.0`.
SHA256: Delegate-Header `c632dabf5622615808608ee07300b3992dadcf9dde56c41f3d6e278160b14238`;
Swift-Interface `9d7ad9903b8ce8881430f9d6369d5963aa7d2845c31f3041a715e818186f17a1`.

Context7 `resolve_library_id(WebKit)` scheiterte an „Monthly quota exceeded“.
Fallback: aktuelle Apple-DocC-Originaldaten unter `developer.apple.com/tutorials/data/documentation/`;
die normale HTML-Seite verlangt JavaScript. Diese Dokumentation ist nicht auf
SDK27 eingefroren; Availability, Selector, Nullability und `NS_SWIFT_NAME` wurden
deshalb gegen obige Header geprüft. Kein SDK installiert, kein Build/Compilerlauf.

Alle folgenden WebExtension-Verträge gelten ab **iOS/iPadOS/Mac Catalyst 18.4,
macOS 15.4, visionOS 2.4**. Header markieren Delegate, Context, Action, Tab und
Configuration mit `WK_SWIFT_UI_ACTOR`; `WKFoundation.h`/Foundation lösen das zu
**`@MainActor`** auf. `WebPage.Configuration.webExtensionController` ist im
Swift-Interface ebenfalls MainActor, ab **iOS 26.0**. Swift-Namen der Action und
MatchPattern sind `WKWebExtension.Action` / `.MatchPattern`, keine geratenen
Top-Level-Swift-Typen.

Die Deklarationen unten geben Apples DocC-Completion-Formen wieder, ergänzt um
die belegte Methoden-Isolation. Die Objective-C-Importe stehen nicht ausgeschrieben
im Swift-Overlay; exakte zusätzliche Importer-Attribute der Closures
(`@Sendable`/`@preconcurrency`) wurden ohne Compilerlauf **nicht** festgestellt.
Root soll diese beim ohnehin vorgesehenen Kandidatenbuild prüfen, keine
`nonisolated`- oder `@unchecked Sendable`-Umgehung ergänzen.

```swift
// Optional requirements of WKWebExtensionControllerDelegate; all return Void.
@MainActor func webExtensionController(
    _ controller: WKWebExtensionController,
    presentActionPopup action: WKWebExtension.Action,
    for context: WKWebExtensionContext,
    completionHandler: @escaping ((any Error)?) -> Void)

@MainActor func webExtensionController(
    _ controller: WKWebExtensionController,
    promptForPermissions permissions: Set<WKWebExtension.Permission>,
    in tab: (any WKWebExtensionTab)?, for extensionContext: WKWebExtensionContext,
    completionHandler: @escaping (Set<WKWebExtension.Permission>, Date?) -> Void)

@MainActor func webExtensionController(
    _ controller: WKWebExtensionController,
    promptForPermissionToAccess urls: Set<URL>,
    in tab: (any WKWebExtensionTab)?, for extensionContext: WKWebExtensionContext,
    completionHandler: @escaping (Set<URL>, Date?) -> Void)

@MainActor func webExtensionController(
    _ controller: WKWebExtensionController,
    promptForPermissionMatchPatterns matchPatterns: Set<WKWebExtension.MatchPattern>,
    in tab: (any WKWebExtensionTab)?, for extensionContext: WKWebExtensionContext,
    completionHandler: @escaping (Set<WKWebExtension.MatchPattern>, Date?) -> Void)
```

Quellen: [Popup-Delegate](https://developer.apple.com/documentation/webkit/wkwebextensioncontrollerdelegate/webextensioncontroller(_:presentactionpopup:for:completionhandler:)),
[API-Rechte](https://developer.apple.com/documentation/webkit/wkwebextensioncontrollerdelegate/webextensioncontroller(_:promptforpermissions:in:for:completionhandler:)),
[URLs](https://developer.apple.com/documentation/webkit/wkwebextensioncontrollerdelegate/webextensioncontroller(_:promptforpermissiontoaccess:in:for:completionhandler:)),
[Match-Patterns](https://developer.apple.com/documentation/webkit/wkwebextensioncontrollerdelegate/webextensioncontroller(_:promptforpermissionmatchpatterns:in:for:completionhandler:)).
DocC bietet zusätzlich `async throws` für Popup und `async -> (Set<T>, Date?)`
für die drei Prompts; Step1 braucht nur eine Form pro Requirement.

Popup-Callback kommt erst bei vollständig geladener Popup-WebView. Completion
einmal nach erfolgreicher Präsentation mit `nil`, bei Ablehnung/Fehler mit Error;
kein Warten bis zum Schließen. Permission-Completion enthält nur erlaubte
Elemente der Anfrage plus deren gemeinsames Ablaufdatum; leer bedeutet keine
Freigabe, `nil` beim Datum bedeutet ohne Ablauf. Fehlender Delegate oder zu
späte Antwort führt laut Vertrag zu deny; Apple nennt **keine konkrete Timeoutdauer**.
Tab kann `nil` sein und ist dann keine belegte Zuordnung zum ausgewählten Tab.
Abbruch/Tabwechsel/Unload müssen offene Antworten zeitnah genau einmal beenden.

Weitere relevante MainActor-Signaturen:

```swift
// WKWebExtensionContext
func action(for tab: (any WKWebExtensionTab)?) -> WKWebExtension.Action?
func performAction(for tab: (any WKWebExtensionTab)?) // -> Void
func setPermissionStatus(_ status: PermissionStatus,
    for permission: WKWebExtension.Permission, expirationDate: Date?) // -> Void
func setPermissionStatus(_ status: PermissionStatus,
    for url: URL, expirationDate: Date?) // -> Void
func setPermissionStatus(_ status: PermissionStatus,
    for pattern: WKWebExtension.MatchPattern, expirationDate: Date?) // -> Void
func permissionStatus(for url: URL,
    in tab: (any WKWebExtensionTab)?) -> PermissionStatus
// Same query overloads exist for Permission and MatchPattern.
var grantedPermissionMatchPatterns: [WKWebExtension.MatchPattern: Date] { get set }
var deniedPermissionMatchPatterns: [WKWebExtension.MatchPattern: Date] { get set }
var hasAccessToPrivateData: Bool { get set }

// WKWebExtension.Action
var associatedTab: (any WKWebExtensionTab)? { get } // weak; nil for default action
var presentsPopup: Bool { get }
var popupViewController: UIViewController? { get } // iOS-family only
var popupWebView: WKWebView? { get }
func closePopup() // -> Void

// WKWebExtension.MatchPattern
init(scheme: String, host: String, path: String) throws
// WKWebExtensionController
func load(_ extensionContext: WKWebExtensionContext) throws // -> Void
func unload(_ extensionContext: WKWebExtensionContext) throws // -> Void
```

Quellen: [action](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/action(for:)),
[performAction](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/performaction(for:)),
[API-set](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/setpermissionstatus(_:for:expirationdate:)-692ui),
[URL-set](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/setpermissionstatus(_:for:expirationdate:)-5q9id),
[Pattern-set](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/setpermissionstatus(_:for:expirationdate:)-7038f),
[URL-query](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/permissionstatus(for:in:)-96xaf),
[Pattern-Init](https://developer.apple.com/documentation/webkit/wkwebextension/matchpattern/init(scheme:host:path:)),
[load](https://developer.apple.com/documentation/webkit/wkwebextensioncontroller/load(_:)),
[unload](https://developer.apple.com/documentation/webkit/wkwebextensioncontroller/unload(_:)).

## Vorgeschlagene minimale Root-Implementierung

**Popup/Lifecycle:** Runtime behält gebündelten Kontext und einen Host stark;
`controller.delegate` ist laut SDK **weak**. Nur geladenen, eigenen Kontext
und normale Default-Store-Seite bei aktiver Szene anbieten. Debug-Spike-Aktion
im bestehenden Page-Actions-Flow; `presentAfterBrowserActions`/`onDismiss`
bereits vorhanden, also erst dessen Sheet schließen. Ein kleiner
`UIViewControllerRepresentable`-Anker im Browser hält eine UIKit-View in
derselben Szene. Deren tatsächlicher Presenter muss sichtbar, nicht bereits
in einer Präsentation und `view.window != nil` sein; kein globales KeyWindow.

Beim echten Tap `(context, sourceTabID, aktuelle Site, Request-Generation)`
festhalten und `performAction(for: nil)` aufrufen. Im Delegate erneut prüfen,
`presentsPopup` und `popupViewController` verlangen; Presenter präsentiert
**diesen WebKit-Controller**, ohne eigenes HTML-/WKWebView-Laden.
Für iPad `popoverPresentationController.sourceView` = lebender Anker,
`sourceRect` in dessen Koordinaten; iPhone darf UIKit adaptieren.
Action, Kontext, Popup und Callback bis Präsentationsabschluss halten;
Action/Kontext/Popup bis Dismissal halten, danach Referenzen freigeben.
Schließen-Schaltfläche im Spike-Host sowie Escape/Accessibility-Dismissal
vorsehen. UIKit-Dismissal schließt laut WebKit automatisch und entlädt die
Popup-WebView; bei selbst verwaltetem Containment explizit `closePopup()`.
Bei Tab-/Site-/Workspacewechsel, Quelltab-Schließen, Szene inaktiv oder
Kontext-Unload: erst Popup/Prompt abbrechen, dann Kontext entladen und ggf.
Files-Staging entfernen. Späte Delegate-Antworten anhand der Generation ablehnen.
Default-Action hat keinen `associatedTab`; die festgehaltene App-Zuordnung
ist eine UI-Sicherheitsgrenze, kein WebKit-Tab-Adapter.
[Popup-VC](https://developer.apple.com/documentation/webkit/wkwebextension/action/popupviewcontroller),
[closePopup](https://developer.apple.com/documentation/webkit/wkwebextension/action/closepopup()),
[UIKit present](https://developer.apple.com/documentation/uikit/uiviewcontroller/present(_:animated:completion:)),
[Anker](https://developer.apple.com/documentation/uikit/uipopoverpresentationcontroller/sourceview),
[sourceRect](https://developer.apple.com/documentation/uikit/uipopoverpresentationcontroller/sourcerect).

**Tab-Grenze:** `performAction(for: nil)` gewährt keinen tabbezogenen User-Gesture-
Nachweis; mit echtem Tab markiert WebKit den Gesture. Ein zukünftiger Adapter
braucht stabile UUID→Tab-/Window-Objekte, passende Controller-Zuordnung und
Open/Activate/Close-Nachrichten. SDK-Swift-Overlay belegt
`didActivateTab(_:previousActiveTab:)` und `didCloseTab(_:windowIsClosing:)`;
Controller-Nachrichten gehen an alle geladenen Kontexte, Context-Nachrichten
nur an diesen. `@MainActor optional func webView(for context: WKWebExtensionContext)
-> WKWebView?` muss eine WebView mit genau diesem Controller liefern; bei `nil`
warnt Apple vor fehlender Injektion/Modifikation. SDK27 `WebPage` exponiert keine
solche WebView und keine Tab-Konformität. Keine Introspektion/Privat-API oder
fingierte WebView. Root-Alternative für den Spike bleibt Default-Popup plus
explizite Kontext-Site-Rechte; volle Tab-APIs bleiben unbelegt.
[Tab-Vertrag](https://developer.apple.com/documentation/webkit/wkwebextensiontab/webview(for:)).

**Grant/deny und Scope:** Die zwei Auto-Grant-Schleifen für Host-Patterns
(Bundle/Files) ersetzen; API-Rechte `.storage`/DNR gesondert sichtbar zustimmen
lassen. Neuer Kontext startet ohne Site-Grants, ausdrücklich
`hasAccessToPrivateData = false`. Ein kleiner Spike-Dialog zeigt Extension,
exakten HTTPS-Host und Ablaufdatum: „Verweigern“, „Für diese Site erlauben“,
„Widerrufen“. Vorschlag für diesen Nachweis: ausschließlich `https`, exakter
Host ohne Wildcard, Pfad `/*`, Datum jetzt + 60 Sekunden; keine dauerhafte
UX-/Step2-Entscheidung. Pattern mit SDK-Initializer validieren. URL-Setter
konvertiert die URL in ein Match-Pattern; er garantiert **keinen einzelnen
Dokument-/Query-/Port-Scope**. Muster mit `*` oder `<all_urls>` erweitern Zugriff;
ein Host-Muster ist kein eTLD-/Subdomain-/Cookie-/Workspace-Grenzversprechen.

Nur `.grantedExplicitly`, `.deniedExplicitly`, `.unknown` sind setzbar;
implicit/requested-Zustände sind Abfrageergebnisse. Grant und deny bekommen
explizite Dates; `nil`/Overloads ohne Date bedeuten distant future.
Widerruf setzt das genaue Muster auf `.unknown` (erneut fragen) oder
`.deniedExplicitly` (weiter verweigern). Breitere/überlappende Grants müssen
ebenfalls entfernt werden; danach effektiven URL-Status prüfen. Die Dictionaries
enthalten beim Lesen keine abgelaufenen Einträge; Bulk-Set ersetzt jeweils
alles. Kein neues Berechtigungs-Repository/Sync: WebKit-Kontext bleibt Autorität.
Nach deny/expiry/revoke frisch navigieren; bereits injiziertes JS/DOM wird nicht
als rückwirkend entfernt behauptet. Permission-Delegates erlauben nur bestätigte
Anfrage-Teilmenge; ein `<all_urls>`-Request wird hier leer beantwortet, gezielte
Site-Freigabe separat gesetzt. Unbekannte/nil Tabs erhalten keine automatische
Zuordnung/Freigabe. Ein Callback ohne Freigabe ersetzt nicht das Setzen eines
dauerhaften `.deniedExplicitly` für den sichtbaren Site-Schalter.
[Grants](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/grantedpermissionmatchpatterns),
[Denials](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/deniedpermissionmatchpatterns).

**Normal/Private/defaultStore:** Bundle und Files sind zwei unabhängige Kontexte;
gleicher Anzeigename überträgt keinen Grant/Storage. Step1 prüft nur Bundle;
Files-Kopie während des Site-Nachweises entladen, sonst kann sie selbst injizieren.
Kontext-Site-Grants gelten für dessen angebundene normale Tabs, nicht nur den
UI-Quelltab. `attach` schließt Private und getrennte Workspace-Stores aus.
`usesDefaultWebsiteDataStore` prüft tatsächlich Identität mit dem injizierbaren
`normalWebsiteDataStore`; Produktionsdefault ist `.default()`, der Name allein
garantiert nicht den Systemdefault. Configuration `.nonPersistent()` betrifft
Extension-Persistenz. **Dokumentationsgrenze:** Apple/SDK-Header nennen allgemein
`.default()` als `defaultWebsiteDataStore`; der aktuelle öffentliche
WebKit-Konfigurator erzeugt bei `IsPersistent::No` ausdrücklich einen eigenen
nonpersistent Store. Daher weder Identität mit dem normalen Seitenstore noch
Persistenz aus dem allgemeinen Property-Text ableiten. Root liest am exakten
SDK27-Kandidaten `controller.configuration.defaultWebsiteDataStore.isPersistent`
und dessen Identität zum normalen Store zurück. Aktueller Upstream-Code ist
kein Binärbeweis für SDK27. Andere offene Tab-Stores können laut Property-Vertrag
unter Grants ebenfalls zugänglich sein; `.nonPersistent()` allein beweist keine
Normal-/Privattrennung. Private-Zugriff verlangt explizite Zustimmung, separaten
`WKUserContentController` und nonpersistent `WKWebsiteDataStore`; im Step1
weiterhin kein Controller/Adapter/Popup/Grant für private Tabs. Kein Safari-
Grantimport und keine Grantkopie zwischen Kontexten/Normal/Private.
[Configuration](https://developer.apple.com/documentation/webkit/wkwebextensioncontroller/configuration-swift.class/nonpersistent()),
[defaultStore](https://developer.apple.com/documentation/webkit/wkwebextensioncontroller/configuration-swift.class/defaultwebsitedatastore),
[Private-Vertrag](https://developer.apple.com/documentation/webkit/wkwebextensioncontext/hasaccesstoprivatedata),
[WebKit-Konfigurator](https://github.com/WebKit/WebKit/blob/0350073e737b173a5a7a80ad55a83d52e940b98d/Source/WebKit/UIProcess/Extensions/WebExtensionControllerConfiguration.cpp)
(Dateirevision vom 8. Juli 2026; SHA256
`ff988cfa82bc8bda7b565ad9a5fa109572da3431f870f3a610705b6e79f3239b`).

**Offene Runtimefrage DNR:** Fixture nutzt `.declarativeNetRequest` und eine
globale `block`-Regel. Apples DNR-Dokumentation fordert Host-Zustimmung ausdrücklich
für Redirect/modifyHeaders; daraus folgt kein allgemeiner deny-Schalter für
Blockregeln. Root muss deren Wirkung separat messen. Minimaler Kandidat:
Fixture-Recht auf `declarativeNetRequestWithHostAccess` begrenzen und beide
Grant-Schleifen sowie Manifest-Fokuscheck passend ändern; die erhoffte Bindung
von `block` an Site-Grants ist unter SDK27 **noch zu beweisen**. Bleibt die Regel
global aktiv, kann Root den Fixture-Ruleset zunächst global deaktivieren und
den Site-Nachweis auf Content-Zugriff begrenzen; das ist keine Abnahme von
„deny lässt die ganze Seite unverändert“. DNR-Site-Grenze dann offen halten.
[Apple DNR](https://developer.apple.com/documentation/safariservices/blocking-content-with-your-safari-web-extension).

## App-Store-Einordnung

[Originalregeln 2.5.2 und 4.7](https://developer.apple.com/app-store/review/guidelines/):
2.5.2 verlangt ein abgeschlossenes Bundle/Container-Modell und untersagt
nachgeladenen/installierten/ausgeführten Code, der App-Funktionalität verändert;
die begrenzte Bildungsausnahme ist hier nicht belegt. 4.7 nennt unter Bedingungen
auch Plug-ins/HTML5-/JavaScript-Software außerhalb des Binaries, mit Verantwortung
für deren Inhalte/Privacy, Zahlungen, Moderation, Softwareindex/Universal Links
und Altersgrenzen; 4.7.2 verbietet native API-Exposition ohne vorherige Apple-
Erlaubnis, 4.7.3 verlangt expliziten Consent je einzelner Software.
Die öffentliche WebKit-API ist deshalb keine automatische Ausnahme/Zulassung.
Vorgeschlagene Step1-Grenze: eigene gebündelte Fixture, Files nur vorhandener
DEBUG-/exakter-Byte-Spike, kein Code-Download/Selbstupdate; neue Fixture-Version
nur mit neuem App-Build. Allgemeiner Files-Installer/Store und juristische
Auslegung bleiben außerhalb dieses Berichts. Privacy-Manifest-/Update-Gesamtnachweis
von Step1 wird hier nicht geschlossen.

## Genau zwei sichtbare Abnahmewege für Root

1. **Fixture-Popup:** Exakten eigenen Kandidaten mit `-AhoiWebExtensionSpike`
   starten, normale Check-Seite mit frischem Token laden, Files-Kopie entladen.
   Über Page Actions „Ahoi Spike“ wählen: Actions-Sheet ist weg, echtes WebKit-
   Popup zeigt denselben Storage-Visits-Stand. Bestehende Fixture ist nur lesend;
   für „bedienen“ minimal einen Refresh-Button ergänzen, der denselben Wert neu
   liest. Refresh bedienen, sichtbar schließen, erneut öffnen. Erwartung: eine
   Präsentation, funktionierender Refresh/Close, Popup-WebView nach Dismissal
   entladen, Extension-Storage bleibt im Kontext. Nochmals öffnen und Quelltab
   wechseln/schließen bzw. dessen Kontext entladen: Popup verschwindet, keine
   späte Wiederpräsentation oder Verwendung des alten Tabs. Default-Popup wird
   ausdrücklich als solcher protokolliert.
2. **Site-Recht:** Nur Bundle-Kontext, frische HTTPS-Site A und anderer Host B.
   A explizit verweigern, frisch navigieren: kein Content-Script/Visits-Anstieg;
   unabhängige Seiten-Probes messen zusätzlich Control und blockierbaren Request,
   damit fehlende Extension-Injektion keine DNR-Falschannahme erzeugt. A gezielt
   für 60 Sekunden erlauben und frisch navigieren: Fixture-Report mit neuem Token,
   Visits-Anstieg und belegte DNR-Wirkung; B bleibt ohne Injektion. Nach Ablauf
   A frisch laden: Zugriff fehlt/erneute Zustimmung nötig. A erneut erlauben,
   widerrufen, frisch laden: ebenso kein Zugriff. A in normalem und privatem Tab
   vergleichen: normal entsprechend Site-State, privat niemals Report/Popup oder
   normaler Grant/Storage; privates Schließen ändert normalen State nicht.
   DNR-Block bei deny/expiry/revoke separat erwarten/prüfen; globales Blocking
   ist ein offener Fehler für die vollständige „unverändert“-Erwartung.

Ergänzende Fokuschecks **nur für Root, hier nicht ausgeführt**: drei
Permission-Callbacks (leer/Teilmenge/Date, exakt einmal auch bei Cancel/Unload),
abgelaufene und überlappende Patterns, Scheme/Host/Subdomain und malformed Pattern,
Kontextwechsel/Files-Grants ohne Übertragung, Default-/Private-/Separated-Attach
und tatsächlicher Extension-defaultStore (Persistenz/Identität),
fehlender Presenter/iPad-Anker, Release-/Launchargument-Grenze und unveränderte
Files-Byte-/Security-Scope-/Reentrancy-/Unload-Gates. Bestehende Runtime5 verwenden;
Matrix nur bei neuem Tab-/Store-/Autorisierungsseam erweitern.

Nächste minimale Root-Schritte: eigene `13423f52`-Abnahme abschließen; gebündelten
Kontext/Host halten; UIKit-Anker und Default-Action in vorhandene onDismiss-Seam
setzen; Site-Autogrants durch obigen Spike-Dialog/Delegate-Antworten ersetzen;
Fixture-Refresh und DNR-HostAccess-Kandidat begrenzt ergänzen; exakt die zwei
Wege plus nötige Fokuschecks am daraus gebauten Kandidaten prüfen. Keine
vollständige Browser-WebView-Migration aus dieser Recherche ableiten.

**Einzige offene Nutzerentscheidung:** nach vollständigem Step1 samt offenen
DNR-/Privacy-/Update-Befunden über Step2/v1 entscheiden. Runtime-Unsicherheiten
oben sind technische Root-Nachweise, keine zusätzliche Grundsatzbefragung.

## Eigene Prüfung und Rückgabe

Nur dieser Bericht ist writable; keine Source-/Checkpoint-/Build-/UI-/Teständerung,
keine Unterdelegation, kein Push/Integration. Pfadbezüge und Quellen wurden beim
eigenen Diffcheck geprüft; Commit/Zeilenumfang und `git diff --check`-Ergebnis
werden mit genau einer `cockpit_report(db98dafe)`-Quittung an Root übergeben.
Das eigene Recherche-Goal betrifft diese belegte Übergabe; Root-Prüfung und
Produktabnahme bleiben davon getrennt offen.
