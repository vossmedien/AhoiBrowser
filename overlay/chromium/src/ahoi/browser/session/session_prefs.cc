// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::session {

BASE_FEATURE(kAhoiWorkspaceWebsiteSessions,
             base::FEATURE_DISABLED_BY_DEFAULT);

namespace {

constexpr std::string_view kAskValue = "ask";
constexpr std::string_view kContinueValue = "continue";
constexpr std::string_view kEmptyValue = "empty";
constexpr char kDefaultWebsiteSessionValue[] = "default";
constexpr char kWebsiteSessionVersionKey[] = "version";
constexpr char kWebsiteSessionWorkspacesKey[] = "workspaces";
constexpr char kWebsiteSessionRecoveryKey[] = "recovery_context_id";
constexpr int kWebsiteSessionBindingsVersion = 1;

const base::DictValue* GetValidWebsiteSessionRoot(const PrefService* prefs) {
  if (!prefs || !prefs->FindPreference(kWebsiteSessionBindingsPref)) {
    return nullptr;
  }
  const base::DictValue& root = prefs->GetDict(kWebsiteSessionBindingsPref);
  const std::string* recovery =
      root.FindString(kWebsiteSessionRecoveryKey);
  if (root.FindInt(kWebsiteSessionVersionKey) !=
          kWebsiteSessionBindingsVersion ||
      root.size() != 3u || !root.FindDict(kWebsiteSessionWorkspacesKey) ||
      !recovery || !base::Uuid::ParseLowercase(*recovery).is_valid()) {
    return nullptr;
  }
  return &root;
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kStartupModePref, kAskValue);
  registry->RegisterDictionaryPref(kWebsiteSessionBindingsPref,
                                   base::DictValue());
  registry->RegisterListPref(kWebsiteSessionPendingRemovalsPref,
                             base::ListValue());
}

bool ShouldUseWorkspaceWebsiteSessions(const PrefService* prefs) {
  if (base::FeatureList::IsEnabled(kAhoiWorkspaceWebsiteSessions)) {
    return true;
  }
  return prefs && prefs->FindPreference(kWebsiteSessionBindingsPref) &&
         !prefs->GetDict(kWebsiteSessionBindingsPref).empty();
}

std::string_view StartupModeToPrefValue(StartupMode mode) {
  switch (mode) {
    case StartupMode::kAsk:
      return kAskValue;
    case StartupMode::kContinue:
      return kContinueValue;
    case StartupMode::kEmpty:
      return kEmptyValue;
  }
  return {};
}

std::optional<StartupMode> StartupModeFromPrefValue(std::string_view value) {
  if (value == kAskValue) {
    return StartupMode::kAsk;
  }
  if (value == kContinueValue) {
    return StartupMode::kContinue;
  }
  if (value == kEmptyValue) {
    return StartupMode::kEmpty;
  }
  return std::nullopt;
}

StartupMode GetStartupMode(const PrefService& prefs) {
  if (!prefs.FindPreference(kStartupModePref)) {
    return StartupMode::kAsk;
  }
  return StartupModeFromPrefValue(prefs.GetString(kStartupModePref))
      .value_or(StartupMode::kAsk);
}

bool SetStartupMode(PrefService* prefs, StartupMode mode) {
  if (!prefs || !prefs->FindPreference(kStartupModePref) ||
      prefs->IsManagedPreference(kStartupModePref)) {
    return false;
  }
  const std::string_view value = StartupModeToPrefValue(mode);
  if (value.empty()) {
    return false;
  }
  prefs->SetString(kStartupModePref, value);
  return true;
}

bool IsStartupModeManaged(const PrefService& prefs) {
  return prefs.FindPreference(kStartupModePref) &&
         prefs.IsManagedPreference(kStartupModePref);
}

bool InitializeWebsiteSessionBindings(
    PrefService* prefs,
    base::span<const base::Uuid> existing_workspace_ids) {
  if (!prefs ||
      !prefs->FindPreference(kWebsiteSessionBindingsPref)) {
    return false;
  }
  const base::DictValue& current =
      prefs->GetDict(kWebsiteSessionBindingsPref);
  if (GetValidWebsiteSessionRoot(prefs)) {
    return true;
  }
  if (!current.empty() || existing_workspace_ids.empty() ||
      prefs->IsManagedPreference(kWebsiteSessionBindingsPref)) {
    return false;
  }

  std::set<base::Uuid> unique_ids;
  base::DictValue bindings;
  for (const base::Uuid& id : existing_workspace_ids) {
    if (!id.is_valid() || !unique_ids.insert(id).second) {
      return false;
    }
    bindings.Set(id.AsLowercaseString(), kDefaultWebsiteSessionValue);
  }
  base::DictValue adopted;
  adopted.Set(kWebsiteSessionVersionKey, kWebsiteSessionBindingsVersion);
  adopted.Set(kWebsiteSessionWorkspacesKey, std::move(bindings));
  adopted.Set(kWebsiteSessionRecoveryKey,
              base::Uuid::GenerateRandomV4().AsLowercaseString());
  prefs->SetDict(kWebsiteSessionBindingsPref, std::move(adopted));
  return true;
}

