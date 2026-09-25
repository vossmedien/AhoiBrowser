// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/isolated_profile_registry.h"

#include <algorithm>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace ahoi::session {

namespace {

constexpr char kDirKey[] = "profile_dir";
constexpr char kWorkspaceKey[] = "workspace_id";
constexpr char kNameKey[] = "name";
constexpr char kIconKey[] = "icon";
constexpr char kAccentKey[] = "accent_argb";
constexpr char kStateKey[] = "state";

// A plain directory base name: no separators, no parent references.
bool IsValidProfileDir(std::string_view dir) {
  return !dir.empty() && dir != "." && dir != ".." &&
         dir.find('/') == std::string_view::npos &&
         dir.find('\\') == std::string_view::npos;
}

std::optional<IsolatedProfileEntry> Decode(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* dir = dict->FindString(kDirKey);
  const std::string* workspace = dict->FindString(kWorkspaceKey);
  const std::string* name = dict->FindString(kNameKey);
  const std::string* icon = dict->FindString(kIconKey);
  const std::optional<int> state = dict->FindInt(kStateKey);
  if (!dir || !IsValidProfileDir(*dir) || !workspace || !name || !icon ||
      !state || *state < static_cast<int>(IsolatedProfileState::kCreating) ||
      *state > static_cast<int>(IsolatedProfileState::kDeleting)) {
    return std::nullopt;
  }
  IsolatedProfileEntry entry{
      .profile_dir = *dir,
      .workspace_id = base::Uuid::ParseLowercase(*workspace),
      .name = base::UTF8ToUTF16(*name),
      .icon = base::UTF8ToUTF16(*icon),
      .state = static_cast<IsolatedProfileState>(*state),
  };
  if (!entry.workspace_id.is_valid()) {
    return std::nullopt;
  }
  if (const base::Value* accent = dict->Find(kAccentKey)) {
    // Stored as a double because ARGB exceeds int; it must be an exact
    // 32-bit value.
    const std::optional<double> number = accent->GetIfDouble();
    if (!number || *number < 0 || *number > 0xFFFFFFFFu ||
        *number != static_cast<double>(static_cast<uint32_t>(*number))) {
      return std::nullopt;
    }
    entry.accent_argb = static_cast<uint32_t>(*number);
  }
  return entry;
}

base::DictValue Encode(const IsolatedProfileEntry& entry) {
  base::DictValue dict;
  dict.Set(kDirKey, entry.profile_dir);
  dict.Set(kWorkspaceKey, entry.workspace_id.AsLowercaseString());
  dict.Set(kNameKey, base::UTF16ToUTF8(entry.name));
  dict.Set(kIconKey, base::UTF16ToUTF8(entry.icon));
  if (entry.accent_argb) {
    dict.Set(kAccentKey, static_cast<double>(*entry.accent_argb));
  }
  dict.Set(kStateKey, static_cast<int>(entry.state));
  return dict;
}

bool CanWrite(const PrefService* local_state) {
  return local_state && local_state->FindPreference(kIsolatedProfilesPref) &&
         !local_state->IsManagedPreference(kIsolatedProfilesPref);
}

}  // namespace

void RegisterIsolatedProfileLocalState(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kIsolatedProfilesPref);
}

std::vector<IsolatedProfileEntry> GetIsolatedProfiles(
    const PrefService* local_state) {
  std::vector<IsolatedProfileEntry> entries;
  if (!local_state || !local_state->FindPreference(kIsolatedProfilesPref)) {
    return entries;
  }
  for (const base::Value& value :
       local_state->GetList(kIsolatedProfilesPref)) {
    if (std::optional<IsolatedProfileEntry> entry = Decode(value)) {
      entries.push_back(std::move(*entry));
    }
  }
  return entries;
}

std::optional<IsolatedProfileEntry> FindIsolatedProfile(
    const PrefService* local_state,
    std::string_view profile_dir) {
  for (IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    if (entry.profile_dir == profile_dir) {
      return std::move(entry);
    }
  }
  return std::nullopt;
}

bool AddIsolatedProfile(PrefService* local_state,
                        const IsolatedProfileEntry& entry) {
  if (!CanWrite(local_state) || !IsValidProfileDir(entry.profile_dir) ||
      !entry.workspace_id.is_valid() || entry.name.empty()) {
    return false;
  }
  for (const IsolatedProfileEntry& existing :
       GetIsolatedProfiles(local_state)) {
    if (existing.profile_dir == entry.profile_dir ||
        existing.workspace_id == entry.workspace_id) {
      return false;
    }
  }
  base::ListValue list = local_state->GetList(kIsolatedProfilesPref).Clone();
  list.Append(Encode(entry));
  local_state->SetList(kIsolatedProfilesPref, std::move(list));
  return true;
}

bool SetIsolatedProfileState(PrefService* local_state,
                             std::string_view profile_dir,
                             IsolatedProfileState state) {
  if (!CanWrite(local_state)) {
    return false;
  }
  base::ListValue list = local_state->GetList(kIsolatedProfilesPref).Clone();
  for (base::Value& value : list) {
    std::optional<IsolatedProfileEntry> entry = Decode(value);
    if (entry && entry->profile_dir == profile_dir) {
      value.GetDict().Set(kStateKey, static_cast<int>(state));
      local_state->SetList(kIsolatedProfilesPref, std::move(list));
      return true;
    }
  }
  return false;
}

bool RemoveIsolatedProfile(PrefService* local_state,
                           std::string_view profile_dir) {
  if (!CanWrite(local_state)) {
    return false;
  }
  base::ListValue list = local_state->GetList(kIsolatedProfilesPref).Clone();
  const size_t removed = list.EraseIf([profile_dir](const base::Value& value) {
    const std::optional<IsolatedProfileEntry> entry = Decode(value);
    return entry && entry->profile_dir == profile_dir;
  });
  if (removed) {
    local_state->SetList(kIsolatedProfilesPref, std::move(list));
  }
  return removed > 0;
}

std::vector<std::string> RemoveIsolatedProfilesNotIn(
    PrefService* local_state,
    const std::set<std::string>& existing_profile_dirs) {
  std::vector<std::string> removed;
  if (!CanWrite(local_state)) {
    return removed;
  }
  for (const IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    if (!existing_profile_dirs.contains(entry.profile_dir)) {
      removed.push_back(entry.profile_dir);
    }
  }
  for (const std::string& dir : removed) {
    RemoveIsolatedProfile(local_state, dir);
  }
  return removed;
}

}  // namespace ahoi::session
