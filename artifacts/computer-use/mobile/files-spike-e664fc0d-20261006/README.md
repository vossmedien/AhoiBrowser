# Native Files UI baseline on e664fc0d

Inhouse, own A058D204 simulator, DebugLocal, exact clean-source receipt and
installed-byte readback. Two native journeys executed: cancellation PASS
(29.96s), import FAIL (46.07s), no skips. Original xcodebuild exit65 and target
xcresult are preserved; the failure is at the loaded-state assertion. Focused
runtime tests were not run after that failure.

`selected-folder.png` actually shows the app Documents parent, containing the
one five-file fixture folder. A returned tap alone did not prove entry into
that folder. `import-failed-tree.txt` shows `Test extension import failed`.
The later test correction requires the fixture navigation bar before Open.
The loader separately acquires security scope before its first metadata read,
matching Apple's public Files-import contract. That correction's role in this
specific failure was a hypothesis, not a claimed passing fix.

`candidate-receipt.json`, `runner-binding.json` and `installed-readback.json`
bind the app, source, original runner and receipt-bearing runner.
`cleanup.json` proves own device Shutdown, own E2E lock release and removal of
only the staged input after exact content-hash checks. Foreign devices/data
were untouched. This is a baseline, not complete ADR0012 Step1 acceptance.
Canonical logs omit trailing blank lines; raw target logs remain unchanged.
