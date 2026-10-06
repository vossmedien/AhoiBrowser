# Crest-Abgleich 6. Oktober 2026: Commits und Community-Feedback

Quellen:
- Crest `main` von `3b0a9159` bis `4e59eafd`: 10 Commits, Releases 0.7.3–0.7.11, Review anhand von Commit-Texten und Stat.
- Fünf Reddit-Threads aus r/CrestBrowser, gelesen über die öffentlichen RSS-Feeds. Die HTML-Seite und die JSON-API sperrt Reddit für automatisierte Abrufe.
  - 1wljavg: kleine UI-Hinweise
  - 1wjpfca: Few Suggestions
  - 1winzkd: Tab-Switcher
  - 1wg3t13: 0.6 Customization Update
  - 1wf8a1l: Sub-Spaces

Das Vorgängerdokument ist `2026-09-29-crest-adoption.md`.

## Crest-Commits seit 3b0a9159

Keiner der 10 Commits ändert den Chromium-Kern. Die meisten betreffen Crests SwiftUI-Shell, den C#-Kern oder die Website.

| Commit | Thema | Ahoi | Empfehlung |
| --- | --- | --- | --- |
| 606000ce | FedCM-Login-Button stürzt ohne Host-Fenster ab | Ahoi nutzt Stock-`BrowserView`, vermutlich nicht betroffen | „Mit Google fortfahren“ einmal am installierten Build prüfen |
| f311feec | Favicons auf Retina unscharf (1x-PNG) | Sidebar nutzt `FaviconService`/`ImageSkia`, Auflösung nicht verfolgt | Sidebar und Pins bei 2x prüfen, nur bei Unschärfe fixen |
| 1222cfab | Tab-Gruppen aus Extensions als Sidebar-Ordner; `chrome.identity`-Fix | Gruppen-Adapter fehlt (`docs/CHROMIUM_REUSE_AND_MOBILE_SYNC.md`); Identity-Fehler nur durch Crests Domain-Substitution | Zuerst prüfen, ob Extension-Gruppen in Ahoi sichtbar werden |
| 2d3e8adc | Zen-Import mit Pins in Split Views | Ahoi importiert aus Zen bewusst nur History, Bookmarks und Formulare | Für einen späteren Zen-Session-Import vormerken |
| 8df7c967, 86f5d23c, 4e59eafd, Website-Commits | CRX-Limit beim eigenen Download, SwiftUI-Verlaufsmenü, Toolbar-Hosts, Marketing | betreffen nur Crests eigene Implementierung | verwerfen |

## Community-Wünsche und Crest-Funktionen gegen Ahoi

„Stock“ heißt: Chromium bringt die Funktion mit und Ahoi lässt sie aktiv. Der genaue sichtbare Zustand im Ahoi-Build ist noch nicht in jeder Zeile geprüft.

