# Phase-0-Spike: CloudKit und Companion-Sicherheitsmodell

Status: lokaler, buildbarer Architektur-Spike. Kein behaupteter CloudKit-Roundtrip.

## Zweck und Grenzen

Der Spike unter `spikes/cloudkit` klärt die gemeinsam nutzbaren Swift-Modelle und die Sicherheitsgrenzen für den macOS-Browser und die spätere iOS/iPadOS-Companion-App. Er enthält keine Produkt-UI, keine Secrets, keine registrierte Bundle-ID und keinen erfundenen iCloud-Container.

`EncryptedValue` ist in diesem Spike ausschließlich eine typisierte, opake Schema-/Transporthülle. Der Spike implementiert weder Verschlüsselung noch Schlüsselableitung, Schlüsselaufbewahrung oder Recovery; die Tests verwenden synthetische Bytes und beweisen keine kryptographische Sicherheit. Eine spätere Produktionseinbindung darf nur überprüfte Plattform- beziehungsweise Bibliotheksprimitive verwenden und keine eigenen kryptographischen Primitive einführen.

Der konkret kompilierte Apple-Adapter legt ausschließlich `EncryptedValue` über Apples `CKRecord.encryptedValues` ab und liest es auch nur dort wieder aus. Konflikt- und Änderungsmetadaten liegen dagegen bewusst als normale, query-/sortierbare CKRecord-Felder vor: Schema-Version, Entity-ID, Datenklasse, HLC, Geräte-ID, Order-Key-Sortierschlüssel und Tombstone-/Recovery-Metadaten. Ciphertext wird nie in ein normales Feld geschrieben. Das ist kein behaupteter Nachweis zusätzlicher anwendungsseitiger Verschlüsselung, Double Encryption, produktiver Schlüsselverwaltung oder Recovery.

## Enthalten

- typisierte UUIDs für Geräte, Workspaces, Tree Nodes und Tabs;
- Hybrid Logical Clock (HLC) mit deterministischer Konfliktordnung;
- fraktionale Order Keys mit Geräte-Tie-Breaker;
- Tombstones und payloadfreie Recovery-Metadaten;
- deterministischer Konfliktresolver: physical/logical HLC, dann Tombstone-Vorrang, Origin-Device, HLC-Node und zuletzt versionierte kanonische Value-Bytes;
- exhaustive Allow-/Deny-Klassifizierung aller derzeit vorgesehenen Sync-Datenklassen;
- Opt-in pro Developer-Asset;
- providerunabhängiger `CloudRecordTransport` und ein davor geschalteter `CloudSyncCoordinator`;
- bedingt kompilierter `AppleCloudKitAdapter` für Private Database und Custom Zone;
- separat testbarer `AppleCloudKitRecordCodec` mit Roundtrip-Prüfung der normalen Metadaten und des `encryptedValues`-Payloads;
- signierte Companion-Befehle `open`, `focus` und `close` mit Source-/Target-Device-ID, Nonce, fester TTL von fünf Minuten, Gerätefreigabe, Ed25519-Verifikationsadapter und atomarem Replay Store;
- lokales, explizites Remote-Control-Enablement-Gate;
- Ablehnung von URL-Userinfo, Incognito-Zielen, Massen-/Leer-Close, Custom-/Script-/File-/Data-Schemes und nicht freigegebenen Geräten;
- kontrollierte Fehler statt Prozessabbruch bei ungültigen Order Keys oder ausgeschöpftem HLC-Counter.

## Explizite Sync-Grenze

Standardmäßig erlaubt sind Workspaces, Tree Nodes, Order Keys, Tombstones, Recovery-Metadaten, normale Device Tabs, History, Appearance, ausdrücklich erlaubte Settings und Extension-Inventar. Developer-Assets benötigen ein Opt-in für die konkrete Entity-ID.

Immer abgelehnt werden:

- Cookies;
- Passwörter und Autofill;
- Site Data und Cache;
- Site Permissions;
- Extension Storage;
- Incognito-Daten;
- Keychain- und Header-Secrets;
- HTTP-Basic-/Digest-Auth-Secrets.

Diese Entscheidung liegt im `CloudSyncCoordinator` sowohl vor jedem Upload als auch nach jedem Fetch, bevor ein eingehender Datensatz an Aufrufer zurückgegeben wird. In beide Richtungen gilt derselbe konkrete Developer-Asset-Opt-in. Ein neuer Datenklassentyp muss bewusst in der exhaustiven Swift-`switch`-Anweisung einsortiert werden.

Nutzerseitige Löschung ist ausschließlich ein validierter Tombstone-Write. Entity, HLC, löschendes Gerät und Purge-Zeit müssen konsistent sein. Der öffentliche Spike-Transport besitzt keine Raw-Delete-Methode. Physische Tombstone-Bereinigung darf später nur als interne, bestätigungs- und aufbewahrungsgebundene Garbage Collection ergänzt werden; sie ist in diesem Spike nicht implementiert und nicht als bestanden behauptet.

Ungültige Order-Key-Codierungen und HLC-Counter-Überläufe liefern Fehler und lösen keinen `precondition`-Prozessabbruch aus. Eine persistente Quarantäne für korrupte oder unbekannte Records bleibt trotzdem ein offenes Produktions-Gate.

