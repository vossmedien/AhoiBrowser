# Mobile private lock: failed Face ID keeps page shielded — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or skips**
on exact Xcode 27.0 DebugLocal build62
(`testFailedFaceIDKeepsLoadedPrivatePageShielded`, source `88c3cd3`).

With the device-authentication preference enabled and one loaded private
fixture page ("Scale tab"), Home/return showed the shield. "Entsperren" started
the system Face ID sheet; the host then delivered the Simulator's own
non-matching-face event (`com.apple.BiometricKit_Sim.pearl.nomatch`, i.e.
Simulator ▸ Features ▸ Face ID ▸ Non-matching Face) repeatedly. iOS ended the
evaluation with a failure itself (no Cancel button remained to tap). The app
kept the opaque shield, never exposed the private page or private address, and
showed the recovery text "Die Geräteauthentifizierung wurde abgebrochen oder
ist nicht verfügbar. Versuche es erneut." with "Entsperren" enabled again. Both
screenshots were visually reviewed.

Harness setup: host-enabled Simulator Face ID enrollment on A168, reset to 0
afterwards; A168 returned to Shutdown. Built and post-test installed bundles
were byte-for-byte equal with valid ad-hoc signature and source/build stamp
(`candidate.json`).

Limits: Simulator biometric event, not physical hardware; the passcode-fallback
path after biometric lockout, iPad scenes and VoiceOver remain open. Not Sync.
