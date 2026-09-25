# OPEN mobile defect: library does not close after creating a Workspace — 25 September 2026

Status: **RED, unresolved.** Reproduced on A168/iOS 27 fixture journey
`testLibraryClosesWithDoneAfterCreatingWorkspace` (builds 68, 71, 72) and in
the real HTTPS suite (builds 60–66, `createWorkspace` + `closeLibrary`).

Journey: Browser actions → Workspaces → Verwalten → Workspace → type name →
Erstellen. The library then shows the new Workspace's pushed detail. Tapping
"Fertig" dims the button but the sheet stays; about ten seconds later the AX
tree again contains the "Neuer Arbeitsbereich" alert (Erstellen/Abbrechen) and
the software keyboard, so the browser toolbar is not hittable. Strip
`build72-alert-detail-done.png`: alert with typed name → pushed detail with
Done → Done dimmed, detail still shown.

Changes already made (kept, none fixes the root cause alone):
`1319fb9` Done as `.confirmationAction` toolbar item of both columns (compact
detail) instead of an overlay under the pushed navigation bar; `d7b5f28`
creation alert presented after the Manage menu closes (removed a stale
`PopoverDismissRegion` that swallowed taps, confirmed absent in build71);
`94b7b06` new Workspace selected only after the alert animation.

Next step: replace the text-field alert with a small sheet/form (or commit
from an `onDismiss`) so no alert presentation can outlive its source, then
rerun this journey and the Workspace move journey. Do not claim Workspace
creation acceptance on mobile until then.
