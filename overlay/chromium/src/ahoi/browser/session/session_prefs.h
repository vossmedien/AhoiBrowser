// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_PREFS_H_
#define AHOI_BROWSER_SESSION_SESSION_PREFS_H_

#include <optional>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/feature.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/uuid.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::session {

inline constexpr char kStartupModePref[] = "ahoi.session.startup_mode";
// These profile preferences are deliberately not syncable. A workspace's
// portable identity is distinct from its device-local website-session binding.
inline constexpr char kWebsiteSessionBindingsPref[] =
    "ahoi.session.website_session_bindings";
// Crash-safe removal intents of deleted Workspaces' isolated partitions:
// a list of {"context_id", "path"}. Written before the tree deletion commits
// and cleared only after the partition directory is gone.
inline constexpr char kWebsiteSessionPendingRemovalsPref[] =
    "ahoi.session.website_session_pending_removals";

// Development gate while Chromium's native site-permission authority is still
// profile-wide. Once a profile has local bindings, disabling the feature must
// not silently route its existing isolated Workspaces to the default jar.
BASE_DECLARE_FEATURE(kAhoiWorkspaceWebsiteSessions);
bool ShouldUseWorkspaceWebsiteSessions(const PrefService* prefs);

struct WebsiteSessionBinding {
  // An invalid UUID means Chromium's existing default StoragePartition. This
  // is an explicit binding, not a missing dictionary entry.
  base::Uuid context_id;

  bool is_default() const { return !context_id.is_valid(); }
  bool operator==(const WebsiteSessionBinding&) const = default;
};

enum class StartupMode {
  kAsk = 0,
  kContinue = 1,
  kEmpty = 2,
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

std::string_view StartupModeToPrefValue(StartupMode mode);
std::optional<StartupMode> StartupModeFromPrefValue(std::string_view value);

// Unknown or corrupt stored values fail back to the product default, `kAsk`.
StartupMode GetStartupMode(const PrefService& prefs);

// Returns false for managed preferences or an invalid enum value.
bool SetStartupMode(PrefService* prefs, StartupMode mode);
bool IsStartupModeManaged(const PrefService& prefs);

// On first adoption, bind every already-persisted workspace to the existing
// default partition. New workspaces subsequently receive a fresh local context
// without moving, copying or clearing any previously stored website data.
bool InitializeWebsiteSessionBindings(
    PrefService* prefs,
    base::span<const base::Uuid> existing_workspace_ids);

// A missing entry after initialization is a newly arriving workspace. Invalid
// entries fail rather than silently sending that workspace to the default jar.
std::optional<WebsiteSessionBinding> GetOrCreateWebsiteSessionBinding(
    PrefService* prefs,
    const base::Uuid& workspace_id);

// Returns the existing binding without creating one for an unknown Workspace.
std::optional<WebsiteSessionBinding> FindWebsiteSessionBinding(
    const PrefService* prefs,
    const base::Uuid& workspace_id);

struct PendingWebsiteSessionRemoval {
  base::Uuid context_id;
  base::FilePath partition_path;
};

// Removes a deleted Workspace's binding and records its partition for removal.
// Afterwards restore treats the context as unknown (recovery partition), so
// no page can reopen with the deleted Workspace's accounts. Returns false for
// managed or corrupt state; a default binding is removed without an intent.
bool RetireWebsiteSessionBinding(PrefService* prefs,
                                 const base::Uuid& workspace_id,
                                 const base::FilePath& partition_path);
// Workspace IDs that currently hold a binding (valid state only).
std::vector<base::Uuid> GetWebsiteSessionBoundWorkspaceIds(
    const PrefService* prefs);
std::vector<PendingWebsiteSessionRemoval> GetPendingWebsiteSessionRemovals(
    const PrefService* prefs);
void CompleteWebsiteSessionRemoval(PrefService* prefs,
                                   const base::Uuid& context_id);

// Restore accepts a local isolated context only if the profile already knows
// it. Corrupt or foreign tab metadata goes to a separate, stable recovery
// partition instead of gaining access to another workspace's cookies.
bool IsKnownWebsiteSessionBinding(const PrefService* prefs,
                                  const WebsiteSessionBinding& binding);
std::optional<WebsiteSessionBinding> GetWebsiteSessionRecoveryBinding(
    const PrefService* prefs);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_SESSION_PREFS_H_