Die Konfliktordnung behandelt gleiche physical/logical HLC-Werte als denselben kausalen Rang, auch wenn ihre HLC-Node-IDs verschieden sind. Danach gewinnt ein Tombstone vor einem Live-Wert, anschließend entscheiden Origin-Device und HLC-Node. Sind auch diese Metadaten identisch, entscheidet `ConflictTieBreaker` Version 1 anhand explizit stabil kanonisierter Value-Bytes. Die unverhashten Bytes vermeiden eigene Kryptoprimitiven und Hashkollisionen; beim Decodieren wird ihre Bindung an den Value erneut geprüft. Damit ist die Auswahl für unterschiedliche gültige Values kommutativ und assoziativ statt von der Ankunftsreihenfolge abhängig.

Generisches `Codable` gilt ausdrücklich nicht als kanonisch und reicht für `VersionedValue` nicht mehr aus. Unterstützt werden nur Typen mit explizitem `ConflictCanonicalizable`-Opt-in: normalisierte Strings, Bool, die freigegebenen Integer-Typen, UUID, Data, die typisierten IDs, `EncryptedValue`, Optional sowie Arrays und Sets dieser Typen. Arrays erhalten ihre Reihenfolge; Sets sortieren die stabilen Elementbytes und brechen bei kanonischen Kollisionen kontrolliert ab. Weitere Sync-Modelle müssen eine versionierte, plattformunabhängige Implementierung bereitstellen oder werden bereits vom generischen Typvertrag abgewiesen. Golden-Byte-Tests und alle Einfügereihenfolgen eines Test-Sets dienen als prozessunabhängiges Determinismus-Äquivalent; ein echter Mehrprozess-/Mehrgeräte-CloudKit-Test bleibt Teil der späteren E2E-Gates.

## Companion-Command-Vertrag

Ein signierter Payload bindet `commandID`, Quellgerät, Zielgerät, mindestens 128 Bit Nonce, Ausgabezeitpunkt und den konkreten Befehl. Die Signatur wird über die kanonische, nach Schlüsseln sortierte JSON-Repräsentation geprüft. Die Gültigkeit beträgt fest 300 Sekunden; sie ist nicht vom Sender verlängerbar.

Validierungsreihenfolge:

1. Zielgerät und lokal aktiviertes Remote Control,
2. Zeitfenster und Nonce-Form,
3. erlaubte Befehlssemantik ohne URL-Userinfo,
4. explizite Gerätefreigabe,
5. Signatur,
6. atomarer Replay-Verbrauch.

Damit kann ein ungültiger oder unsignierter Befehl keine Nonce im Replay Store vergiften. `open` erlaubt ausschließlich `http` und `https`; `user`, `password` oder andere URL-Userinfo sind als Secret-Transfer verboten. `focus` und `close` akzeptieren nur das normale Profil; `close` genau einen Tab. Shell-Befehle, interne Custom Schemes und Incognito sind im Modell nicht ausführbar. Ist Remote Control lokal deaktiviert, verwirft der Validator jeden Befehl vor semantischer Ausführung, Signaturprüfung und Replay-Verbrauch.

## Lokal bauen und testen

```bash
cd spikes/cloudkit
swift test
```

Die Tests verwenden einen In-Memory-Transport sowie lokale, nicht hochgeladene `CKRecord`-Instanzen für Codec-Roundtrips. Der bedingt kompilierte Apple-Adapter wird gegen das installierte SDK gebaut, jedoch nicht ohne Provisioning gegen iCloud aufgerufen.

Verifizierter Phase-0-Lauf am 20. August 2026 auf Apple Silicon mit Apple Swift 6.3.3 und dem macOS-26.5-SDK:

```text
swift test --jobs 1 --disable-index-store
Build complete!
Executed 34 tests, with 0 failures (0 unexpected)
```

`--jobs 1` begrenzt ausschließlich die parallele Compilerlast im gemeinsam genutzten Workspace; es verändert die Tests nicht.

## Noch erforderliche Apple-Gates für einen echten Roundtrip

Ein echter Mac-iOS-Roundtrip darf erst als bestanden gelten, wenn alle folgenden externen Voraussetzungen vorliegen:

1. finale macOS- und iOS-Bundle-IDs im Apple Developer Account;
2. registrierter iCloud-Container mit konsistentem Identifier;
3. aktivierte iCloud-/CloudKit-Entitlements für beide App IDs;
4. Development- und Distribution-Provisioning-Profile für beide Targets;
5. CloudKit-Schema in der Development-Umgebung und kontrollierte Promotion nach Production;
6. echtes, angemeldetes iCloud-Testkonto auf mindestens einem Mac und einem iOS-Gerät;
7. produktiver Key-Lifecycle: Generierung, Keychain-Aufbewahrung, Gerätefreigabe, Rotation, Recovery und Widerruf;
8. persistenter Replay Store statt des Spike-In-Memory-Stores;
9. serverseitige Change Tokens, persistente Quarantäne, Zonen-Recovery, Teilfehler-/Quota-/Offline-Behandlung und bestätigungsgebundene Tombstone-Garbage-Collection;
10. Computer-Use-E2E mit sichtbarer Erstellung auf Gerät A, Synchronisation, Konflikt, Löschung und Recovery auf Gerät B.

Bis diese Gates mit Artefakten belegt sind, ist der Status `BLOCKED_ENTITLEMENT` beziehungsweise `NOT_RUN`, nicht `PASS`.
