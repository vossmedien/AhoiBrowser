# AhoiBrowser und Crest: Funktionsvergleich und Empfehlungen

Stand: 12. September 2026. Die Analyse wurde anschließend vom Nutzer zur Integration in den Master bestätigt. Seine Klarstellung gibt automatischer Archivierung und synchronisierten Split-Anordnungen ebenfalls hohe Priorität, primär zwischen Desktop-Installationen. Eine eigene Split-Ansicht auf iOS/iPadOS ist keine Pflicht. Die folgenden Empfehlungen sind entsprechend fortgeschrieben; Quellcode-/Laufzeitbefunde bleiben an den untersuchten Stand gebunden. Verbindlicher Umfang: [Master-Zielprompt][a-master].

**Meine Empfehlung: Ahois Chromium-Grundlage und beschlossenes Produktmodell beibehalten.** Crest liefert besonders gute Anregungen für den Umgang mit Links, gespeicherten Arbeitsorten und gerätegerechter Bedienung. Die größten zusätzlichen Nutzwerte sind vollständiger Desktop-Split-/Archiv-Sync, sichere automatische Archivierung, verlässliches Workspace-Routing, eine feste Ausgangsadresse für gespeicherte Tabs, Link-Peek und frei belegbare Tastenkürzel. Bestehende Ahoi-Funktionen wie Split View, Quick Window, Suche und Schlaf-Ausnahmen sollten vollständig nutzbar werden; ihr erneuter Nachbau wäre kein zusätzlicher Wert.

**Zuerst die technische Einordnung:** Crest ist ein SwiftUI-/WebKit-Browser, kein Firefox-/Gecko-Fork. Das Installieren von Firefox-Add-ons bezeichnet eine Erweiterungsquelle, nicht die Browser-Engine. Ahoi Desktop basiert auf dem vollständigen nativen Chromium-Browser; AhoiMobile verwendet SwiftUI und das systemgelieferte WebKit. Crest ist deshalb vor allem eine Verhaltensreferenz für Desktop und zusätzlich eine mögliche technische Referenz für Mobile. [Crest-Architektur][c-arch], [Ahoi-README][a-readme]

**Untersuchungsumfang und Beleggrenzen.** Geprüft wurden die öffentliche Website, das Hilfezentrum, öffentliche Produktabbildungen, Architektur-/Kompatibilitäts-/Roadmap-Dokumente und ausgewählte ausführbare Quellpfade. Crest wurde als unveränderter flacher Checkout von main auf c2bf7e328a911a9c962fd3354ad4a83ee4dc1099 gelesen. Ahoi wurde auf cf56c04568883e518cf55d70eecf775b05b6e101 mit Mastervertrag und den Checkpoints vom 8. September abgeglichen. Vorhandene unversionierte Arbeitsdateien wurden nicht verändert. Es gab keine Installation von Crest, keine ausgeführten Browser-/Gerätetests und keinen Performancevergleich. Ein Quellpfad, eine Website-Angabe und ein belegter Laufzeitpass sind verschiedene Nachweise.

