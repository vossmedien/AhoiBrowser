// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi::sidebar {

namespace {

constexpr std::array<CommandItemType, 4> kSearchableCommandTypes = {
    CommandItemType::kOpenTab,
    CommandItemType::kSavedPage,
    CommandItemType::kFolder,
    CommandItemType::kWorkspace,
};

bool IsSearchableCommandType(CommandItemType type) {
  return std::ranges::find(kSearchableCommandTypes, type) !=
         kSearchableCommandTypes.end();
}

SidebarDiscoveryItemKind ToDiscoveryKind(CommandItemType type) {
  switch (type) {
    case CommandItemType::kOpenTab:
      return SidebarDiscoveryItemKind::kOpenTab;
    case CommandItemType::kSavedPage:
      return SidebarDiscoveryItemKind::kSavedPage;
    case CommandItemType::kFolder:
      return SidebarDiscoveryItemKind::kFolder;
    case CommandItemType::kWorkspace:
      return SidebarDiscoveryItemKind::kWorkspace;
    case CommandItemType::kHistory:
    case CommandItemType::kBrowserCommand:
      break;
  }
  NOTREACHED();
  return SidebarDiscoveryItemKind::kOpenTab;
}

GURL SanitizeDisplayUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return GURL();
  }
  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.ClearPassword();
  return url.ReplaceComponents(replacements);
}

std::string CommandStableId(const CommandItem& item) {
  return base::StrCat({"command:", base::NumberToString(
                                      static_cast<int>(item.type)),
                       ":", item.stable_id});
}

std::string RestoreStableId(SessionID id) {
  return base::StrCat({"restore:", base::NumberToString(id.id())});
}

const sessions::tab_restore::Tab* FirstNavigableTab(
    const std::vector<std::unique_ptr<sessions::tab_restore::Tab>>& tabs) {
  const auto it = std::ranges::find_if(tabs, [](const auto& tab) {
    return tab && !tab->navigations.empty();
  });
  return it == tabs.end() ? nullptr : it->get();
}

const sessions::tab_restore::Tab* RepresentativeTab(
    const sessions::tab_restore::Entry& entry) {
  switch (entry.type) {
    case sessions::tab_restore::TAB:
      return static_cast<const sessions::tab_restore::Tab*>(&entry);
    case sessions::tab_restore::SPLIT: {
      const auto& split =
          static_cast<const sessions::tab_restore::Split&>(entry);
      return FirstNavigableTab(split.tabs);
    }
    case sessions::tab_restore::GROUP: {
      const auto& group =
          static_cast<const sessions::tab_restore::Group&>(entry);
      return FirstNavigableTab(group.tabs);
    }
    case sessions::tab_restore::WINDOW: {
      const auto& window =
          static_cast<const sessions::tab_restore::Window&>(entry);
      if (window.tabs.empty()) {
        return nullptr;
      }
      const int selected =
          std::clamp(window.selected_tab_index, 0,
                     static_cast<int>(window.tabs.size() - 1));
      const sessions::tab_restore::Tab* const selected_tab =
          window.tabs[static_cast<size_t>(selected)].get();
      return selected_tab && !selected_tab->navigations.empty()
                 ? selected_tab
                 : FirstNavigableTab(window.tabs);
    }
  }
  return nullptr;
}

size_t EntryTabCount(const sessions::tab_restore::Entry& entry) {
  switch (entry.type) {
    case sessions::tab_restore::TAB:
      return 1u;
    case sessions::tab_restore::SPLIT:
      return static_cast<const sessions::tab_restore::Split&>(entry)
          .tabs.size();
    case sessions::tab_restore::GROUP:
      return static_cast<const sessions::tab_restore::Group&>(entry)
          .tabs.size();
    case sessions::tab_restore::WINDOW:
      return static_cast<const sessions::tab_restore::Window&>(entry)
          .tabs.size();
  }
  return 0u;
}