| Funktion (Quelle) | Ahoi-Stand | Nutzen | Aufwand | Empfehlung |
| --- | --- | --- | --- | --- |
| Kurze Bestätigungs-Toasts, z. B. „Link im Hintergrund geöffnet“ oder „Link kopiert“ (1wljavg) | fehlt; Ahoi hat kein Toast-System | hoch, weil Ahois Chrome minimal ist und viele Aktionen still bleiben | M | **übernehmen**: ein zentraler, kurz eingeblendeter Hinweis für Hintergrund-Tab, Kopieren, Speichern, Archivieren und Workspace-Verschieben |
| Hinweis „Esc beendet Vollbild“, Pointer-Lock-Hinweis, Link-Ziel beim Hover (1wljavg) | stock (Chromium-Bubbles) | — | — | per Journey sicherstellen, dass die schwebende Navigation sie nicht verdeckt |
| Visueller Tab-Switcher im Arc-Stil (Ctrl+T) mit Bereichen und W zum Schließen (1winzkd) | teilweise: MRU mit ⌃⌥⇥, Command Bar sucht Tabs, ⌘-Scroll | hoch für Arc-Umsteiger | M–L | **übernehmen**: Overlay mit gespeicherten, temporären und Split-Tabs, Tastatur-Navigation und W/⌘W zum Schließen |
| Mehrfachauswahl von Tabs/Ordnern (⌘/⇧-Klick) und Verschieben in einem Schritt (0.6) | fehlt; die Sidebar-Auswahl ist einzeln | hoch beim Aufräumen | M | **übernehmen**: ⌘/⇧-Klick in der Sidebar, Sammelaktionen Verschieben, Archivieren, In Split, Schließen |
| Ordner-Hover zeigt den Inhalt und öffnet einen Tab per Klick (1wg3t13) | teilweise: Ordner-Hover zeigt zuletzt benutzte Links (`browser_sidebar_host_recent_links.cc`) | mittel | S | prüfen, ob alle Einträge statt nur der letzten gezeigt werden sollen |
| Ordner schließt sich wieder, wenn ein Tab außerhalb aktiv wird (1wg3t13) | unklar | mittel | S | beobachten, Verhalten am Build prüfen |
| Angehefteten Tab per Doppelklick oder Favicon-Klick zur gespeicherten Adresse zurücksetzen (1wjpfca, 0.6) | vorhanden: Pin-Home und Hover (Patch 0034) | — | — | Favicon-Klick als zusätzlichen Auslöser erwägen |
| Adressleiste und Navigation dauerhaft oben statt nur per Hover (1wjpfca, 1wg3t13) | teilweise: Pin-Button hält die Navigation offen; neu ⌥⌘T (351f78b0) | hoch | S | ⌥⌘T abnehmen, danach Einstellung „Navigation immer zeigen“ sichtbar machen |
| Sub-Spaces: mehrere Sidebars teilen Logins und Erweiterungen (1wf8a1l) | vorhanden: geteilte Workspaces teilen ein Profil; getrennte Stufen per ADR 0011 | — | — | keine Arbeit; im Workspace-Dialog klarer erklären, dass geteilte Workspaces Logins teilen |
| Erweiterungs-Seitenpanel pro Space (0.6) | Chromium-Seitenpanel, Ahoi-Verhalten nicht geprüft (`app/startup_policy.cc` blendet einige Panels aus) | mittel (Claude/ChatGPT-Extensions) | M | prüfen, ob Extension-Panels in Ahoi öffnen; danach entscheiden |
| Übersetzung (0.6, auf dem Gerät) | Chromium-Translate, aus Datenschutzgründen aus (`privacy_defaults.cc`) | mittel | — | beibehalten: in den Einstellungen aktivierbar; On-Device ist auf dem Mac nicht ohne Google möglich |
| Automatisches Bild-in-Bild beim Tabwechsel (0.6) | PiP vorhanden (Patch 0086), Auto-PiP nicht geprüft | mittel | S | prüfen, ob Chromiums Auto-PiP in Ahoi greift |
| Sidebar rechts, randlose Seiten, Look-and-Feel-Vorschau (0.6) | Sidebar rechts fehlt; Glass und Karten vorhanden | niedrig bis mittel | M | beobachten; Designentscheidung des Owners |
| Download-Animation Richtung Archiv, Hinweis bei versteckter Sidebar (0.6) | Chromium-Download-Bubble in der schwebenden Navigation | mittel: bei versteckter Navigation fehlt Feedback | S | mit dem Toast-System zusammen lösen |
| Command Bar ergänzt Adressen aus dem aktuellen Space, Tab nimmt an (0.6) | nicht geprüft | mittel | S | prüfen |
| Link per Drag ohne Modifier in Peek ziehen (0.6) | Peek per Kontextmenü und ⇧-Klick (0059, 0061) | niedrig bis mittel | M | beobachten |
| Split aus gespeicherten Tabs erzeugt Kopien (0.6) | nicht geprüft | niedrig | S | prüfen |
| Getting-Started-Tab mit Übungen (0.6) | fehlt | mittel bei Release | M | vor öffentlichem Release erwägen |
| Crest-Wappen-Baukasten, App-Icon-Paletten (0.6) | — | — | — | v1 schließt einen umfangreichen Wappen- und Theme-Baukasten aus |

## Vorschlag zur Reihenfolge

1. Hinweis-/Toast-System, inklusive Download-Feedback bei versteckter Navigation.
2. Mehrfachauswahl in der Sidebar.
3. Visueller Tab-Switcher.
4. Billige Prüfungen am aktuellen Build: FedCM, Retina-Favicons, Auto-PiP, Extension-Seitenpanel, Ordner-Verhalten, Command-Bar-Ergänzung.

1 bis 3 sind neue Produktfunktionen und brauchen deine Freigabe als Paket. 4 ist reine Prüfung.
