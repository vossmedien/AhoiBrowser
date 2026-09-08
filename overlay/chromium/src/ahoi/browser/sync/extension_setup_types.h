// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_EXTENSION_SETUP_TYPES_H_
#define AHOI_BROWSER_SYNC_EXTENSION_SETUP_TYPES_H_

#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_authorization.h"
#include "base/uuid.h"

namespace ahoi::sync {

// Source is code-owned routing, NEVER a URL, package bytes, signing key or a
// claim that a peer already granted this profile's extension permissions.
enum class ExtensionInstallSource {
  kChromeWebStore = 0,
  kPinnedUblockClassic = 1
};

struct ExtensionDesiredConfiguration {
  std::string extension_id;
  ExtensionInstallSource source = ExtensionInstallSource::kChromeWebStore;
  bool installed = true;
  bool enabled = true;

  friend bool operator==(const ExtensionDesiredConfiguration&,
                         const ExtensionDesiredConfiguration&) = default;
};

// Partial/loading capture is not uninstall intent. Native should include all
// eligible regular-profile extensions, excluding unpacked, component, policy
// and externally installed packages whose trusted restore route is unknown.
struct NativeExtensionSetupSnapshot {
  bool complete = false;
  // ALL actually installed profile IDs, including non-restorable entries.
  // Local readback only: absence from the eligible subset below cannot prove
  // successful uninstall of an extension that became policy-managed meanwhile.
  std::vector<std::string> installed_extension_ids;
  std::vector<ExtensionDesiredConfiguration> extensions;
};

enum class ExtensionRestoreDisposition {
  kApplied,
  kNeedsConfirmation,
  kPending,
  kBlockedByPolicy,
  kUnsupported,
  kFailed,
  kCancelled,
};

struct ExtensionRestoreRequest {
  ExtensionDesiredConfiguration desired;
  base::Uuid operation_id;
  // Opaque binding to the exact intended shared version. Preserve through
  // native callbacks; it is not a file path or a second native storage ID.
  std::string revision;
  // True only for an explicit local Retry/Install action. Passive sync must
  // not steal focus to raise a permission prompt; report NeedsConfirmation.
  bool user_initiated = false;
  SyncAuthorization authorization;
};

struct ExtensionRestoreResult {
  base::Uuid operation_id;
  std::string revision;
  ExtensionRestoreDisposition disposition =
      ExtensionRestoreDisposition::kUnsupported;
};

bool IsValidExtensionDesiredConfiguration(
    const ExtensionDesiredConfiguration& desired);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_EXTENSION_SETUP_TYPES_H_
