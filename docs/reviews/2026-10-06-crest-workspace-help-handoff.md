# Workspace-Trennung im vorhandenen Dialog präzisieren

Source-Handoff für Paket 7 des [Integrationsplans](2026-10-06-crest-integration-plan.md), eine bestehende Sidebar-Datei ausschließlich im [Patch](2026-10-06-crest-workspace-help-handoff.patch). Desktop-Owner Claude behält die Schreibgrenze.

`AddWorkspaceLevelChoice` erklärt bereits drei Stufen. Seine Formulierung „Erweiterungen teilen alle Workspaces“ ist jedoch neben „Vollständig getrennt“ missverständlich: Gemeinsame History, Passwörter, Berechtigungen und Extensions gelten innerhalb desselben Chromium-Profils. Außerdem sagt „Die Stufe lässt sich später nicht ändern“ zu viel, denn der vorhandene `kConvertToIsolated`-Dialog unterstützt die spätere vollständige Profiltrennung.

Die kleine Deutsch-/Englisch-Textänderung erklärt ausdrücklich die gemeinsamen Website-Anmeldungen, begrenzt das Teilen weiterer Browserdaten auf dieses Profil und beschreibt nur den nicht verfügbaren Wechsel zwischen „Gemeinsam“ und „Eigene Website-Sitzungen“. Die vorhandene Erklärung der vollständigen Trennung bleibt passend. Keine Daten-, Profil-, Permission- oder Migrationslogik wird verändert; keine neue Isolationsstufe.

Sourcebelege: `browser_sidebar_host_workspace_dialog.cc` (`AddWorkspaceLevelChoice`, `ShowWorkspaceDialog`, `AcceptWorkspaceDialog`) und `session/isolated_profile_creation.cc` (`ConvertToIsolatedWorkspace`). Der betreffende Dialogtext ist im aktuellen Desktop-Owner-Source identisch zur Handoff-Basis. `git apply --check --whitespace=error-all` PASS; Locale-Konvention und verbleibende Layout-/Sourcegrenzen geprüft. Kein Build, Test-Binary oder sichtbarer Lauf.

Nach dem Cherry-Pick des Handoff-Commits im gemeinsamen Kandidaten:

```sh
git apply --check --whitespace=error-all docs/reviews/2026-10-06-crest-workspace-help-handoff.patch
git apply docs/reviews/2026-10-06-crest-workspace-help-handoff.patch
```

Offen beim Owner: Dialog Deutsch/Englisch in kleinem Fenster und per Tastatur/VoiceOver lesen; alle drei Optionen erreichbar, vollständig getrennter Workspace öffnet weiter über den vorhandenen Pfad. Textänderung nicht als neue Isolations-/Migrationsabnahme werten. Reguläre Source-/Kandidaten-/Auslieferungsgrenzen bleiben erhalten.
