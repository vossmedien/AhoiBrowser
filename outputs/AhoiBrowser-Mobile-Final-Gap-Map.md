# AhoiBrowser Mobile – Feature-Freeze-Gap-Map

Stand: `72e450df95fed128d191606b08988da7e8b93516`
Zielvertrag: `outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`

Diese Map bewertet den Quellstand vor der finalen Abschlusswelle. Ein Status
`implementiert` ist noch kein E2E-PASS. Sichtbare und externe Ergebnisse
werden erst an den späteren Feature-Freeze-Kandidaten gebunden.

## Klassifikation

| Journey | Status vor Abschlusswelle | Quellverantwortung und belegte Implementierung | Noch kontrollierbar zu schließen |
|---|---|---|---|
| `MOB-USER-01` | implementiert; Evidenzlücke | `MobileBrowserController.swift`, `MobileBrowserPolicies.swift`, `MobileAddressCommandSheet.swift`: Cold-Load, sichere URL-/Suchauflösung, sichtbare Origin und HTTPS-Navigation | finale sichtbare Cold-Launch-/Origin-Journey auf Freeze-Kandidat |
| `MOB-USER-02` | implementiert; Evidenzlücke | `MobileBrowserController.swift`, `MobileHarborDeckView.swift`, `MobileBrowserPreferences.swift`: konfigurierter Suchanbieter, Back/Forward/Reload/Stop | vollständige sichtbare Navigationsfolge ohne Doppelload |
| `MOB-USER-03` | implementiert; skalierte Evidenzlücke | `MobileBrowserController.swift`, `MobileTabSwitcherSheet.swift`, `MobileBrowserSessionSaveCoordinator.swift`: Create, Reorder, Rename, Close, Undo und revisionsserialisiertes Restore | 1-/5-/20-Tab- und Terminate/Restore-Journey sowie Runtime-Metrik |
| `MOB-USER-04` | implementiert; Evidenzlücke | `CompanionStore.swift`, `CompanionViews.swift`, `MobileBrowserSidebar.swift`, `AhoiMobileBrowserView.swift`: Save/Move/Reopen und tombstone-sichere Baumoperationen | vollständige sichtbare Baum-/Suche-/Workspace-Journey |
| `MOB-USER-05` | implementiert; Evidenzlücke | `MobileBrowserController.swift`, `MobileBrowserControllerWebPageLifecycle.swift`, `MobileBrowserModels.swift`, `MobilePrivateSceneShield.swift`: flüchtiger gemeinsamer Private-Store, privatefreie Session/History/Search/Sync und Privacy Cover | Prozess-Tod-, Cookie-/Storage- und negative Projektions-Journey |
| `MOB-USER-06` | pre-grant implementiert; externer Post-grant-Teil | `AppEntry.swift`, `MobileBrowserController.swift`, `MobileBrowserPolicies.swift`: sichere Cold-/Warm-URL-Aktivierung und persistenzarme Deduplizierung | pre-grant sichtbar erneut prüfen; echter Systemdefault erst nach Apple-Grant |
| `MOB-USER-07` | teilweise implementiert | `MobileWebDialogHost.swift`, `MobileBrowserControllerWebPageLifecycle.swift`, `MobileDownloadsSheet.swift`, Downloadtypen derzeit in `MobileBrowserPolicies.swift`: File Provider, WebKit-Datastore, sichere Namen, Fortschritt, Cancel, Quick Look, Share und Popup-Zuordnung | datensparsame normale Interrupted-Download-Recovery; private Artefakte weiterhin nie persistieren |
| `MOB-USER-08` | implementiert; Evidenzlücke | `MobileBrowserControllerWebPageLifecycle.swift`, `MobileBrowserPolicies.swift`, `MobileWebDialogPresenter.swift`, `MobileWebDialogHost.swift`: Kamera/Mikrofon/Motion, JS-Dialoge, Upload und Originbindung | sichtbare Allow/Deny/Cancel-/Mainframe-/Subframe-Matrix |
| `MOB-USER-09` | teilweise implementiert | native SwiftUI-Semantik, semantische Farben, SF Symbols, Reduce Motion/Transparency und zahlreiche Labels/Values in `AhoiMobileBrowserView.swift`, `MobileHarborDeckView.swift`, `MobileBrowserSidebar.swift`, `MobileTabSwitcherSheet.swift` | Dynamic-Type-Hauptaktionen, VoiceOver-Reihenfolge/Escape, erhöhter Kontrast und Rotationsmatrix gezielt härten und prüfen |
| `MOB-USER-10` | teilweise implementiert | `NavigationSplitView`/Sidebar, Compact-/Regular-Width-Präsentation, systemnative Listen/Buttons und vorhandene Browser-Shortcuts in `AhoiMobileBrowserView.swift`, `MobileBrowserSidebar.swift`, `MobileHarborDeckView.swift` | vollständige Tastaturbefehle sowie explizit prüfbare Fokus-/Pointer-/Multitasking-Verträge |
| `MOB-USER-11` | teilweise implementiert | Offline-/DNS-Klassifikation, Retry, Scene-Flush, Memory-Discard und Session-Restore in `MobileBrowserControllerFailures.swift`, `MobileBrowserController.swift`, `AhoiMobileBrowserView.swift` | eigene TLS-/Zertifikats-/WebContent-Darstellung, nicht schleifende Recovery und Download-Restore |
| `MOB-USER-12` | lokale Teile implementiert; Transport extern | `CompanionSyncBridge*.swift`, `CloudKitSyncProvider*.swift`, Key-Lifecycle/Rotation/Revocation und `RemoteCommandSigner.swift`: verschlüsselte Queue, Konflikt/Tombstone, Approval/Revocation, TTL/Replay/Wrong-Target/Signature-Rejection | kandidatengebundene lokale Negativmatrix; echter Mac–iPhone–iPad-Transport und volle `SYNC-*`-Matrix extern |
| `MOB-USER-13` | implementiert; Evidenzlücke | `MobileBrowserPolicies.swift`, `MobileBrowserController.swift`, `MobileLinkInteractionCoordinator.swift`: Reject lokaler/script-/credentialtragender/unbekannter Schemes und Origin-Bestätigung erlaubter Übergaben | sichtbare negative/positive Scheme-Journey |
| `MOB-USER-14` | teilweise implementiert | Live-Page-Cap, Background-/Memory-Discard und normales Session-Restore in `MobileBrowserController.swift`/`AhoiMobileBrowserView.swift` | reproduzierbare 1-/5-/20-Tab-, Launch-, Flush-, Discard-/Restore-, Speicher- und Energieinstrumentierung |
| `MOB-USER-15` | implementiert; breite Evidenzlücke | Harbor Deck, Focus Voyage, Workspace Canvas, private Farbwelt, Website-Tint/Fallback und systemnative Accessibility-Modi | finale iPhone/iPad × normal/privat × hell/dunkel × Tint × Accessibility-Sichtmatrix |