Die GitHub-Release-Metadaten weisen [Crest 0.5.7 vom 27. August](https://github.com/pauljoda/Crest/releases/tag/v0.5.7) als aktuellen regulären Release aus. Daneben ist [Development 0.5.105 vom 12. September](https://github.com/pauljoda/Crest/releases/tag/development-0.5.105-2026-09-12-1087-r87.1) als Vorabversion veröffentlicht; deren Zielrevision 6f53535b weicht ebenfalls vom hier gelesenen main-Commit ab. Die Matrix beschreibt den recherchierten Website-/Quellstand. Sie behauptet keine Funktionsparität des Stable-Binaries mit main.

Die Tabellen decken die wesentlichen öffentlich beschriebenen Produktfunktionen sowie die für Ahoi relevanten technischen Unterschiede ab. Sie sind kein zeilenweises Vollaudit aller Crest-Dateien. „Code“ bedeutet, dass der entsprechende Ahoi-Pfad auffindbar und für die Bewertung geprüft ist; es bedeutet keine vollständige aktuelle Produktabnahme. „Soll“ bezeichnet unseren verbindlichen Zielumfang. „Ergänzung“ bezeichnet einen noch nicht klar zugesagten oder im geprüften Ahoi-Umfang nicht nachgewiesenen Ablauf.

**Unsere Ausgangslage ist umfangreicher als eine leere Featureliste.** Der Desktop-Checkpoint dokumentiert unter anderem einen realen Arc-Import mit Ordnern und Splits, Neustartpersistenz und einen wiederholten Import ohne Duplikate. Er lässt weitere Sidebar-/Drag-Korrekturen und die vollständige Abnahme offen. Lokale Website-Sitzungen pro Workspace sind ausdrücklich beschlossen, aber noch nicht implementiert. Mobile besitzt Entwicklungsnachweise; der aktuellere Sync-Checkpoint führt weitere Geräte-, Signierungs- und gemeinsame Sync-Nachweise als offen. Diese historischen Kandidatenbelege wurden hier gelesen, die heute installierten Anwendungen nicht neu abgenommen. [Desktop-Checkpoint][a-desktop], [Workspace-Sitzungen][a-sessions], [Mobile-Checkpoint][a-mobile], [Sync-Checkpoint][a-sync-checkpoint]

Beim grundlegenden Produktmodell ergibt sich folgender Vergleich:

| Nr. | Bereich | Crest | Ahoi: Code beziehungsweise Soll | Bewertung |
| --- | --- | --- | --- | --- |
| 1 | Desktop-Engine | System-WebKit mit eigener Browserhülle. | Vollständiges Chromium mit nativen Browserdiensten. | Chromium beibehalten; eine Engine-Umstellung würde den Kernnutzen verändern. |
| 2 | Zielgeräte | Mac, iPad, iPhone; Apple-orientiert. | v1: Apple-Silicon-Mac und iOS/iPadOS 26; Windows, Linux und Intel ausgeschlossen. | Kein heutiger Ahoi-Vorteil durch den Ordnernamen „Plattformuebergreifend“. |
| 3 | Organisation | Space ist Organisations- und umfassende Profilgrenze. | Workspace organisiert Baum, Auswahl, Darstellung und künftig lokale Website-Sitzungen. | Nutzbares Kontextmodell übernehmen, technische Grenzen bewusst unterschiedlich halten. |
| 4 | Kontotrennung | Getrennte WebKit-Datenspeicher pro Profil. | Native lokale Session-Isolation beschlossen, noch offen. | Höchste Priorität innerhalb des bestehenden Plans; verhindert Account-Verwechslungen. |
| 5 | Verlauf, Passwörter, Extensions | Je Space getrennt. | Diese Dienste und Extension-Storage bleiben global. | Ahoi-Entscheidung beibehalten. Ein Workspace ist bei uns keine Sicherheitsgrenze gegenüber globalen Erweiterungen. |
| 6 | Seitenkompatibilität | WebKit plus eigene Browserintegrationen. | Chromium übernimmt Webplattform, Permissions, Downloads, Media und DevTools. | Gute Grundlage für Entwickler-/Kundenarbeit; Überlegenheit erst an realen Fällen belegen. |
| 7 | Produktumfang | Arc-artiger Browser mit vielen Apple-Integrationen. | Arc-artiger Arbeitsbrowser mit Developer Toolkit, HTTP Auth und vollständigen Chromium-Funktionen. | Eigene Schwerpunkte klar erklären und liefern. |
| 8 | Reife | Öffentlich angeboten; Website bezeichnet Early Access und fehlendes unabhängiges Security-Audit. | Entwicklungsprodukt, keine vollständige Stable-Abnahme. | Keine Seite allein wegen Website oder vorhandener Tests als fertig bewerten. |

Die Einordnung stützt sich auf [Crests Architektur][c-arch], [Space-Modell][c-spaces], [Produktseite](https://crestbrowser.com/), [Ahois Mastervertrag][a-master] und [Workspace-Vertrag][a-sessions].

Bei Tabs, Navigation und der täglichen Bedienung sind diese Unterschiede relevant:

| Nr. | Funktion | Crest | Ahoi: Code beziehungsweise Soll | Empfehlung |
| --- | --- | --- | --- | --- |
| 9 | Vertikale Sidebar | Dauerhafte Organisation und aktuelle Tabs. | Zentraler Produktkern mit angedockter/schwebender Darstellung. | Vorhandene Darstellung und Bedienbarkeit abschließen. |
| 10 | Favoriten als Iconraster | Eigene Pinned-Sites-Ebene pro Space. | Gespeicherte Tabs plus separate native Lesezeichenoberfläche. | Optional kompakte Darstellung vorhandener Ziele; keine dritte unabhängige Sammlung. |
| 11 | Gespeicherte/temporäre Tabs | Unterschiedliche Platzierung und Schließsemantik. | Bereits zentral definiert und im Tab-/Sessionmodell vorhanden. | Begriffe und Aktionen verständlich halten; keine neue Parallelstruktur. |
| 12 | Feste Ausgangsadresse | Gespeicherter Tab hat zusätzlich zur aktuellen URL eine Home-URL. | TreeNode besitzt eine URL; echte Navigation aktualisiert sie. Kein entsprechender Home-Ablauf gefunden. | Sehr sinnvolle Ergänzung für CMS, Tickets, Dashboards und Recherche. |
| 13 | Verschachtelte Ordner | Ordner auch innerhalb aktueller Tabs; ganze Unterbäume zwischen Bereichen verschiebbar. | Tiefer persistenter Baum, DnD und Undo im Soll/Code. | Ganze Baumoperationen erhalten. Temporäre Ordner nur bei klarem Bedarf ergänzen. |
| 14 | Tab-auf-Tab-Drop | Laut Hilfe erzeugt Drop auf einen aktuellen Tab einen Ordner; Drop auf die Seite erzeugt Split. | Die Mitte einer Seitenzeile bedeutet verbindlich Split. | Crest-Gesten nicht unverändert übernehmen; Ahois gewohnte Zielsemantik schützen. |
| 15 | Split View | Zwei bis vier Karten, dokumentiertes Spaltenmodell. | Zwei bis vier echte Chromium-Seiten, einschließlich 2×2 und asymmetrischer Layouts. | Bereits geplanter Ahoi-Mehrwert; Layout-/Fokus-/Restorequalität liefern. |
| 16 | Fokus folgt Maus | Optionaler Wechsel des aktiven Split-Panes bei Hover. | Expliziter Fokus; optionale Darstellung im erweiterten Ziel. | Standardmäßig aus; nur mit eindeutiger Zielzuordnung und ohne Hover-Freigabe geschützter Aktionen. |
| 17 | Command Palette | Navigation, Tabs, Verlauf und Browserbefehle aus einem Katalog. | CommandService mit lokalen Quellen, Typen, Ranking und Suchkürzeln vorhanden. | Gemeinsamen Katalog ausbauen, keine zweite Suche. |
| 18 | Freie Tastenkürzel | Mac: Suchen, Aufnehmen, Konfliktanzeige, Zurücksetzen. | Feste Browser-/Ahoi-/Extension-Kürzel; vollständiger eigener Belegungseditor nicht gefunden. | Hoher Nutzwert für Poweruser; Menüs, Palette und Einstellungen gemeinsam speisen. |
| 19 | Zuletzt verwendeter Tab | Control-Tab springt zum zuletzt verwendeten Tab. | Tab-Cycling im Soll; eigener MRU-Ablauf nicht nachgewiesen. | Kleine nützliche Ergänzung, konfigurierbar und konfliktfrei. |
| 20 | Quick Window | Externe Links kurz prüfen und einem Space zuordnen. | Code vorhanden; normaler Profilkontext und Überführung des bestehenden Tabs. | Bestehenden Ablauf abschließen, dann Routing ergänzen. |
| 21 | Automatisches Link-Routing | Geordnete URL-Regeln vor dem externen Standardziel. | Externe Links/Quick Window im Soll; entsprechender Regel-Editor nicht gefunden. | Wichtigste neue Ergänzung nach verlässlicher Session-Isolation. |
| 22 | Gemerktes Linkziel | Gewählten Space für eine Website merken. | Kein entsprechender Ablauf nachgewiesen. | Als bewusste Option in denselben Routing-Ablauf integrieren. |
| 23 | Link-Peek | Normale Links über Ausgangsseite ansehen, schließen oder zum Tab machen. | Sichere Website-Popup-Overlays sind Soll/Code; allgemeines Link-Peek ist ein eigener zusätzlicher Einstieg. | Vorhandenen Popup-Lebenszyklus erweitern; expliziter Aufruf zuerst. |
| 24 | Automatisches Peek | Optional für Links zu anderen Websites aus gespeicherten Tabs. | Nun als konfigurierbare Erweiterung aufgenommen. | Standardmäßig aus; normale Link-/Modifier-Semantik erhalten. |

Quellen: [Tabs und gespeicherte Ziele][c-tabs], [Gesten][c-gestures], [Splits][c-splits], [Command Palette][c-command], [Shortcuts][c-shortcuts], [Link-Routing][c-routing], [Peek/Quick Window](https://crestbrowser.com/guides/peek-and-quick-window/). Die geprüften Ahoi-Einstiegspunkte stehen weiter unten im Quellcodevergleich.

Bei Aufbewahrung, Datenschutz und Erweiterungen sollten wir besonders genau unterscheiden:

| Nr. | Funktion | Crest | Ahoi: Code beziehungsweise Soll | Empfehlung |
| --- | --- | --- | --- | --- |
| 25 | Automatische Tab-Archivierung | Pro Space konfigurierbare Intervalle, wiederherstellbares Archiv. | Der frühere v1-Ausschluss ist durch die Nutzerentscheidung ersetzt. | Hohe Priorität: konfigurierte Fristen, geschützte Ausnahmen, Wiederherstellung und Desktop-Archiv-Sync. |
| 26 | History-/Archiv-Aufbewahrung | Eigene Fristen für Verlauf, Archiv und Downloadvermerke. | Native Dienste, manuelles Löschen und Restore; keine zusätzliche Archivplattform. | Zuerst klare Wiederherstellung und Löschumfänge; keine zweite Historienhaltung. |
| 27 | Keep Loaded/Unload | Live-Seite halten oder entladen, ohne das Ziel zu löschen. | NeverSleep/TabSleeping/ResourcePolicy und Kontextmenü vorhanden. | Bereits vorhanden, kein neues Feature. Sichtbarkeit und konkrete Ausnahmen abnehmen. |
| 28 | Private Browsing | Nichtpersistente Private Spaces. | Desktop echtes OffTheRecordProfile; Mobile eigener privater Modus. | Native Grenzen beibehalten. |
| 29 | Private Ansicht sperren | Optional Geräteauthentifizierung und erneute Sperre im Hintergrund. | Gleichwertiger Produktablauf nicht gefunden. | Sinnvoll vor allem für Mobile: Face ID/Touch ID und geschützter App-Umschalter. |
| 30 | Site Controls | Kontextbezogene Rechte, Reader, Blocking und Erweiterungsaktionen. | Page Info, Privacy, Cookies, Cache, Toolkit und aktive Pane-Zuordnung im Soll/Code. | Bestehende Aktionen verständlicher zusammenführen, keine neue Sicherheitsdatenbank. |
| 31 | Eingebauter Blocker | Kleine mitgelieferte WebKit-Regelliste. | Eigener Blocker/Filterengine ausdrücklich ausgeschlossen. | Nicht übernehmen. |
| 32 | uBlock Origin | Vollständiges Blocking scheitert laut Technikdokument an fehlender Request-Interception. | Gepinnter offizieller Classic-Release über eng begrenzten Ahoi-Pfad vorgesehen. | Ahois bestehende Lösung abschließen; keine pauschale MV2-Freigabe und kein „funktioniert“ ohne echten Filtertest. |
| 33 | Extension-Quellen | Chrome Web Store, Firefox Add-ons, kompatible Safari-Web-Extensions, lokale Pakete auf Mac. | Native Chromium-Extensions; kein Mehr-Engine-Extension-Host. | Keine zusätzliche Firefox-/Safari-Laufzeit entwickeln. |
| 34 | Extension-Rechte/-Storage | Pro Space und Gerät getrennt. | Global; bestimmte Darstellung/Pins workspacebezogen möglich. | Keine stillschweigende Änderung unseres Trust-Modells. |
| 35 | Kompatibilitätsanzeigen | Funktionsbegrenzungen und Laufzeitprobleme verständlich sichtbar. | Native Installations-/Berechtigungswege; genaue Ahoi-Sondergrenzen erforderlich. | Sehr gutes UX-Prinzip: Einschränkung vor Installation nennen und konkrete nächste Aktion anbieten. |
| 36 | Native Passwortmanager | Eigener Keychain-Speicher; zusätzliche Anbieter haben dokumentierte Signatur-/Entitlement-Gates. | Chromium-Passwortstore, Autofill und native Extension-Anbieter erhalten. | Vorhandene Dienste produktisieren, keine eigene Vault-/Native-Messaging-Kopie. |
| 37 | Passwort-Synchronisierung | Separater iCloud-Keychain-Weg für Crest-Passwörter. | Passwort-/Credential-Sync ausdrücklich ausgeschlossen. | Nicht übernehmen; Struktur-/Setup-Sync sauber davon trennen. |

Quellen: [Aufbewahrung][c-retention], [Private Spaces][c-private], [Site Controls][c-controls], [Extension-Matrix][c-extension-matrix], [Request-Interception-Grenze][c-interception], [1Password](https://crestbrowser.com/guides/onepassword/), [iCloud Passwords](https://crestbrowser.com/guides/icloud-passwords/), [Ahoi-Extension-Vertrag][a-extensions].

Für Entwicklung, Lesen, Medien und kleine Alltagsaktionen ergibt sich dieses Bild:

| Nr. | Funktion | Crest | Ahoi: Code beziehungsweise Soll | Empfehlung |
| --- | --- | --- | --- | --- |
| 38 | Entwicklerwerkzeuge im Kontext | Lokale Entwicklungsadressen zeigen automatisch passende Werkzeugleiste. | Developer Toolkit standardmäßig verborgen und bewusst aktivierbar. | Nach einmaliger Aktivierung kontextbezogen anbieten; keine automatische Injection oder Rechteänderung. |
| 39 | Console/Network/Inspector | Web Inspector plus eigene Zugänge. | Vollständige native Chrome DevTools. | DevTools nutzen, keinen zweiten Network-/Performance-Inspector bauen. |
| 40 | CSS/JS/Cookies/Header | Crest dokumentiert einen kleineren Entwicklerzugang. | Deutlich ausführlicherer Toolkit-Vertrag für CSS/LESS/SASS, JS, Cookies, Header und Reset. | Bestehenden Ahoi-Schwerpunkt abschließen und klar präsentieren. |
| 41 | HTTP Basic/Digest/.htaccess | WebKit-Authentifizierung und Credential-Anbindung vorhanden. | Expliziter umfangreicher Schutzbereichs-, Accountwechsel- und Credential-Vertrag. | Besonderer Nutzwert für Agentur-/Staging-Arbeit; Qualität am echten Fall beweisen. |
| 42 | Reader | Eigener Reader für geeignete Artikel. | Kein klarer eigener Ahoi-Reader-Ablauf gefunden; Chromium-Reading-Mode nicht damit gleichsetzen. | Sinnvoll ergänzen; Desktop vorhandene Upstream-Funktion zuerst prüfen, Mobile nativen Ablauf schaffen. |
| 43 | Link als Markdown | Eigener Kopierbefehl neben normalem Link. | Normales Kopieren vorhanden/gefordert; Markdown-Ablauf nicht gefunden. | Kleine Ergänzung mit hohem Alltagsnutzen für Tickets und Notizen. |
| 44 | Screenshots/Viewport | Auswahl-, Ganzseiten-/Portrait-Capture und Entwicklerzugänge. | Kein Screenshot-Editor; Responsive Mode bleibt DevTools. | Höchstens vorhandene Capture-Aktionen bequem zugänglich machen; keinen Editor nachbauen. |
| 45 | Teilen/PDF/Webarchive | Native Share-Aktionen, PDF und WebKit-Archiv. | Drucken, PDF, Dateien und Systemintegration im Soll. | Sichtbare Standardwege fertigstellen; WebKit-Archivformat nicht zum plattformübergreifenden Ahoi-Format machen. |
| 46 | Downloads | Status, Ziel, Fehler und Aufbewahrung pro Space. | Globaler nativer Downloadmanager mit Pause/Resume, Finder und Historie. | Chromium-Fähigkeiten erhalten; keine duplizierte Downloadverwaltung. |
| 47 | Media/PiP | Eigene WebKit-/Plattformanbindung, reale Geräteprüfungen teils offen dokumentiert. | Native Media Session, PiP, MiniPlayer, WebRTC und Split-Pane-Lebenszyklus zugesagt. | Zentrale Arbeitsreisen liefern; keinen Feature-Haken aus einer Produktabbildung ableiten. |
| 48 | Passkeys/OAuth/SSO/DRM | Plattform-/Signierungs-/Anbietervoraussetzungen beeinflussen Teile des Umfangs. | Umfangreicher Browserfähigkeitsvertrag, einschließlich eigener Release-Gates. | Konkrete Arbeitsseiten und Anbieter prüfen. Engine-Wahl allein ist kein Erfolgstest. |

Quellen: [Localhost-Tools][c-localhost], [Seitenaktionen][c-page-tools], [Crest-Roadmap][c-roadmap], [Crest-Architektur][c-arch], [Ahoi-Mastervertrag][a-master] und [HTTP-Auth-Vertrag][a-http].

Beim Umzug und Zusammenspiel der Geräte liegen weitere Möglichkeiten:

| Nr. | Funktion | Crest | Ahoi: Code beziehungsweise Soll | Empfehlung |
| --- | --- | --- | --- | --- |
| 49 | Arc-/Zen-Umzug | Strukturimport mit Auswahl und Zielzuordnung. | Arc-Import mit dokumentierter realer Teilabnahme; Zen nur anhand belegter Schemata. | Vorhandene sichere Importer fertigstellen. |
| 50 | Importvorschau | Vorher/Nachher pro Space, einzelne Einträge abwählen, Ziel gestalten. | Redigierte Vorschau, Snapshot, Bestätigung, atomarer Commit, Rollback und No-op. | Verständliche Zielvorschau ergänzen; technische Sicherungen erhalten. |
| 51 | Chrome/Safari/Firefox-Import | Je Quelle unterschiedliche Datenkategorien. | Native vorhandene Importer verwenden; tatsächliche Capabilities anzeigen. | Verfügbarkeit ehrlich pro Quelle zeigen, nicht pauschal „alles importieren“. |
| 52 | Portabler Export | Browserstruktur als validiertes Archiv; sensible Datengruppen ausgeschlossen. | Import-Backups/Recovery vorhanden; einfacher nutzerseitiger Workspace-Export nicht nachgewiesen. | Sinnvolle Ergänzung für Umzug, Datensouveränität und Wiederherstellung. |
| 53 | CloudKit-Struktursync | Spaces, Tabs, Ordner, Verlauf, Archiv und ausgewählte Preferences. | Gemeinsame normale Tabs, separate Lesezeichen, Verlauf und definierte Datenklassen in Format 3. | Bestehenden gemeinsamen Sync abschließen; keine zweite Engine integrieren. |
| 54 | Sync-Default | In geprüftem Quellstand wird eine fehlende globale Einstellung als aktiviert interpretiert. | Sync soll bewusst aktiviert werden; Mobile startet lokal. | Crest-Default nicht übernehmen. „Optional“ ist keine Aussage über den tatsächlichen Code-Default. |
| 55 | Browser-Setup-Sync | Erweiterungen bleiben laut Dokumentation pro Space/Gerät; keine vollständige Setup-Wiederherstellung belegt. | ADR 0010 verlangt native Einstellungen, vertrauenswürdige Extension-Wiederherstellung und positiv geprüfte Werte. | Bereits geplanter, bedeutender Ahoi-Mehrwert; derzeit kein abgeschlossener Produktvorteil. |
| 56 | Split-Gruppen auf anderen Geräten | Gruppe/Reihenfolge portabel; iPhone zeigt Karten nacheinander. | Nun verbindlich: vollständige normale Split-Anordnung zwischen Desktops; Mobile erhält Metadaten. | Hohe Priorität, einschließlich Reihenfolge/Layout/Ratios. Keine mobile Split-UI-Pflicht. |
| 57 | iPhone-Bedienung | Eigene Sidebar-Ansicht, kompakte Controls und Touch-Gesten. | Harbor Deck, mobile Sidebar und eigener Browser vorhanden. | Interaktionsprinzipien nutzen; heutige Mobile-Flows verbessern statt neu beginnen. |
| 58 | iPad | Sidebar, resizable Split-Spalten und Hardwaretastatur. | Native adaptive iPad-Oberfläche; gemeinsame Desktop-Metadaten bleiben erhalten. | Mehrseitenansicht nach praktischer UX-Bewertung; keine Voraussetzung für Desktop-Sync. |
| 59 | Geräteaktionen | Struktursync; kein vergleichbarer gezielter Remote-Control-Vertrag im geprüften Umfang. | Link an konkreten Mac/Workspace und begrenzte signierte Remote-Aktionen im Soll. | Bestehender Ahoi-Schwerpunkt; bewusst auslösen, keine ungefragte Navigation am anderen Gerät. |

Quellen: [Crest-Umzug/Export][c-import], [Crest-Sync][c-sync], [Crest-Split-Vertrag][c-splits], [Sync-Controller][c-sync-controller], [Extension-Vertrag][c-extensions], [Ahoi-Setup-Sync][a-setup], [Ahoi-Mobile-Ziel][a-mobile-goal].

Darstellung und Betrieb gehören ebenfalls zum Vergleich:

| Nr. | Bereich | Crest | Ahoi: Code beziehungsweise Soll | Empfehlung |
| --- | --- | --- | --- | --- |
| 60 | Workspace-Identität | Wappen, Farbfelder, Verläufe und umfangreiche „Space Forge“. | Icon, Workspace-Akzent, dezente Seitenfarbe und Accessibility-Vorgaben. | Klare Identität übernehmen; keinen Wappeneditor als v1-Priorität. |
| 61 | Barrierefreiheit | Native Oberflächen; Roadmap nennt noch physische Prüfungen. | VoiceOver, Kontrast, reduzierte Bewegung/Transparenz verbindlich. | Tatsächliche Lesbarkeit und Bedienung beweisen; native Technik allein reicht nicht. |
| 62 | Hilfe/Entdeckbarkeit | Aufgabenorientiertes Hilfezentrum mit kurzen konkreten Abläufen. | Umfassende interne Spezifikation; Produkt-Hilfe kein entsprechender Schwerpunkt. | Kurze Hilfe direkt an unbekannten Funktionen: gespeicherter Tab, Peek, Routing, Wiederherstellung. |
| 63 | Telemetrie/Datenschutz | Keine eigene Tracking-/Browserdatenplattform laut Datenschutzseite. | Keine Produkttelemetrie, kein Kontozwang, dokumentierte nötige Endpunkte. | Bereits gemeinsames Prinzip; kein Alleinstellungsmerkmal behaupten. |
| 64 | Updates/Distribution | Mac-Direktdistribution/Sparkle und mobile App-Store-Verlinkung. | Signierung, Notarisierung, Updater, Rückfallpfad und Security-Rolls im Soll. | Vertrauenswürdige Versorgung priorisieren, nicht nur Funktionszahl. |
| 65 | Geschwindigkeit/Ressourcen | Lifecycle- und Performancearbeit im Repository. | Messbare Lean-/Memory-/Start-/Workspace-Budgets im Ziel. | Keine schnellere/leichtere Engine behaupten; vergleichbare Nutzerlast und Hardware messen. |

Quellen: [Space-Anpassung][c-customize], [Hilfezentrum](https://crestbrowser.com/guides/), [Datenschutz](https://crestbrowser.com/privacy/), [Roadmap][c-roadmap], [Ahoi-Mastervertrag][a-master]. Die angesehenen Mac-/iPhone-/Import-Abbildungen zeigen Crests Gestaltung, belegen aber weder Animation, Touchqualität noch tatsächliche Laufzeitgeschwindigkeit.

**Der Quellcode liefert konkrete Erkenntnisse für eine Übernahme.** Positiv ist die Trennung zwischen gemeinsamen Modellen/Policies, Plattformadaptern und Darstellung. Viele Regeln sind eigenständige, lesbare Funktionen. Das macht Crest zu einer brauchbaren Referenz. Wir sollten daraus kleine klar definierte Verhaltensverträge ableiten; ein zweiter BrowserStore oder eine zweite Sync-/Extension-Architektur würde Ahoi mehr Verantwortung aufladen, als er Nutzen bringt.

| Geprüfte Stelle | Konkreter Befund | Übertragbarkeit auf Ahoi |
| --- | --- | --- |
| [BrowserTab][c-tab-model] und [SavedLocationRestorePolicy][c-home-policy] | Aktuelle URL und gespeicherte Ausgangsadresse sind getrennt; Rückkehr ist eine eigene Aktion. | Sehr gutes Modellkonzept. Bei Ahoi muss es in TreeNode, Sessionbeobachtung und den abgestimmten Sync-Vertrag passen. |
| [BrowserLinkRoutingPolicy][c-routing-code] | Erste aktivierte passende Regel gewinnt; sonst Standardziel beziehungsweise gemerkter Space. „Contains“ prüft die gesamte URL-Zeichenkette. | Reihenfolge und Fallback übernehmen; Domain-/Pfad-Matching besser spezifizieren. Kein Code-Copy nötig. |
| [ShortcutSettingsModel][c-shortcuts-code] und [ConflictPolicy][c-shortcut-conflicts] | Suchen, Aufnahme, Konfliktzustand, Reset sowie Extension-Kommandos sind greifbare Codepfade. | Katalog-/Konfliktprinzip übernehmen. Desktop nutzt Chromium-Kommandos und native Accelerators, nicht die SwiftUI-Tastaturschicht. |
| [WebsiteDataStore][c-website-store] | Persistenter WKWebsiteDataStore wird an eine Profil-ID gebunden. | Relevant für Mobile; Ahoi nutzt dort WebView/WebPage, daher Adapter prüfen. Desktop bleibt an Chromium gebunden. |
| [SpaceAccessController][c-access] | Geräteauthentifizierung kontrolliert Zugriff auf geschützte Inhalte. | Für Mobile als Authentifizierungs-/Sperrablauf nützlich. Keine zusätzliche Verschlüsselungs- oder Tresorgarantie daraus ableiten. |
| [ImportReviewPlan][c-import-plan] und [PortableArchive][c-export-code] | Importauswahl/Zielzuordnung als explizites Modell; Archivversion und Mengen-/Größengrenzen. | Review-Modell und Validierungsprinzip nutzen; vorhandene Ahoi-Importer und Transaktionen erhalten. |
| [CRX3Verifier][c-crx] | Eigene Developer-/Publisher-/ID-/Paketprüfung für den WebKit-Extension-Host. | Desktop nicht übernehmen: entsprechende Verantwortung liegt schon bei Chromium. |
| [XPIVerifier][c-xpi] | Abgleich mit AMO-Größe/SHA-256 und Prüfung vorhandener Signaturdateien; ausdrücklich keine vollständige Mozilla-JAR/PKCS#7-Signaturprüfung. | Belegt einen anderen Vertrauenspfad, keine Firefox-native Validierungsparität. Kein zusätzlicher Ahoi-Extension-Pfad. |
| [CloudSyncController][c-sync-controller] und [CredentialPreferences][c-credential-default] | Fehlende Sync-Einstellung fällt auf true; auch der Passwort-Keychain-Sync-Default ist true. | Für Ahois Opt-in-/Credential-Grenzen ungeeignet. Nicht als unveränderte Mobile-Sync-Bibliothek übernehmen. |
| [CI-Konfiguration][c-ci] | Öffentlicher App-Job führt Builds für macOS/iOS aus; kein XCTest-Aufruf in diesem Job. | Ein grüner Workflow ist kein Beleg der kompletten Browserfunktion oder ausgeführter Tests. Lokale Testskripte existieren separat. |

Der Routing-Unterschied hat einen praktischen Grund: Eine Regel für „kunde.example“ als beliebigen URL-Teil könnte auch auf „https://andere.example/?next=kunde.example“ passen. Für Ahoi empfehle ich getrennte Optionen für exakten Host, Host samt Subdomains und optionalen Pfad. Ein sichtbarer Test der eingegebenen Regel sollte vor dem Speichern zeigen, welcher Workspace gewählt wird. Das ist eine aus dem Matcher abgeleitete Produktverbesserung, kein hier nachgewiesener Angriff. [Matcher][c-routing-code]

Bei Extensions ist die größte technische Einschränkung ausdrücklich dokumentiert: Einige Browser-APIs fehlen, Downloads sind nur teilweise abgebildet, declarativeNetRequest ist verlustbehaftet und vollständiges uBlock-Blocking benötigt eine derzeit fehlende Interception-Grenze. „Im Store verfügbar“, „Paket installiert“ und „volle Funktion“ dürfen deshalb nicht gleichgesetzt werden. [API-Matrix][c-extension-matrix], [Interception][c-interception]

Bei CloudKit ist ebenso eine genaue Formulierung nötig: Die Hilfe beschreibt ein einschaltbares Feature, der untersuchte Controller initialisiert ohne gespeicherten Wert jedoch mit true. Das ist ein überprüfter Quellcode-Default, kein hier beobachteter Netzwerktransfer einer frischen Releaseinstallation. Für Ahoi bleibt ausdrückliches Einschalten maßgeblich. Passwortwerte in iCloud Keychain sind außerdem ein anderer Transport als CloudKit; für unseren ausgeschlossenen Passwort-Sync ändert dieser Unterschied nichts. [Controller][c-sync-controller], [Credential-Default][c-credential-default], [Sync-Hilfe][c-sync]

**Die passenden Ahoi-Einstiegspunkte existieren zu einem großen Teil bereits.** Diese Zuordnung verhindert, dass aus einer Wettbewerbsanalyse unnötige Neuentwicklung entsteht:

| Thema | Geprüfter Ahoi-Einstiegspunkt | Folgerung |
| --- | --- | --- |
| Lokale Suche und Befehle | [command_service.h](../../overlay/chromium/src/ahoi/browser/navigation/command_service.h) | Vorhandene Quellen und Identitäten weiterverwenden. |
| Quick Window | [quick_window.h](../../overlay/chromium/src/ahoi/browser/command_bar/quick_window.h) | Bestehendes WebContents verschieben; Routing muss den richtigen Sitzungskontext bereits vor Navigation wählen. |
| Popup/Peek | [popup_overlay_service.h](../../overlay/chromium/src/ahoi/browser/popup/popup_overlay_service.h) und [Controller](../../overlay/chromium/src/ahoi/browser/ui/popup/popup_overlay_controller.cc) | Normalen Link-Einstieg ergänzen, bestehenden Lebenszyklus erweitern. |
| Gespeicherte Ausgangsadresse | [tab_tree_model.h](../../overlay/chromium/src/ahoi/browser/tab_tree/tab_tree_model.h) und [Sessionbeobachtung](../../overlay/chromium/src/ahoi/browser/session/session_bridge_observers.cc) | Derzeit ein URL-Feld; Navigation aktualisiert Metadaten. Home-URL benötigt eine bewusste Modellerweiterung. |
| Keep Loaded | [resource_policy_service.cc](../../overlay/chromium/src/ahoi/browser/resource_policy/resource_policy_service.cc), [tab_sleeping.cc](../../overlay/chromium/src/ahoi/browser/memory/tab_sleeping.cc), [Kontextmenü](../../overlay/chromium/src/ahoi/browser/ui/sidebar/browser_sidebar_host_context_menu.cc) | Bereits implementierter Weg einschließlich native Discard-Regeln und NeverSleep. |
| Mobile-Bedienung | [MobileBrowserSidebar.swift](../../apps/AhoiMobile/Sources/AhoiMobileCore/MobileBrowserSidebar.swift), [ActionsSheet](../../apps/AhoiMobile/Sources/AhoiMobileCore/MobileBrowserActionsSheet.swift) | Vorhandene Browseroberfläche verbessern, kein zweites Mobile-Produkt. |
| Workspace-/Setup-Sync | [Workspace-Vertrag][a-sessions] und [ADR 0010][a-setup] | Native Kontextgrenzen und abgestimmtes gemeinsames Format erhalten. |

**Was wir übernehmen sollten, in sinnvoller Reihenfolge.** Die Aufwandseinstufung ist relativ, keine Stunden- oder Lieferzusage: klein bedeutet einen begrenzten bestehenden UI-/Befehlspfad; mittel betrifft mehrere Oberflächen oder Persistenz; groß betrifft zusätzlich Session-, Sync- oder sicherheitsrelevante Lebenszyklen. Der tatsächliche Aufwand hängt von der Abnahme der vorhandenen Grundlagen ab.

| Priorität | Vorschlag | Konkreter Mehrwert | Aufwand/Abhängigkeit | Woran der Nutzen sichtbar wird |
| --- | --- | --- | --- | --- |
| Bestehendes Ziel zuerst | Sidebar/Import/Extensions, dann Workspace-Sitzungen und Daily Driver abschließen. | Verlässlicher Arbeitsbrowser statt weiterer unfertiger Flächen. | Bestehendes großes Paket, kein neuer Scope. | Zwei Accounts derselben Site bleiben über Workspacewechsel, Popup und Neustart getrennt; Import und Wiederherstellung sind verlässlich. |
| Hoch | Automatische Archivierung und Desktop-Archiv-Sync. | Weniger offene Altlasten bei vollständiger Wiederherstellbarkeit. | Mittel bis groß; bestehender Lifecycle-/Sync-Vertrag. | Geeignete Tabs/Gruppen archivieren und auf anderem Desktop korrekt wiederherstellen, ohne aktive Arbeit zu beenden. |
| Hoch | Vollständige normale Split-Anordnungen zwischen Desktops synchronisieren. | Arbeitsorganisation am zweiten Mac fortsetzen. | Groß; logische Gruppe und native Projektion koordinieren. | Zwei-/Drei-/Vier-Pane-Gruppe mit Reihenfolge/Layout/Ratios erhalten; Offline-Konflikte konvergieren. Mobile bewahrt Metadaten ohne Split-UI. |
| Hoch | Workspace-Routing für externe Links, inklusive bewusst gemerkter Website-Ziele. | Mail-/Messenger-/Ticket-Links öffnen im passenden Arbeitskontext. | Mittel bis groß; Session-Isolation und URL-Handler zuerst. | Link zu Kundenportal → richtiger Workspace; falsche/unbekannte Regel erklärt oder nutzt klares Standardziel. |
| Hoch | Feste Ausgangsadresse gespeicherter Tabs. | Ein CMS-/Dashboard-Tab bleibt ein definierter Arbeitsort, auch nach tiefer Navigation. | Mittel; Tree/Session und möglicher Sync-Feldvertrag. | Von Unterseite mit einer Aktion zum gespeicherten Einstieg; neues Ziel nur explizit setzen. |
| Hoch | Explizites Link-Peek mit Übernahme als Tab oder Split. | Recherche ohne ständig neue dauerhafte Tabs. | Mittel bis groß; Popup-, Fokus- und Before-Unload-Lebenszyklus. | Link ansehen → schließen ohne Ausgangszustand zu verlieren; übernehmen ohne unnötigen Reload im gleichen Kontext. |
| Hoch | Frei belegbare Browser-/Workspace-/Split-Befehle. | Schnellere tägliche Arbeit und zugängliche Bedienung. | Mittel; Command-Katalog, Menü und Extensions gemeinsam betrachten. | Neuer Shortcut funktioniert in richtigem Kontext, Konflikt wird vor Übernahme sichtbar, Reset funktioniert. |
| Gut früh kombinierbar | „Link als Markdown kopieren“. | Weniger Handarbeit in Tickets, Dokumentation und Notizen. | Klein; vorhandene Copy-Aktion. | Korrekt maskierter Titel/Link, immer das aktive Split-Pane, keine URL-Credentials im Clipboard. |
| Danach | Kontextabhängiger Entwicklerzugang nach bewusstem Einschalten. | Lokale-/Staging-Arbeit benötigt weniger Klicks, normale Seiten bleiben ruhig. | Klein bis mittel; bestehendes Toolkit verwenden. | Localhost/konfiguriertes Projekt zeigt passende Aktionen; keinerlei implizite Rechte-/Zertifikatsausnahme. |
| Danach | Visuelle Import-Zielvorschau und Einzelwahl verbessern. | Nutzer versteht vor dem Umstieg, wo seine Struktur landet. | Mittel; vorhandener Import-Hub. | Quelle/Ziel nachvollziehbar; Abbruch verändert nichts; Wiederholung bleibt ohne Duplikate. |
| Danach | Portabler Workspace-Export/-Import. | Selbstbestimmter Umzug und Recovery ohne Cloudpflicht. | Mittel; Versionierung, Beziehungen und vorhandene Transaktionen. | Export in frischen lokalen Zustand wieder einlesen; Hierarchie/Reihenfolge stimmen, keine Zugangsdaten enthalten. |
| Danach, Mobile besonders | Privaten Bereich beim Hintergrundwechsel sperren. | Schutz vor beiläufigem Einsehen am entsperrten Gerät. | Mittel; Authentifizierung, Lifecycle, Snapshot-/Accessibility-Abschirmung. | App-Umschalter zeigt keine privaten Inhalte; Rückkehr verlangt Systemauthentifizierung. |
| Danach | Reader gut erreichbar integrieren. | Angenehmeres Lesen, besonders auf dem iPhone. | Mittel; vorhandene Plattformfunktion zuerst prüfen. | Geeigneten Artikel lesen und verlustfrei zur Originalseite zurückkehren. |
| Kleine Ergänzung | MRU-Tabwechsel und kurze Kontext-Hilfe. | Häufige Wechsel und neue Bedienkonzepte werden schneller verständlich. | Klein bis mittel; bestehende Kommandos. | Rücksprung zwischen tatsächlich zuletzt genutzten Tabs, ohne ungewollten Workspacewechsel. |

Seit der bestätigten Integration sind auch die mit „Danach“ eingeordneten Empfehlungen verbindlicher Zielumfang. Das Wort beschreibt ihre Integrationsfolge nach den benötigten Grundlagen; Archivierung und Desktop-Split-Sync sind ausdrücklich hoch priorisiert. Keiner dieser Vorschläge verlangt einen neuen Cloud-Dienst, ein Abonnement oder eine bezahlte API. Die wesentlichen Kosten entstehen durch Entwicklung, sichere Integration und laufende Wartung; insbesondere Routing/Peek sind trotz kleiner Oberfläche keine bloßen CSS-Änderungen.

**Was wir bewusst nicht übernehmen sollten.** Diese Empfehlungen erhalten entweder eine ausdrückliche Ahoi-Entscheidung oder verhindern unverhältnismäßige Wartung:

| Crest-Konzept | Entscheidung für Ahoi | Begründung |
| --- | --- | --- |
| WebKit statt Chromium auf dem Mac | Nicht übernehmen. | Würde native Extension-/DevTools-/Browserfähigkeiten durch eigene Integrationsarbeit ersetzen. |
| Eigenes System für Chrome-, Firefox- und Safari-Erweiterungen | Nicht übernehmen. | Große laufende Kompatibilitäts- und Sicherheitsverantwortung; für Chromium-Desktop kaum entsprechender Nutzen. |
| Vollständige Space-Silos für Verlauf/Passwörter/Extensions | Nicht übernehmen. | Widerspricht dem beschlossenen globalen Dienstemodell. |
| Ungefragt aktivierte Archivierung | Nicht als stiller Default übernehmen. | Die Funktion ist hoch priorisierter v1-Umfang; Fristen und Schutz-/Wiederherstellungsregeln bleiben bewusst konfigurierbar. |
| Mitgelieferte Blocking-Engine | Nicht übernehmen. | Bestehender uBO-/Privacy-Vertrag trennt diese Verantwortung bewusst. |
| Passwort-Sync beziehungsweise aktivierter Sync-Fallback | Nicht übernehmen. | Widerspricht unseren Daten- und Einwilligungsgrenzen. |
| Space Forge/Wappenbaukasten | Keine v1-Priorität. | Name, Icon und lesbarer Akzent lösen den wichtigsten Orientierungsbedarf günstiger. |
| Zweiter Inspector/Responsive-Editor/Screenshot-Editor | Nicht übernehmen. | Chrome DevTools beziehungsweise vorhandene Aktionen erfüllen die Aufgabe. |
| Maus-Hover entscheidet standardmäßig das aktive Split-Pane | Nicht als Default. | Die Zuordnung von Adresse, Befehlen und Berechtigungen muss absichtlich und eindeutig bleiben. |
| Gleiche Desktopaufteilung auf das iPhone pressen | Nicht übernehmen. | Das mobile Interaktionsziel ist wichtiger als Pixelparität. |

**Bestätigte Entscheidung: vollständige Split-Organisation zwischen Desktops.** Die Nutzerklarstellung ersetzt den früheren Sync-Ausschluss. Logische Gruppe, geordnete Mitglieder, Layout und normalisierte Teilungsverhältnisse werden synchronisiert; native Handles, konkrete Fenstergeometrie, aktiver Fokus und Website-Sitzungen bleiben lokal. iOS/iPadOS bewahrt diese Metadaten verlustfrei, ohne eine eigene Split-Oberfläche anbieten zu müssen. Deren praktischer Nutzen bleibt eine getrennte UX-Entscheidung und blockiert den Desktop-Sync nicht. [Crest-Splits][c-splits], [Ahoi-Split-Vertrag][a-split]

**Unser wirklicher Mehrwert sollte in drei vollständigen Arbeitsabläufen liegen.** Erstens Kundenarbeit: externer Link landet beim richtigen Workspace und Account, CMS/Home-Ziel bleibt auffindbar, Staging-Authentifizierung funktioniert. Zweitens Recherche und Umsetzung: Quelle kurz in Peek öffnen, bewusst neben die Arbeitsseite legen, DevTools am richtigen Pane, Ergebnis als Markdown-Link übernehmen. Drittens Gerätewechsel und Recovery: normale Tabs und Organisation erscheinen verständlich auf Mac und Mobile, ein neuer Mac stellt zulässige Einstellungen/Extensions wieder her, Zugangsdaten bleiben lokal und ein Export ermöglicht den Ausstieg. Crest bestätigt die Nachfrage nach solchen Abläufen; Ahoi kann sie mit seiner nativen Chromium-Integration und den schon beschlossenen Zusatzfunktionen differenzieren.

Das sind zunächst begründete Produkthypothesen. Ein begrenzter Nutzertest sollte genau diese zusammenhängenden Aufgaben vergleichen: Zahl unnötiger Schritte, falsche Account-/Pane-Ziele, verlorener Kontext und Wiederherstellbarkeit. Aussagen über „schneller“, „stabiler“ oder „leichter“ brauchen anschließend Messungen an vergleichbaren Kandidaten. Ein weiterer umfangreicher Featurekatalog ersetzt diese Prüfung nicht.

**Quellübernahme und Wartung.** Crest deklariert MPL-2.0; Ahoi-eigener Code GPL-3.0-or-later. Ideen und Abläufe lassen sich unabhängig implementieren. Vor einer tatsächlichen Übernahme konkreter Dateien sind Lizenzhinweise, Drittbestandteile und die passende Distributionsbehandlung dateibezogen zu prüfen; aus diesem Bericht folgt keine pauschale Lizenzfreigabe. Name, Logos, Wappen und Markenassets sollten nicht übernommen werden. Für unsere Fälle ist eine kleine eigene Anpassung bestehender Ahoi-Dienste meist überschaubarer als die direkte Einbindung von Crest-Unterbäumen. [Crest-Lizenz][c-license], [Crest-Markenregeln][c-trademarks], [Ahoi-README][a-readme]

**Prüfabschluss.** Die Quellcodeanalyse selbst änderte keine Produktdateien und startete keine Builds. In der nachfolgenden autorisierten Integration wurden Ziel-/Sync-Dokumente und der Anforderungskatalog aktualisiert; dadurch ist keine Funktion bereits implementiert oder abgenommen. Offene Grenzen sind ausdrücklich eine native Crest-Laufzeitprüfung, ein fairer Performancevergleich sowie die vollständige aktuelle Ahoi-Abnahme. Sie begrenzen Produktqualitätsaussagen, verhindern aber nicht die oben begründete Entscheidung über sinnvolle Funktionen.

[a-readme]: ../../README.md
[a-master]: ../../outputs/AhoiBrowser-Master-Zielprompt.md
[a-desktop]: ../ACTIVE_DESKTOP_CHECKPOINT.md
[a-mobile]: ../ACTIVE_MOBILE_CHECKPOINT.md
[a-sync-checkpoint]: ../UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md
[a-sessions]: ../WORKSPACE_SESSIONS.md
[a-extensions]: ../EXTENSION_COMPATIBILITY.md
[a-http]: ../HTTP_AUTH.md
[a-setup]: ../decisions/0010-full-browser-setup-sync.md
[a-mobile-goal]: ../../outputs/AhoiBrowser-Mobile-Zielprompt.md
[a-split]: ../SPLIT_VIEW.md
[c-arch]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/Documentation/ARCHITECTURE.md
[c-spaces]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/spaces/spaces-and-isolation.md
[c-tabs]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/tabs/organize-tabs-and-folders.md
[c-gestures]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/gestures-and-pointer.md
[c-splits]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/tabs/split-view.md
[c-command]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/command-palette.md
[c-shortcuts]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/keyboard-shortcuts.md
[c-routing]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/link-routing.md
[c-retention]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/privacy/history-archive-and-downloads.md
[c-private]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/spaces/private-spaces.md
[c-controls]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/privacy/content-blocking-and-site-permissions.md
[c-extension-matrix]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/Documentation/ExtensionAPICompatibilityMatrix.md#boundary-notes
[c-interception]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/Documentation/ExtensionEmulationServices.md#request-interception-broker--required-not-implemented
[c-localhost]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/localhost-developer-tools.md
[c-page-tools]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/browsing/page-tools.md
[c-roadmap]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/Documentation/ROADMAP.md
[c-import]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/getting-started/move-to-crest.md
[c-sync]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/spaces/sync-across-devices.md
[c-extensions]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/Documentation/ExtensionCompatibility.md
[c-customize]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/HelpCenter/docs/spaces/customize-spaces.md
[c-tab-model]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Domain/BrowserTab/BrowserTab.swift#L3-L46
[c-home-policy]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Domain/BrowserSession/Tabs/BrowserSavedLocationRestorePolicy.swift#L3-L10
[c-routing-code]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Domain/BrowserTransientBrowsing/ExternalLinks/BrowserLinkRoutingPolicy.swift#L15-L185
[c-shortcuts-code]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Application/BrowserShortcuts/Settings/BrowserShortcutSettingsModel.swift#L112-L207
[c-shortcut-conflicts]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Domain/BrowserShortcuts/Policies/BrowserShortcutConflictPolicy.swift
[c-website-store]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Infrastructure/WebsiteData/WebsiteDataStore.swift#L5-L22
[c-access]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Application/BrowserSpaceAccess/BrowserSpaceAccessController.swift#L24-L91
[c-import-plan]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Features/DataPortability/Models/BrowserImportReviewPlan.swift#L18-L103
[c-export-code]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Features/DataPortability/Models/BrowserPortableArchive.swift#L3-L42
[c-crx]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Infrastructure/Extensions/Sources/ChromeWebStore/CRX3Verifier.swift#L33-L141
[c-xpi]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Infrastructure/Extensions/Sources/MozillaAddons/XPIVerifier.swift#L4-L69
[c-sync-controller]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Application/BrowserCloudSync/BrowserCloudSyncController.swift#L47-L65
[c-credential-default]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/CrestShared/Domain/BrowserSpace/Credentials/BrowserCredentialPreferences.swift#L18-L22
[c-ci]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/.github/workflows/ci.yml#L31-L65
[c-license]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/LICENSE
[c-trademarks]: https://github.com/pauljoda/Crest/blob/c2bf7e328a911a9c962fd3354ad4a83ee4dc1099/TRADEMARKS.md
