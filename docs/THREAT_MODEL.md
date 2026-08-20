# Threat model

## Protected assets

Browsing credentials and sessions, HTTP-auth secrets, password-manager data,
history/tree metadata, developer header secrets, synced records, update signing
keys, remote-command authorization, microphone/camera/screen data, downloaded
files, and the integrity of browser navigation.

## Relevant adversaries

- malicious or compromised websites and cross-origin content
- malicious extensions or native-messaging hosts
- network attackers, captive portals, and malicious proxies
- compromised sync records or replayed remote commands
- a local process attempting to read browser data or inject UI
- compromised update infrastructure or a malicious downgrade
- accidental disclosure through logs, crash reports, screenshots, or support data

## Trust boundaries

Renderer processes are untrusted. Browser-process services validate renderer
messages and preserve Chromium's sandbox boundaries. Extension processes are not
trusted with Ahoi secrets. CloudKit transports encrypted permitted records but
is not trusted with plaintext encrypted-value contents. The iOS companion may
request only enumerated normal-profile tab commands; the Mac validates device,
signature, nonce, five-minute TTL, replay state, target, and scope.

## Explicit non-goals

AhoiBrowser cannot protect data after full compromise of the logged-in macOS
account or kernel, guarantee anonymity, bypass DRM restrictions, or make unsafe
developer overrides harmless. Incognito limits local persistence but does not
hide traffic from sites, employers, ISPs, or network observers.

## Mandatory abuse cases

Tests cover cross-workspace session consistency, OTR isolation, auth credential
realm confusion, plaintext HTTP warnings, remote-command replay/expiry, poisoned
sync ordering, malicious extension attempts, hidden developer overrides, update
signature failure, TLS error handling, and secret leakage into diagnostics.