std::optional<WebsiteSessionBinding> GetOrCreateWebsiteSessionBinding(
    PrefService* prefs,
    const base::Uuid& workspace_id) {
  if (!workspace_id.is_valid()) {
    return std::nullopt;
  }
  const base::DictValue* current = GetValidWebsiteSessionRoot(prefs);
  if (!current) {
    return std::nullopt;
  }
  const base::DictValue* workspaces =
      current->FindDict(kWebsiteSessionWorkspacesKey);
  const std::string key = workspace_id.AsLowercaseString();
  const base::Value* stored = workspaces->Find(key);
  if (stored) {
    const std::string* value = stored->GetIfString();
    if (!value) {
      return std::nullopt;
    }
    if (*value == kDefaultWebsiteSessionValue) {
      return WebsiteSessionBinding();
    }
    base::Uuid context_id = base::Uuid::ParseLowercase(*value);
    return context_id.is_valid()
               ? std::make_optional(
                     WebsiteSessionBinding{.context_id = context_id})
               : std::nullopt;
  }
  if (prefs->IsManagedPreference(kWebsiteSessionBindingsPref)) {
    return std::nullopt;
  }
  if (!base::FeatureList::IsEnabled(kAhoiWorkspaceWebsiteSessions)) {
    base::DictValue updated = current->Clone();
    updated.FindDict(kWebsiteSessionWorkspacesKey)
        ->Set(key, kDefaultWebsiteSessionValue);
    prefs->SetDict(kWebsiteSessionBindingsPref, std::move(updated));
    return WebsiteSessionBinding();
  }
  base::Uuid context_id = base::Uuid::GenerateRandomV4();
  base::DictValue updated = current->Clone();
  updated.FindDict(kWebsiteSessionWorkspacesKey)
      ->Set(key, context_id.AsLowercaseString());
  prefs->SetDict(kWebsiteSessionBindingsPref, std::move(updated));
  return WebsiteSessionBinding{.context_id = context_id};
}

bool BindNewWorkspaceWebsiteSessions(
    PrefService* prefs,
    const base::Uuid& workspace_id,
    base::span<const base::Uuid> other_workspace_ids,
    bool own_website_sessions) {
  if (!prefs || !workspace_id.is_valid() ||
      !prefs->FindPreference(kWebsiteSessionBindingsPref) ||
      prefs->IsManagedPreference(kWebsiteSessionBindingsPref)) {
    return false;
  }
  if (!GetValidWebsiteSessionRoot(prefs)) {
    if (!prefs->GetDict(kWebsiteSessionBindingsPref).empty()) {
      return false;  // Corrupt state; never overwrite it.
    }
    if (!own_website_sessions) {
      // Nothing is isolated yet; the missing entry already means shared.
      return true;
    }
    base::DictValue bindings;
    std::set<base::Uuid> unique_ids;
    for (const base::Uuid& id : other_workspace_ids) {
      if (id == workspace_id) {
        continue;
      }
      if (!id.is_valid() || !unique_ids.insert(id).second) {
        return false;
      }
      bindings.Set(id.AsLowercaseString(), kDefaultWebsiteSessionValue);
    }
    base::DictValue root;
    root.Set(kWebsiteSessionVersionKey, kWebsiteSessionBindingsVersion);
    root.Set(kWebsiteSessionWorkspacesKey, std::move(bindings));
    root.Set(kWebsiteSessionRecoveryKey,
             base::Uuid::GenerateRandomV4().AsLowercaseString());
    prefs->SetDict(kWebsiteSessionBindingsPref, std::move(root));
  }
  const base::DictValue* current = GetValidWebsiteSessionRoot(prefs);
  if (!current) {
    return false;
  }
  const std::string key = workspace_id.AsLowercaseString();
  if (current->FindDict(kWebsiteSessionWorkspacesKey)->Find(key)) {
    return false;
  }
  base::DictValue updated = current->Clone();
  updated.FindDict(kWebsiteSessionWorkspacesKey)
      ->Set(key, own_website_sessions
                     ? base::Uuid::GenerateRandomV4().AsLowercaseString()
                     : std::string(kDefaultWebsiteSessionValue));
  prefs->SetDict(kWebsiteSessionBindingsPref, std::move(updated));
  return true;
}

std::optional<WebsiteSessionBinding> FindWebsiteSessionBinding(
    const PrefService* prefs,
    const base::Uuid& workspace_id) {
  const base::DictValue* root = GetValidWebsiteSessionRoot(prefs);
  if (!root || !workspace_id.is_valid()) {
    return std::nullopt;
  }
  const std::string* value = root->FindDict(kWebsiteSessionWorkspacesKey)
                                 ->FindString(workspace_id.AsLowercaseString());
  if (!value) {
    return std::nullopt;
  }
  if (*value == kDefaultWebsiteSessionValue) {
    return WebsiteSessionBinding();
  }
  base::Uuid context_id = base::Uuid::ParseLowercase(*value);
  return context_id.is_valid()
             ? std::make_optional(WebsiteSessionBinding{.context_id = context_id})
             : std::nullopt;
}

