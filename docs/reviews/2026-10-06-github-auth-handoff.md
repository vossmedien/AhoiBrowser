# AHOI-GIT-AUTH-20261006: vorhandener GitHub-Zugang

6. Oktober 2026; Delegation `cb1e5d9c`; nur Zugangsdiagnose und dieser Bericht.

## Ergebnis

Der vorhandene HTTPS-/gh-Keyring-Pfad funktioniert in der neuen Workersitzung.
`gh auth status --hostname github.com` ergab Exit 0: `vossmedien` ist aktiv,
Credentials stammen aus dem Keyring, Protokoll HTTPS, vorhandener Scope `repo`.
Die Token-Zeile wurde vor der Anzeige entfernt. Kein Login, Accountwechsel,
Credential-Write oder dauerhaftes Config-Edit war nötig.

Die echte unveröffentlichende Push-Probe ergab Exit 0:

```sh
GH_CONFIG_DIR=/Users/vossmedien/.config/gh GIT_TERMINAL_PROMPT=0 \
  /opt/homebrew/bin/git -c core.hooksPath=/dev/null \
  push --dry-run --porcelain origin \
  151004282265b5d753a95ed3daa483017c8d6e09:refs/heads/codex/desktop-core-feature-wave-20260830
```

```text
To https://github.com/vossmedien/AhoiBrowser.git
 \t151004282265b5d753a95ed3daa483017c8d6e09:refs/heads/codex/desktop-core-feature-wave-20260830\t72e2335f..15100428
Done
```

Die Porcelain-Trenner sind oben als `\t` dargestellt. Es wurde nichts gepusht.
`GH_CONFIG_DIR` benennt die bestehende reguläre Konfiguration; `HOME` und
`CODEX_HOME` bleiben unverändert. Hooks wurden ausschließlich für diese Probe
prozesslokal deaktiviert, damit pre-push-Hooks keine eigenen externen Writes
verursachen. Das ersetzt keine Integrations-/Releasegates.

## Tatsächlicher Credential-Flow und Grenzen

| Grenze | Lesend beobachtet |
| --- | --- |
| System-Gitconfig | `/opt/homebrew/etc/gitconfig`: `credential.helper=osxkeychain` |
| GitHub-Helper aus `/Users/vossmedien/.gitconfig` | Erst leerer Eintrag, danach `!/opt/homebrew/bin/gh auth git-credential` |
| Effektiver Helper | Der leere Eintrag setzt die Liste zurück; für dieses HTTPS-Remote wird ausschließlich gh verwendet |
| Workerumgebung | `HOME=/Users/vossmedien`; isoliertes CodexHome unverändert; keine GH-/GitHub-Token-Env-Variablen gesetzt |
| Nativer Internet-Passwort-Fallback | Metadatenlookup `github.com` mit `vossmedien` bzw. `christianvossCaeli`: jeweils Exit 44, kein passender Eintrag; kein `-g`/`-w`, kein fremder Account verwendet |

Das alleinige Setzen von `GIT_CONFIG_GLOBAL` aktiviert hier also keinen nativen
Keychain-Fallback: Die globale Datei delegiert GitHub weiterhin an gh.
`CODEX_HOME` allein bestimmt dagegen nicht die gh-Konfiguration. Der reguläre
gh-Pfad ist aktuell nutzbar; ein externer Zugangsentzug ist nicht nachgewiesen.

Die früheren Fehler des Hauptowners bleiben historische Befunde. Seine
Aufrufbelege enthalten bereits dasselbe `GH_CONFIG_DIR`; die damalige
Prozess-/Keyring-/Erreichbarkeitsursache ist nicht rückwirkend geklärt. Der
heutige Erfolg beweist keine damalige Token-Widerrufung oder Credential-Reparatur.
Die erfolgreiche Probe gilt für diesen Workerprozess, noch nicht automatisch
für den laufenden Hauptownerprozess. Keine weiteren Auth-Proben nach dem Erfolg.

Unkritische Diagnosekommandos:

```sh
git config --show-origin --show-scope --get-all credential.helper
git config --show-origin --show-scope --get-all credential.https://github.com.helper
git config --get-urlmatch credential https://github.com/vossmedien/AhoiBrowser.git
git remote get-url --push origin
/usr/bin/security find-internet-password -s github.com -a vossmedien
/usr/bin/security find-internet-password -s github.com -a christianvossCaeli
```