SidebarDiscoveryItemKind RestoreKind(
    const sessions::tab_restore::Entry& entry) {
  switch (entry.type) {
    case sessions::tab_restore::TAB:
      return SidebarDiscoveryItemKind::kRecentlyClosedTab;
    case sessions::tab_restore::SPLIT:
      return SidebarDiscoveryItemKind::kRecentlyClosedSplit;
    case sessions::tab_restore::GROUP:
      return SidebarDiscoveryItemKind::kRecentlyClosedGroup;
    case sessions::tab_restore::WINDOW:
      return SidebarDiscoveryItemKind::kRecentlyClosedWindow;
  }
  NOTREACHED();
  return SidebarDiscoveryItemKind::kRecentlyClosedTab;
}

std::u16string EntryTitle(const sessions::tab_restore::Entry& entry,
                          const sessions::tab_restore::Tab& tab,
                          const GURL& original_url,
                          const GURL& display_url) {
  if (entry.type == sessions::tab_restore::GROUP) {
    const auto& group =
        static_cast<const sessions::tab_restore::Group&>(entry);
    if (!group.visual_data.title().empty()) {
      return group.visual_data.title();
    }
  }
  if (entry.type == sessions::tab_restore::WINDOW) {
    const auto& window =
        static_cast<const sessions::tab_restore::Window&>(entry);
    if (!window.user_title.empty()) {
      return base::UTF8ToUTF16(window.user_title);
    }
  }
  if (!tab.navigations.empty()) {
    const auto& navigation =
        tab.navigations[static_cast<size_t>(tab.normalized_navigation_index())];
    if (!navigation.title().empty()) {
      if ((original_url.has_username() || original_url.has_password()) &&
          navigation.title() == base::UTF8ToUTF16(original_url.spec())) {
        return base::UTF8ToUTF16(display_url.spec());
      }
      return navigation.title();
    }
  }
  return display_url.is_empty() ? std::u16string()
                                : base::UTF8ToUTF16(display_url.spec());
}

std::optional<SidebarDiscoveryItem> MakeRecentlyClosedItem(
    const sessions::tab_restore::Entry& entry) {
  const sessions::tab_restore::Tab* const tab = RepresentativeTab(entry);
  if (!tab || tab->navigations.empty()) {
    return std::nullopt;
  }
  const auto& navigation =
      tab->navigations[static_cast<size_t>(tab->normalized_navigation_index())];
  const GURL& original_url = navigation.virtual_url();
  const GURL display_url = SanitizeDisplayUrl(original_url);
  const std::u16string title =
      EntryTitle(entry, *tab, original_url, display_url);
  const size_t tab_count = EntryTabCount(entry);
  if (title.empty() || tab_count == 0u || !entry.id.is_valid()) {
    return std::nullopt;
  }
  return SidebarDiscoveryItem{
      .kind = RestoreKind(entry),
      .stable_id = RestoreStableId(entry.id),
      .title = title,
      .secondary_text = display_url.is_empty()
                            ? std::u16string()
                            : base::UTF8ToUTF16(display_url.spec()),
      .url = display_url.is_empty() ? std::nullopt
                                    : std::optional<GURL>(display_url),
      .restore_id = entry.id,
      .tab_count = tab_count,
      .timestamp = entry.timestamp,
  };
}

}  // namespace

SidebarDiscoveryModel::SidebarDiscoveryModel(
    CommandService* command_service,
    sessions::TabRestoreService* tab_restore_service)
    : command_service_(command_service),
      tab_restore_service_(tab_restore_service) {
  CHECK(command_service_);
  command_service_->AddObserver(this);
  if (tab_restore_service_) {
    tab_restore_service_->AddObserver(this);
    tab_restore_service_->LoadTabsFromLastSession();
  }
}

SidebarDiscoveryModel::~SidebarDiscoveryModel() {
  if (tab_restore_service_) {
    tab_restore_service_->RemoveObserver(this);
  }
  if (command_service_) {
    command_service_->RemoveObserver(this);
  }
}

void SidebarDiscoveryModel::AddObserver(
    SidebarDiscoveryModelObserver* observer) {
  observers_.AddObserver(observer);
}

void SidebarDiscoveryModel::RemoveObserver(
    SidebarDiscoveryModelObserver* observer) {
  observers_.RemoveObserver(observer);
}