bool RetireWebsiteSessionBinding(PrefService* prefs,
                                 const base::Uuid& workspace_id,
                                 const base::FilePath& partition_path) {
  const base::DictValue* root = GetValidWebsiteSessionRoot(prefs);
  if (!root || prefs->IsManagedPreference(kWebsiteSessionBindingsPref) ||
      !prefs->FindPreference(kWebsiteSessionPendingRemovalsPref)) {
    return false;
  }
  const std::optional<WebsiteSessionBinding> binding =
      FindWebsiteSessionBinding(prefs, workspace_id);
  if (!binding.has_value()) {
    return true;  // Nothing bound: nothing to retire.
  }
  if (!binding->is_default()) {
    if (partition_path.empty()) {
      return false;
    }
    // Intent first: a crash after this point resumes the removal at startup.
    base::ListValue pending =
        prefs->GetList(kWebsiteSessionPendingRemovalsPref).Clone();
    base::DictValue entry;
    entry.Set("context_id", binding->context_id.AsLowercaseString());
    entry.Set("path", partition_path.AsUTF8Unsafe());
    pending.Append(std::move(entry));
    prefs->SetList(kWebsiteSessionPendingRemovalsPref, std::move(pending));
  }
  base::DictValue updated = root->Clone();
  updated.FindDict(kWebsiteSessionWorkspacesKey)
      ->Remove(workspace_id.AsLowercaseString());
  prefs->SetDict(kWebsiteSessionBindingsPref, std::move(updated));
  return true;
}

std::vector<base::Uuid> GetWebsiteSessionBoundWorkspaceIds(
    const PrefService* prefs) {
  std::vector<base::Uuid> ids;
  const base::DictValue* root = GetValidWebsiteSessionRoot(prefs);
  if (!root) {
    return ids;
  }
  for (const auto entry : *root->FindDict(kWebsiteSessionWorkspacesKey)) {
    base::Uuid id = base::Uuid::ParseLowercase(entry.first);
    if (id.is_valid()) {
      ids.push_back(std::move(id));
    }
  }
  return ids;
}

std::vector<PendingWebsiteSessionRemoval> GetPendingWebsiteSessionRemovals(
    const PrefService* prefs) {
  std::vector<PendingWebsiteSessionRemoval> result;
  if (!prefs || !prefs->FindPreference(kWebsiteSessionPendingRemovalsPref)) {
    return result;
  }
  for (const base::Value& item :
       prefs->GetList(kWebsiteSessionPendingRemovalsPref)) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* id = entry ? entry->FindString("context_id") : nullptr;
    const std::string* path = entry ? entry->FindString("path") : nullptr;
    if (!id || !path) {
      continue;
    }
    base::Uuid context_id = base::Uuid::ParseLowercase(*id);
    if (context_id.is_valid() && !path->empty()) {
      result.push_back({.context_id = std::move(context_id),
                        .partition_path = base::FilePath::FromUTF8Unsafe(*path)});
    }
  }
  return result;
}

void CompleteWebsiteSessionRemoval(PrefService* prefs,
                                   const base::Uuid& context_id) {
  if (!prefs || !prefs->FindPreference(kWebsiteSessionPendingRemovalsPref)) {
    return;
  }
  base::ListValue pending =
      prefs->GetList(kWebsiteSessionPendingRemovalsPref).Clone();
  const std::string id = context_id.AsLowercaseString();
  pending.EraseIf([&id](const base::Value& item) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* stored = entry ? entry->FindString("context_id") : nullptr;
    return !stored || *stored == id;
  });
  prefs->SetList(kWebsiteSessionPendingRemovalsPref, std::move(pending));
}

bool IsKnownWebsiteSessionBinding(const PrefService* prefs,
                                  const WebsiteSessionBinding& binding) {
  if (binding.is_default()) {
    return true;
  }
  const base::DictValue* root = GetValidWebsiteSessionRoot(prefs);
  if (!root) {
    return false;
  }
  const std::string context_id = binding.context_id.AsLowercaseString();
  if (*root->FindString(kWebsiteSessionRecoveryKey) == context_id) {
    return true;
  }
  const base::DictValue* workspaces =
      root->FindDict(kWebsiteSessionWorkspacesKey);
  for (auto entry : *workspaces) {
    const std::string* stored = entry.second.GetIfString();
    if (stored && *stored == context_id) {
      return true;
    }
  }
  return false;
}

std::optional<WebsiteSessionBinding> GetWebsiteSessionRecoveryBinding(
    const PrefService* prefs) {
  const base::DictValue* root = GetValidWebsiteSessionRoot(prefs);
  if (!root) {
    return std::nullopt;
  }
  return WebsiteSessionBinding{
      .context_id = base::Uuid::ParseLowercase(
          *root->FindString(kWebsiteSessionRecoveryKey))};
}

}  // namespace ahoi::session