## Feature-Freeze-Arbeitsreihenfolge

1. `MOB-USER-07`: sichere Download-Recovery ohne Request-, Cookie-, Header-,
   Body- oder Privatdatenpersistenz.
2. `MOB-USER-11`: TLS/Zertifikat/WebContent klassifizieren, verständlich
   präsentieren und genau einmal deterministisch wiederaufnehmen.
3. `MOB-USER-09`/`10`: native Accessibility- und iPad-Interaktionen schließen,
   ohne zweite UI-Architektur.
4. `MOB-USER-03`/`14`: privacy-sichere Runtime-Telemetrie und reproduzierbare
   Launch-/1-/5-/20-Tab-/Discard-/Flush-/Energie-Gates ergänzen.
5. Alle übrigen Journeys quellseitig gegen Regression und erforderliche
   Testseams prüfen; danach Feature Freeze und erst dann die sichtbare Matrix.

## Bereits externe Teilgrenzen

- Apple Managed Default Browser Grant und echter Systemdefault (`MOB-USER-06`)
- entsperrtes physisches iPhone für CloudKit-Development-Mutation und TCC
- kompatibles physisches iPad mit iPadOS 26+
- signierter Mac-Gegenpart sowie für die volle `SYNC-01`–`SYNC-27`-Matrix zwei
  passend signierte Macs
- App Store Connect, TestFlight Review, öffentliche Angaben und Grant

Diese Grenzen entbinden nicht von den vorstehenden kontrollierbaren
Feature-, Simulator-, E2E-, Programm- oder Evidenzarbeiten.

## Geprüfter Zusatzwunsch: uBlock Origin

`uBlock Origin Classic` bleibt ein No-Go: die alte Safari-Portierung ist
eingestellt und der aktuelle Classic-Pfad zielt auf Firefox/Chromium. Das
offizielle `uBlock Origin Lite`-Safari-Paket ist dagegen seit den öffentlichen
Embedded-WebExtension-APIs von WebKit 18.4 grundsätzlich auch in einem
WebKit-basierten Browser prüfbar. Für AhoiMobile ist deshalb ein begrenzter,
gepinnt/provenienzgebundener `WKWebExtension`-Spike zulässig. Er gilt nur dann
als Go, wenn DNR, kosmetische Regeln/Scripting, Extension-Oberfläche,
Tab-Zuordnung, Site-Schalter und Private-Isolation auf demselben sichtbaren
Kandidaten vollständig funktionieren. Andernfalls endet der Spike fail-closed
und der dokumentierte native `WKContentRuleListStore`-Pfad übernimmt unter dem
eigenen Namen „Ahoi Content Blocker“. Details, harte Go-/No-Go-Kriterien,
Lizenzgrenzen und E2E-Matrix stehen in
`outputs/AhoiBrowser-Mobile-uBlock-Feasibility.md`. Der Spike folgt erst nach
dem grünen Mobile-Kernkandidaten und blockiert dessen Feature Freeze nicht.
