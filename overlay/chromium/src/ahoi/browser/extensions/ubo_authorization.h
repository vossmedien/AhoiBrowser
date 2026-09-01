// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_AUTHORIZATION_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_AUTHORIZATION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/extensions/ubo_catalog.h"
#include "ahoi/browser/extensions/ubo_package_verifier.h"
#include "base/memory/raw_ptr.h"
#include "base/types/expected.h"
#include "base/version.h"
#include "url/gurl.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace extensions {
class Extension;
}

namespace ahoi::extensions {

inline constexpr char kUboAuthorizationPref[] =
    "ahoi.extensions.ubo.authorization";

struct UboAuthorizationState {
  uint64_t sequence = 0;
  std::string extension_id;
  base::Version version;
  std::string package_sha256;
  std::string crx_public_key_sha256;
  GURL update_manifest_url;
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

std::optional<UboAuthorizationState> ReadCommittedUboAuthorization(
    const PrefService& prefs);

// Checks rollback invariants before any package bytes are accepted. This is
// repeated when the transient install authorization is created so time of
// check and time of use are independently protected.
base::expected<void, UboVerificationError>
CheckUboCatalogAgainstCommittedAuthorization(const PrefService& prefs,
                                             const UboCatalogEntry& entry);

// Called when the fixed extension is removed. Authorization is local profile
// security state, never synced, and must not survive an uninstall.
void ClearCommittedUboAuthorization(PrefService* prefs);

// Removes only the process-local, uncommitted exception for a profile. Profile
// shutdown must call this before PrefService destruction so the registry never
// retains a dangling address.
void ClearPendingUboInstallAuthorization(PrefService* prefs);

// This is the only runtime MV2 exception predicate. It first requires the
// compile-time product gate, then accepts exactly an internal MV2 extension
// matching a verified pending transaction or the last atomically committed
// authorization. MV3 and all unrelated extension paths return false and
// continue through Chromium's normal policy.
bool IsUboManifestV2ExtensionAllowed(const PrefService& prefs,
                                     const ::extensions::Extension& extension);

class UboInstallAuthorization {
 public:
  UboInstallAuthorization(const UboInstallAuthorization&) = delete;
  UboInstallAuthorization& operator=(const UboInstallAuthorization&) = delete;
  ~UboInstallAuthorization();

  // Commits only after Chromium reports a successful atomic CRX install and
  // the resulting extension still matches the verified metadata. Destruction
  // without Commit() removes the temporary exception, leaving the old
  // committed version authorized and runnable.
  base::expected<void, UboVerificationError> Commit(
      const ::extensions::Extension& installed_extension);

 private:
  friend base::expected<std::unique_ptr<UboInstallAuthorization>,
                        UboVerificationError>
  BeginUboInstallAuthorization(PrefService*,
                               const UboCatalogEntry&,
                               const VerifiedUboPackage&);

  UboInstallAuthorization(PrefService* prefs,
                          UboAuthorizationState state,
                          uint64_t token);
  void Abort();

  raw_ptr<PrefService> prefs_;
  UboAuthorizationState state_;
  uint64_t token_;
  bool active_ = true;
};

base::expected<std::unique_ptr<UboInstallAuthorization>, UboVerificationError>
BeginUboInstallAuthorization(PrefService* prefs,
                             const UboCatalogEntry& entry,
                             const VerifiedUboPackage& package);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_AUTHORIZATION_H_
