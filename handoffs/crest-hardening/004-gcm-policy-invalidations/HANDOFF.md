# 004 – Stop GCM check-in on unmanaged profiles (H5, N1)

Status: ready
Owner lane: desktop (patch stack; assign the next series number)
Base: upstream `chrome/browser/policy/cloud/user_fm_registration_token_uploader_factory.cc`
at tag `153.0.8010.53` (unpatched by Ahoi at lane commit time)

## Finding

Every fresh Ahoi profile checks in with GCM, requests FCM tokens and opens the
MCS connection. The logs show `registration_request.cc:291 … DEPRECATED_ENDPOINT`
about 10 s after launch (for example in
`artifacts/e2e/native-peer-55abcf7-20260913/maca-runtime.log`). The profile's
GCM store holds check-in data (`gservice1-android_id`) but no registrations.

The trigger chain was established by reading the source, not by a runtime trace:
1. On macOS `ProfileImpl` always creates a `UserCloudPolicyManager`.
2. `UserFmRegistrationTokenUploaderFactory` builds a service with every
   profile whenever a policy manager exists.
3. After profile initialization, `UserFmRegistrationTokenUploader` creates
   uploaders for the policy and remote-command invalidation projects.
4. `FmRegistrationTokenUploader` starts `InvalidationListener`, which calls
   `AddAppHandler` and `InstanceID::GetToken`.
5. The GCM driver then starts: check-in, `register3`, then the MCS login.

Web Push, sync, sharing and CBCM were ruled out for a fresh profile:
- Web Push only adds a handler once a site has a subscription.
- Sync and sharing need a Google sign-in, which Ahoi disallows.
- CBCM needs machine enrollment.

## Change

`user-policy-invalidations.patch` returns no uploader service when the profile's
cloud policy manager is the Gaia-based `UserCloudPolicyManager`. Such a
manager can never register a client, because Ahoi disallows Google sign-in.
Profile-level management (`ProfileCloudPolicyManager`, enrollment token) keeps
invalidations. No GCM code is removed, so Web Push and `chrome.gcm` still
start GCM when a site or an extension actually uses them.

## Apply

Add the patch to `patches/chromium/` under the next free number in `series`,
with a README ledger entry. Include it in the next planned desktop package;
no extra build.

## Expected tests

- Build of the next package; no unit test exists for this factory upstream.
- **NET-GCM-01**: fresh profile, 10 minutes idle, then quit. The log contains
  no `registration_request`/`DEPRECATED_ENDPOINT` line, and the profile's GCM
  store has no `gservice1-android_id`. The capture has no connection to
  `android.clients.google.com` or to `mtalk.google.com:5228`.
- **NET-GCM-02** (records the product decision): subscribe to one VAPID Web Push
  on a test page. If `register3` is rejected (`DEPRECATED_ENDPOINT`) for
  unbranded, keyless builds, Web Push does not work in Ahoi at all. Either
  document Web Push and `chrome.gcm` as unsupported, or disable GCM the
  ungoogled way (`disable-gcm.patch`). Decide explicitly; do not leave a
  silently broken push.

## Risks

A profile that is managed only through Gaia user cloud policy would lose policy
invalidations. Ahoi cannot create such a profile because sign-in is disallowed.
