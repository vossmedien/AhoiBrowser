# AhoiBrowser Mobile physical-device preflight

Stand: 30. August 2026
Quellbasis: `f4cf038580c3622772960532eb0a167ec9156f6f`

## Kandidat

- Xcode 26.6 (`17F113`), iPhoneOS SDK 26.5
- Debug, `arm64`, Mindestversion iOS 26.0
- Bundle ID `app.ahoibrowser.AhoiBrowser`
- Version/Build `0.1 (1)`
- Apple-Development-Team und Zertifikatssubjekt lokal geprueft; personenbezogene
  Kennungen werden im oeffentlichen Repository nicht wiederholt
- signierter Candidate-CDHash
  `4d5c36c2c0bd007015ff7ef46f8048b416c9c261`
- Zielgeraet: iPhone 16 Pro Max, iOS 26.6 (`23G71`), Developer Mode aktiv

Der Build verwendete eine temporaere, nicht versionierte xcconfig. CloudKit-,
Keychain-, Push- und Default-Browser-Entitlements wurden fuer diesen lokalen
UI-Kandidaten bewusst nicht angefordert. Leere oder erfundene Produktionswerte
waeren keine gueltige Release-Evidenz.

## Verifizierte Schritte

1. Physischer `iphoneos`-Build mit automatischer Development-Signierung.
2. Strikte Bundle-Signaturpruefung erfolgreich.
3. Signierte App-Entitlements: nur `application-identifier`, Team-ID und
   `get-task-allow`; kein CloudKit und kein Default-Browser-Recht.
4. Xcode-verwaltetes Development-Profil fuer genau diese Bundle-ID und das
   angeschlossene iPhone, gueltig bis 29. August 2027.
5. Installation ueber CoreDevice erfolgreich.
6. Sichtbarer Kaltstart ueber die lokale iPhone-Synchronisierung erfolgreich.
7. Direkte HTTPS-Navigation zu `https://example.com` mit sichtbarem Inhalt und
   Origin in der Bottom-Bar erfolgreich.
8. Gruppierte Browseraktionen und neuer privater Tab sichtbar geprueft.
9. Nur den Ahoi-Prozess gezielt beendet; beim erneuten Start wurde der normale
   Example-Tab wiederhergestellt, der private Tab dagegen nicht.

## Runtime-Evidenz

Der erste programmatische Start wurde von SpringBoard wegen des damals
gesperrten iPhones abgewiesen. Nach lokaler Mac-Authentifizierung wurde das
Geraet ueber iPhone-Synchronisierung bedient und die folgenden sauberen,
Ahoi-only Aufnahmen gesichert:

- `iphone-16-pro-max-01-cold-launch.png`
- `iphone-16-pro-max-02-example-https.png`
- `iphone-16-pro-max-03-browser-actions.png`
- `iphone-16-pro-max-04-private-tab.png`
- `iphone-16-pro-max-05-normal-restore.png`

Damit sind Installation, Launch und dieser begrenzte Development-Smoke belegt.
Die Aufnahmen beweisen nicht die vollstaendigen Cookie-/Storage-, History-,
Sync-, Accessibility-, Permission- oder Fehlerszenarien der jeweiligen
Registry-Journeys. Alle `MOB-USER-*`- und `IOS-*`-Status bleiben deshalb
unveraendert `NOT_RUN`.

## Nicht durch diesen Kandidaten belegt

- Apples verwaltetes Default-Browser-Entitlement und Systemauswahl
- reale CloudKit-Replikation, Push, Shared-Keychain-Key-Lifecycle oder Mac-
  Gegenstelle
- Distribution-Signatur, Archive/IPA, App Store Connect oder TestFlight
- physisches iPad; das vorhandene iPad der 6. Generation mit iPadOS 17.7.10
  liegt unter dem bewusst gesetzten iOS/iPadOS-26-Deployment-Target
- vollstaendige VoiceOver-, Dynamic-Type-, Datei-, Permission-, Background-
  und Fehlermatrix