std::vector<SidebarDiscoveryItem> SidebarDiscoveryModel::Search(
    std::u16string_view query,
    size_t max_results) const {
  if (query.empty() || max_results == 0u || !command_service_) {
    return {};
  }

  CommandQueryOptions options{
      .deduplicate_urls = false,
      .max_results = max_results,
  };
  options.allowed_types.insert(kSearchableCommandTypes.begin(),
                               kSearchableCommandTypes.end());
  std::vector<RankedCommand> ranked = command_service_->Query(query, options);
  std::vector<SidebarDiscoveryItem> results;
  results.reserve(ranked.size());
  for (RankedCommand& result : ranked) {
    CommandItem command = std::move(result.item);
    if (!IsSearchableCommandType(command.type)) {
      continue;
    }
    const GURL display_url = command.url.has_value()
                                 ? SanitizeDisplayUrl(*command.url)
                                 : GURL();
    std::u16string title = command.title;
    if (command.url.has_value() &&
        (command.url->has_username() || command.url->has_password()) &&
        title == base::UTF8ToUTF16(command.url->spec())) {
      title = base::UTF8ToUTF16(display_url.spec());
    }
    std::u16string secondary = command.secondary_text;
    if (!display_url.is_empty() &&
        (command.type == CommandItemType::kOpenTab ||
         command.type == CommandItemType::kSavedPage)) {
      secondary = base::UTF8ToUTF16(display_url.spec());
    }
    const std::string stable_id = CommandStableId(command);
    const SidebarDiscoveryItemKind kind = ToDiscoveryKind(command.type);
    // Keep activation identity but do not retain the command index's raw URL
    // keywords (which can contain URL userinfo) in the sidebar view model.
    command.title = title;
    command.secondary_text = secondary;
    command.keywords.clear();
    command.url = display_url.is_empty() ? std::nullopt
                                         : std::optional<GURL>(display_url);
    results.push_back({
        .kind = kind,
        .stable_id = stable_id,
        .title = std::move(title),
        .secondary_text = std::move(secondary),
        .url = display_url.is_empty() ? std::nullopt
                                      : std::optional<GURL>(display_url),
        .command = std::move(command),
    });
  }
  return results;
}

std::vector<SidebarDiscoveryItem> SidebarDiscoveryModel::RecentlyClosed(
    size_t max_results) const {
  if (max_results == 0u || !tab_restore_service_) {
    return {};
  }
  std::vector<SidebarDiscoveryItem> results;
  results.reserve(
      std::min(max_results, tab_restore_service_->entries().size()));
  for (const auto& entry : tab_restore_service_->entries()) {
    if (std::optional<SidebarDiscoveryItem> item =
            MakeRecentlyClosedItem(*entry)) {
      results.push_back(std::move(*item));
      if (results.size() == max_results) {
        break;
      }
    }
  }
  return results;
}

bool SidebarDiscoveryModel::RestoreRecentlyClosed(
    SessionID entry_id,
    sessions::LiveTabContext* live_tab_context) {
  if (!entry_id.is_valid() || !tab_restore_service_ ||
      std::ranges::none_of(tab_restore_service_->entries(),
                           [entry_id](const auto& entry) {
                             return entry->id == entry_id;
                           })) {
    return false;
  }
  return !tab_restore_service_
              ->RestoreEntryById(live_tab_context, entry_id,
                                 WindowOpenDisposition::UNKNOWN)
              .empty();
}

void SidebarDiscoveryModel::OnCommandIndexChanged(CommandItemType type) {
  if (IsSearchableCommandType(type)) {
    NotifyChanged();
  }
}

void SidebarDiscoveryModel::TabRestoreServiceChanged(
    sessions::TabRestoreService* service) {
  if (service == tab_restore_service_) {
    NotifyChanged();
  }
}

void SidebarDiscoveryModel::TabRestoreServiceLoaded(
    sessions::TabRestoreService* service) {
  if (service == tab_restore_service_) {
    NotifyChanged();
  }
}

void SidebarDiscoveryModel::TabRestoreServiceDestroyed(
    sessions::TabRestoreService* service) {
  if (service != tab_restore_service_) {
    return;
  }
  tab_restore_service_ = nullptr;
  NotifyChanged();
}

void SidebarDiscoveryModel::NotifyChanged() {
  for (SidebarDiscoveryModelObserver& observer : observers_) {
    observer.OnSidebarDiscoveryModelChanged();
  }
}

}  // namespace ahoi::sidebar