Versions-/Dokumentationsabgleich: installiert Git 2.55.0 und gh 2.102.0.
Context7 `/git/htmldocs` bestätigt den [Helper-Reset](https://github.com/git/htmldocs/blob/gh-pages/gitcredentials.adoc);
`/cli/cli` bestätigt die [GH_CONFIG_DIR-Auswahl](https://cli.github.com/manual/gh_help_environment).
Da Context7 gh-Trunk statt den installierten Tag liefert, wurde der
[Helper von gh v2.102.0](https://raw.githubusercontent.com/cli/cli/v2.102.0/pkg/cmd/auth/gitcredential/helper.go)
zusätzlich geprüft: `store` und `erase` sind wirkungslos. Git-Hooks können
[über core.hooksPath deaktiviert werden](https://git-scm.com/docs/git-config/2.55.0#Documentation/git-config.txt-corehooksPath).
Es wurden keine Tokens ausgegeben, exportiert, kopiert, gespeichert oder gelöscht.

## Native Start-/Goal-/Config-Belege

| Bindung | Beobachteter Beleg |
| --- | --- |
| Neue Cockpit-Session | `73CA9F82-2D8E-4D4A-AA39-49F1A5601D46`, Worker für `cb1e5d9c` |
| Neuer Thread / eigenes Goal | `01a111a0-ba57-71f1-a064-88cf9d5c535d`; zuerst `get_goal: null`, danach genau ein `create_goal` mit dem vollständigen beauftragten Auth-Ziel; erneut `get_goal: active` |
| Bestehender Account | `DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5` |
| Unverändertes CODEX_HOME | `/Users/vossmedien/Library/Application Support/Terminal Cockpit/Accounts/DEFAA5B4-A7FE-4558-9214-CAFEAD98D4C5/CodexHome` |
| Native angewendete Konfiguration | `gpt-6.1-sol`, `xhigh`, Provider `openai`, `never`, `danger-full-access` |
| Nativer Start | `2026-10-06T14:31:48.605Z`, `codex-tui`, CLI 0.160.1 |
| Eigener Worktree / Branch | `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-codex-cb1e5d9c`; `cockpit/codex-cb1e5d9c` |
| Startrevision / geprüfter Source | `151004282265b5d753a95ed3daa483017c8d6e09` |
| Hauptowner bleibt separat | Session `68E66C9E-7E52-4842-B328-03D9CD6D3057`, Thread/Goal `01a11179-cb6b-7dc1-9e53-f3d72fefc198`, ursprüngliches Master-Ziel unverändert |

Belege: `cockpit_sessions`, lesendes SQLite `mode=ro` / `session_references`
und native `session_meta`/`turn_context` im vorhandenen CodexHome:
`sessions/2026/10/06/rollout-2026-10-06T16-31-48-01a111a0-ba57-71f1-a064-88cf9d5c535d.jsonl`.
Der Dateidefault `high` wurde nicht mit dem angewendeten `xhigh` verwechselt.
Keine Änderung am Root-Goal oder an anderen Sessions.

Ein separater Jev-Entscheidungsbeleg ist offen: Die einmalige Metadatenanfrage
`a90a716c-5818-4a79-817c-38f2ab822e35` mit bestätigter eigener Bindung ergab
`SOURCE_UNAVAILABLE`; die Statusabfrage derselben UUID ergab `REQUEST_NOT_FOUND`.
Keine Zustellung behauptet, kein Retry, kein neuer Worker. Native Start- und
Config-Belege bestehen unabhängig von dieser Schnittstellengrenze.

## Aufgabenstand und Handback

- [x] Eigene Session, Thread, Goal, Account und angewendete Konfiguration belegt.
- [x] Reale Helper-Kette und vorhandene reguläre Auth-Anbindung geprüft.
- [x] Echte unveröffentlichende Push-Probe erfolgreich; Stop-Bedingung erreicht.
- [x] Nur diesen Bericht erstellt und auf Scope, Secretfreiheit und Befunde geprüft.
- [ ] Zusätzliche Jev-Entscheidungsquittung: oben dokumentiertes Schnittstellengate.

Ownership-Notiz `cca7358d` an den Hauptowner wurde angenommen; das beweist noch
keine Verarbeitung dort. Source, Checkpoints, Build, UI, Installation, Integration
und Veröffentlichung bleiben unberührt und beim Hauptowner.

Dokumentprüfung: eigener Diff, `git diff --check`, DCO-/Lane-Trailer und
`python3 tools/check_lane_boundaries.py --all --since 151004282265b5d753a95ed3daa483017c8d6e09`.
Kein Build, keine Testmatrix, kein Codepaket und deshalb kein natives Code-Review.
Der lokale Commit und Prüfstatus werden im einmaligen `cockpit_report` genannt.

Nächster Schritt: Hauptowner übernimmt den belegten gh-Konfigurationspfad in
seinem normalen CLI-Prozess und setzt seinen bewachten Integrations-/Lieferweg
fort. Falls dessen Prozess weiter scheitert, gezielt die konkrete Prozess- und
Keyring-Grenze gegen diesen erfolgreichen Beleg vergleichen, ohne neue
Credentials, Accountwechsel oder unveränderte Retryserie.

## Abnahme durch den Hauptowner

Der Hauptowner hat den tatsächlichen Ein-Datei-Diff von cdeeae4e gegen Basis
15100428 geprüft und den Bericht übernommen. Seine einmalige Probe im isolierten
Prozess mit beiden vorhandenen Konfigurationspfaden blieb rot. Konkrete aktuelle
Grenze: `security list-keychains -d user` liefert dort keine Suchliste; dieselbe
installierte gh-Version 2.102.0 meldet deshalb keinen nutzbaren Keyring-Zugang.
Die damalige historische Ursache bleibt getrennt davon ungeklärt.

Der vorhandene `tools/desktop_e2e/run-in-gui-session.sh`-Weg hat danach die echte
reguläre CLI-Sitzung des gleichen macOS-Benutzers geprüft. HOME entstand dort
regulär als /Users/vossmedien; weder Root-HOME noch Suchlisten wurden verändert.
Der unveränderte bestehende CODEX_HOME wurde weitergereicht. Die reguläre Liste
enthält die vorhandenen login-/openvpn-/System-Keychains; gh meldet vossmedien
aktiv/gültig, HTTPS. Eine echte unveröffentlichende Push-Probe des Hauptowners
mit denselben Konfigurationspfaden bestand: 72e2335f..92f49b8a, Exit 0. Hooks
waren ausschließlich in dieser Dry-Run-Probe ausgeschaltet. Kein Secret wurde
exportiert oder kopiert, kein Account/Keychain/Credential verändert.

Beleg: `artifacts/tests/github-auth-cb1e5d9c-20261006/root-gui-probe.log`.
Dieser Beleg akzeptiert den regulären Git-Zugangsweg im Hauptowner; er ist keine
Kandidaten-, Defaultbranch-, Release- oder Jev-Abnahme. Ein echter Push verwendet
weiter alle regulären Hooks und bleibt von den jeweiligen Produktgates getrennt.
