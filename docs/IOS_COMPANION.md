# iOS and iPadOS companion

The companion is a native SwiftUI app for iOS/iPadOS 26. It does not embed a
browser engine. It can search and edit synced workspaces/tree/history, inspect
device tabs, open a URL in the system default browser, and request narrowly
scoped actions on normal Mac tabs.

The app uses Apple string catalogs for German and English, system/light/dark
appearance, shared theme tokens where practical, Dynamic Type, VoiceOver,
keyboard support on iPad, and reduced-motion/transparency settings.

## Remote command contract

Permitted commands are open URL using `https`/`http` after canonical validation,
focus one known normal tab, and close one explicitly selected normal tab. Each
command includes version, command ID, issuing/target device IDs, nonce, issued
and expiry timestamps (maximum five minutes), exact action and target, and a
signature from approved device material.

The Mac rejects unknown devices, invalid signatures, replayed nonces, expired
commands, unsupported schemes/actions, incognito targets, over-broad counts, and
commands received while remote control is disabled. There is no shell, script,
developer override, secret transfer, arbitrary custom scheme, or mass-close API.
