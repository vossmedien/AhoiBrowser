// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SECRET_STORE_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SECRET_STORE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/functional/callback.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
namespace crypto::apple {
class KeychainV2;
}
#endif

namespace ahoi {

// Platform boundary for macOS Keychain. Implementations must never log,
// synchronize, persist in prefs, or attach secret values to crash metadata.
// The core only transports opaque `ahoi-keychain:*` references.
class DeveloperSecretStore {
 public:
  virtual ~DeveloperSecretStore() = default;

  virtual std::optional<std::string> Store(std::string_view label,
                                           std::string_view secret) = 0;
  virtual std::optional<std::string> Resolve(
      std::string_view reference) const = 0;
  virtual bool Remove(std::string_view reference) = 0;
};

// Factories are invoked on a MayBlock ThreadPool sequence. Implementations
// must create an independent store handle and must not capture UI objects.
using DeveloperSecretStoreFactory =
    base::RepeatingCallback<std::unique_ptr<DeveloperSecretStore>()>;

// Returns the native local-only store for the current platform. macOS uses a
// non-synchronizable generic-password item protected by the app's Keychain
// access policy. Unsupported platforms fail closed with nullptr.
std::unique_ptr<DeveloperSecretStore> CreatePlatformDeveloperSecretStore();

#if BUILDFLAG(IS_MAC)
// Supplies an explicit access group and injected Keychain for tests only.
std::unique_ptr<DeveloperSecretStore> CreateMacDeveloperSecretStoreForTesting(
    crypto::apple::KeychainV2* keychain,
    std::string access_group);
#endif

// Resolves references into a short-lived rule vector. Failure is atomic: no
// partially materialized rule set is returned and callers must skip the whole
// header profile. The returned rules contain no references and must not cross
// a persistence, sync, logging, crash-report or evidence boundary.
std::optional<std::vector<DeveloperHeaderRule>> MaterializeDeveloperHeaderRules(
    const std::vector<DeveloperHeaderRule>& rules,
    const DeveloperSecretStore& secret_store);

// True only when an enabled request/response rule set contains an opaque
// Keychain reference. Disabled saved rules never trigger Keychain access.
bool DeveloperProfileHasActiveHeaderSecretReferences(
    const DeveloperProfile& profile);

// Resolves enabled request and response rules as one transaction. Failure in
// either direction returns no profile, so callers cannot apply a partial set.
// The returned profile is request-local plaintext and must never be persisted,
// synchronized, logged, displayed, or cached beyond that request.
std::optional<DeveloperProfile> MaterializeDeveloperProfileHeaderSecrets(
    DeveloperProfile profile,
    const DeveloperSecretStore& secret_store);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SECRET_STORE_H_
