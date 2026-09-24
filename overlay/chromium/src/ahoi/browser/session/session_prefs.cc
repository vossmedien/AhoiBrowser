// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>

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
  base::Uuid context_id = base::Uuid::GenerateRandomV4();
  base::DictValue updated = current->Clone();
  updated.FindDict(kWebsiteSessionWorkspacesKey)
      ->Set(key, context_id.AsLowercaseString());
  prefs->SetDict(kWebsiteSessionBindingsPref, std::move(updated));
  return WebsiteSessionBinding{.context_id = context_id};
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
