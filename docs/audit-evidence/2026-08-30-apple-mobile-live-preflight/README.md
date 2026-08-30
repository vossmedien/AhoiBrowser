# Apple Mobile live preflight — 2026-08-30

- Observed at: `2026-08-30T08:41:25Z`
- Source baseline: `9feeeddf5a05a1aeb6e74bfce9253a27db8256ae`
- Method: authenticated Apple Developer and App Store Connect pages inspected
  read-only through Computer Use; no portal value was changed or submitted.

## Apple Developer team and App ID

- Team: `Christian Voss - 248AJ5BN47`
- App ID resource: `299MZ3376A`
- App ID prefix shown by Apple: `248AJ5BN47 (Team ID)`
- Explicit bundle ID: `app.ahoibrowser.AhoiBrowser`
- Platforms: iOS, iPadOS, macOS, tvOS, watchOS and visionOS
- iCloud is enabled with `Include CloudKit support` selected.
- The App ID has `Enabled iCloud Containers (0)`.
- Push Notifications is enabled.
- `Default Web Browser` capability request status is `No Requests`.
- `Browser App Installation` capability request status is `No Requests`.

## iCloud containers

The team's live iCloud-container list contains only:

- `iCloud de vossmedien DisplayPilot`
- `iCloud.de.vossmedien.DisplayPilot`

The dedicated target `iCloud.app.ahoibrowser.AhoiBrowser` does not yet exist.
The DisplayPilot container was not opened, changed, renamed or assigned.

## App Store Connect

- The authenticated account currently reports `Keine Apps` / no apps.
- Therefore no AhoiBrowser App Store Connect record, build, TestFlight group or
  public TestFlight link exists yet.
- App Store Connect shows an EU trader-status warning. Any personal/legal trader
  declaration remains a human-only gate and was not opened or answered.

## Exact next external boundary

The first mutation, after an action-near confirmation, is limited to creating
`iCloud.app.ahoibrowser.AhoiBrowser`, assigning it to the existing Ahoi App ID
and refreshing the matching development profile. App Store Connect app-record
creation, CloudKit Production promotion, public TestFlight, the managed
default-browser request and public release remain separate confirmation gates.
