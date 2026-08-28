// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_device_tab_commands.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_policy.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::sidebar {
namespace {

std::string DeviceTabStableId(const sync::RemoteTabRecord& tab) {
  return base::StrCat(
      {tab.device_id.AsLowercaseString(), ":", tab.id.AsLowercaseString()});
}

bool ParseDeviceTabStableId(std::string_view stable_id,
                            base::Uuid* device_id,
                            base::Uuid* tab_id) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      stable_id, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() != 2u || parts[0].empty() || parts[1].empty()) {
    return false;
  }
  const base::Uuid parsed_device = base::Uuid::ParseLowercase(parts[0]);
  const base::Uuid parsed_tab = base::Uuid::ParseLowercase(parts[1]);
  if (!parsed_device.is_valid() || !parsed_tab.is_valid()) {
    return false;
  }
  *device_id = parsed_device;
  *tab_id = parsed_tab;
  return true;
}

const sync::DeviceRecord* FindEligibleDevice(
    const sync::DeviceTabsSnapshot& snapshot,
    const base::Uuid& device_id) {
  const auto device =
      std::ranges::find(snapshot.devices, device_id, &sync::DeviceRecord::id);
  return device != snapshot.devices.end() && !device->retired &&
                 !device->tombstone
             ? &*device
             : nullptr;
}

const sync::DeviceSessionRecord* FindEligibleSession(
    const sync::DeviceTabsSnapshot& snapshot,
    const sync::RemoteTabRecord& tab,
    base::Time now) {
  const auto session = std::ranges::find(snapshot.sessions, tab.session_id,
                                         &sync::DeviceSessionRecord::id);
  if (session == snapshot.sessions.end() ||
      session->device_id != tab.device_id || !session->active ||
      session->tombstone || now.is_null() || session->last_seen.is_null() ||
      session->last_seen < now - sync::kRemoteSessionVisibleAge) {
    return nullptr;
  }
  return &*session;
}

bool IsEligibleRemoteTab(const sync::DeviceTabsSnapshot& snapshot,
                         const sync::RemoteTabRecord& tab,
                         base::Time now) {
  const GURL url(tab.url);
  return tab.id.is_valid() && tab.device_id.is_valid() &&
         tab.session_id.is_valid() && !tab.is_incognito && !tab.tombstone &&
         url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password() && FindEligibleDevice(snapshot, tab.device_id) &&
         FindEligibleSession(snapshot, tab, now);
}

std::u16string TrimmedUtf16(std::string_view value) {
  std::u16string text = base::UTF8ToUTF16(value);
  base::TrimWhitespace(text, base::TrimPositions::TRIM_ALL, &text);
  return text;
}

std::u16string WorkspaceName(const sync::DeviceTabsSnapshot& snapshot,
                             const sync::RemoteTabRecord& tab) {
  if (!tab.workspace_id.has_value()) {
    return {};
  }
  const auto workspace = std::ranges::find(
      snapshot.workspaces, *tab.workspace_id, &sync::WorkspaceRecord::id);
  return workspace == snapshot.workspaces.end() || workspace->tombstone
             ? std::u16string()
             : TrimmedUtf16(workspace->name);
}

}  // namespace

std::vector<CommandItem> BuildDeviceTabCommandItems(
    const sync::DeviceTabsSnapshot& snapshot,
    base::Time now) {
  std::vector<CommandItem> items;
  items.reserve(snapshot.remote_tabs.size());
  std::set<std::string> stable_ids;
  std::set<std::string> tab_ids;
  for (const sync::RemoteTabRecord& tab : snapshot.remote_tabs) {
    if (!IsEligibleRemoteTab(snapshot, tab, now)) {
      continue;
    }
    const std::string stable_id = DeviceTabStableId(tab);
    // Conflicting provider identities invalidate the complete projection. An
    // earlier arbitrary row must never win merely because it arrived first.
    if (!stable_ids.insert(stable_id).second ||
        !tab_ids.insert(tab.id.AsLowercaseString()).second) {
      return {};
    }
    const sync::DeviceRecord* const device =
        FindEligibleDevice(snapshot, tab.device_id);
    const GURL url(tab.url);
    std::vector<std::u16string> origin_parts;
    const std::u16string device_name = TrimmedUtf16(device->display_name);
    if (!device_name.empty()) {
      origin_parts.push_back(device_name);
    }
    const std::u16string workspace_name = WorkspaceName(snapshot, tab);
    if (!workspace_name.empty()) {
      origin_parts.push_back(workspace_name);
    }
    origin_parts.push_back(base::UTF8ToUTF16(url.spec()));
    std::u16string title = TrimmedUtf16(tab.title);
    if (title.empty()) {
      title = base::UTF8ToUTF16(url.spec());
    }
    items.push_back({
        .type = CommandItemType::kDeviceTab,
        .stable_id = stable_id,
        .title = title,
        .secondary_text = base::JoinString(origin_parts, u"  ·  "),
        .keywords = origin_parts,
        .url = url,
        .priority = 180,
        .last_used = tab.last_active,
    });
  }
  return items;
}

const sync::RemoteTabRecord* ResolveDeviceTabCommand(
    const sync::DeviceTabsSnapshot& snapshot,
    std::string_view stable_id,
    base::Time now) {
  base::Uuid device_id;
  base::Uuid tab_id;
  if (!ParseDeviceTabStableId(stable_id, &device_id, &tab_id)) {
    return nullptr;
  }
  const auto tab = std::ranges::find(snapshot.remote_tabs, tab_id,
                                     &sync::RemoteTabRecord::id);
  return std::ranges::count(snapshot.remote_tabs, tab_id,
                            &sync::RemoteTabRecord::id) == 1 &&
                 tab != snapshot.remote_tabs.end() &&
                 tab->device_id == device_id &&
                 IsEligibleRemoteTab(snapshot, *tab, now)
             ? &*tab
             : nullptr;
}

}  // namespace ahoi::sidebar
