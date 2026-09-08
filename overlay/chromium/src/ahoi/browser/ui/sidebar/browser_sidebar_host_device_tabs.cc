// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_device_tab_commands.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_sync_controls.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

std::u16string SyncText(std::u16string_view german,
                        std::u16string_view english) {
  return std::u16string(base::StartsWith(base::i18n::GetConfiguredLocale(),
                                         "de",
                                         base::CompareCase::INSENSITIVE_ASCII)
                            ? german
                            : english);
}

}  // namespace

void BrowserSidebarHostView::PublishLocalDeviceTabs() {
  if (!profile_sync_service_ || !window_id_.has_value() || !tab_strip_model_) {
    return;
  }
  auto observed = BuildSharedTabCapture(0);
  if (observed_shared_tabs_ && *observed_shared_tabs_ == observed) {
    return;
  }
  observed_shared_tabs_ = std::move(observed);
  profile_sync_service_->RequestSharedTabCapture(
      window_id_->AsLowercaseString());
}

sync::LocalTabCapture BrowserSidebarHostView::BuildSharedTabCapture(
    uint64_t generation) const {
  sync::LocalTabCapture capture{.generation = generation};
  if (!session_bridge_ || !session_bridge_->is_ready() || !tab_strip_model_ ||
      !window_id_) {
    return capture;
  }
  capture.tabs.reserve(tab_strip_model_->count());
  tabs::TabInterface* const active_tab = tab_strip_model_->GetActiveTab();
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    content::WebContents* const contents = tab ? tab->GetContents() : nullptr;
    if (!tab || !contents) {
      return capture;
    }
    const auto page_id = session_bridge_->FindSharedTreeNodeIdForTab(tab);
    const auto presence_id = session_bridge_->GetPresenceIdForTab(tab);
    tab_tree::TreeNode page;
    if (!page_id || !presence_id || *page_id == *presence_id ||
        session_bridge_->tab_tree_store()->GetNode(*page_id, &page) !=
            tab_tree::TabTreeStore::Result::kOk ||
        page.tombstone || page.type != tab_tree::TreeNodeType::kSavedPage) {
      return capture;  // No missing/filtered row can attest a complete window.
    }
    const auto target = tab_tree::GetSharedPageTarget(page);
    if (!target) {
      return capture;
    }
    // Presence describes its shared Page, not a passive stale runtime URL.
    // Actual local navigations update the Page through SessionBridge first.
    capture.tabs.push_back(sync::LocalTabState{
        .stable_key =
            "runtime:" + base::NumberToString(tab->GetHandle().raw_value()),
        .sync_id = *presence_id,
        .workspace_id = page.workspace_id,
        .url = target->url,
        .title = base::UTF16ToUTF8(tab_tree::GetSharedPageTitle(page)),
        .pinned = !page.is_temporary,
        .active = tab == active_tab,
        .tree_node_id = page.id,
        .target_kind = target->kind,
        .local_scheme = target->local_scheme});
  }
  capture.status = sync::LocalTabCaptureStatus::kComplete;
  return capture;
}

void BrowserSidebarHostView::PublishRequestedSharedTabCapture(
    uint64_t generation) {
  if (profile_sync_service_ && profile_sync_ui_attached_ && window_id_) {
    profile_sync_service_->PublishSharedTabCapture(
        window_id_->AsLowercaseString(), BuildSharedTabCapture(generation));
  }
}

void BrowserSidebarHostView::OnAhoiSharedTabSyncStateChanged(
    const sync::SharedTabSyncState&) {
  observed_shared_tabs_.reset();
  ScheduleRuntimePresentationRefresh();
}

ui::ImageModel BrowserSidebarHostView::GetSharedTabOriginIcon(
    const base::Uuid& node_id) const {
  if (!profile_sync_service_) {
    return {};
  }
  const auto origin =
      profile_sync_service_->GetSharedTabProvenance(node_id).creation_device;
  if (!origin || *origin == profile_sync_service_->local_device_id()) {
    return {};
  }
  for (const auto& device : device_tabs_snapshot_.devices) {
    if (device.id != *origin || device.tombstone) {
      continue;
    }
    const auto* icon = &vector_icons::kDevicesIcon;
    if (device.type == sync::DeviceType::kMacDesktop) {
      icon = &vector_icons::kDesktopWindowsIcon;
    } else if (device.type == sync::DeviceType::kIPhone) {
      icon = &kSmartphoneRefreshOldIcon;
    } else if (device.type == sync::DeviceType::kIPad) {
      icon = &kTabletFilledIcon;
    }
    return ui::ImageModel::FromVectorIcon(*icon, visual_style::kMutedText, 15);
  }
  return {};
}

std::u16string BrowserSidebarHostView::GetSharedTabOriginText(
    const base::Uuid& node_id) const {
  const auto temporary = SyncText(u"Temporärer Tab", u"Temporary tab");
  if (!profile_sync_service_) {
    return temporary;
  }
  const auto origin =
      profile_sync_service_->GetSharedTabProvenance(node_id).creation_device;
  if (!origin || *origin == profile_sync_service_->local_device_id()) {
    return temporary;
  }
  for (const auto& device : device_tabs_snapshot_.devices) {
    if (device.id == *origin && !device.tombstone) {
      return temporary + SyncText(u" von ", u" from ") +
             base::UTF8ToUTF16(device.display_name);
    }
  }
  return temporary;
}

void BrowserSidebarHostView::PublishDeviceTabCommands() {
  if (!command_service_) {
    return;
  }
  auto unprojected = device_tabs_snapshot_;
  std::erase_if(unprojected.remote_tabs, [this](const auto& tab) {
    return HasProjectedSharedPage(tab);
  });
  CHECK(command_service_->ReplaceItems(
      CommandItemType::kDeviceTab,
      BuildDeviceTabCommandItems(unprojected, base::Time::Now())));
}

bool BrowserSidebarHostView::HasProjectedSharedPage(
    const sync::RemoteTabRecord& tab) const {
  if (!tab.tree_node_id || !session_bridge_ || !session_bridge_->is_ready()) {
    return false;
  }
  tab_tree::TreeNode page;
  return session_bridge_->tab_tree_store()->GetNode(*tab.tree_node_id, &page) ==
             tab_tree::TabTreeStore::Result::kOk &&
         !page.tombstone && page.type == tab_tree::TreeNodeType::kSavedPage;
}

void BrowserSidebarHostView::RefreshRemoteTabPresentation() {
  if (!remote_tabs_header_ || !remote_tabs_container_) {
    return;
  }
  std::set<base::Uuid> remote_device_ids;
  for (const sync::RemoteTabRecord& tab : device_tabs_snapshot_.remote_tabs) {
    const GURL remote_url(tab.url);
    if (!HasProjectedSharedPage(tab) && !tab.tombstone &&
        remote_url.is_valid() && remote_url.SchemeIsHTTPOrHTTPS() &&
        !remote_url.has_username() && !remote_url.has_password()) {
      remote_device_ids.insert(tab.device_id);
    }
  }
  std::vector<sync::DeviceRecord> filter_devices;
  for (const sync::DeviceRecord& device : device_tabs_snapshot_.devices) {
    if (remote_device_ids.contains(device.id)) {
      filter_devices.push_back(device);
    }
  }

  views::View* controls =
      remote_tabs_container_->GetViewByID(kSidebarSyncControlsViewId);
  if (!controls) {
    controls =
        remote_tabs_container_->AddChildView(CreateSidebarSyncControlsView(
            profile_sync_service_, std::move(filter_devices),
            base::BindRepeating(
                &BrowserSidebarHostView::ScheduleRuntimePresentationRefresh,
                weak_ptr_factory_.GetWeakPtr())));
  } else {
    UpdateSidebarSyncControlsView(controls, profile_sync_service_,
                                  std::move(filter_devices));
  }
  // Search is a direct navigation mode. Device-filter controls do not affect
  // its exact matched rows and would otherwise imply a second active filter.
  controls->SetVisible(sidebar_discovery_query_.empty());
  std::vector<views::View*> previous_rows;
  for (views::View* child : remote_tabs_container_->children()) {
    if (child != controls) {
      previous_rows.push_back(child);
    }
  }
  for (views::View* row : previous_rows) {
    remote_tabs_container_->RemoveChildViewT(row);
  }

  size_t row_count = 0;
  for (const sync::RemoteTabRecord& tab : device_tabs_snapshot_.remote_tabs) {
    // Provider data is untrusted at the final presentation boundary too.
    const GURL remote_url(tab.url);
    if (HasProjectedSharedPage(tab) || tab.tombstone ||
        !remote_url.is_valid() || !remote_url.SchemeIsHTTPOrHTTPS() ||
        remote_url.has_username() || remote_url.has_password() ||
        (sidebar_discovery_query_.empty() &&
         !SidebarSyncControlsMatchesDevice(controls, tab.device_id))) {
      continue;
    }
    sync::DeviceType device_type = sync::DeviceType::kOther;
    std::u16string device_name;
    std::u16string remote_status =
        SyncText(u"Gerät nicht verfügbar", u"Device unavailable");
    bool remote_actions_available = false;
    const auto device = std::ranges::find(
        device_tabs_snapshot_.devices, tab.device_id, &sync::DeviceRecord::id);
    if (device != device_tabs_snapshot_.devices.end()) {
      device_type = device->type;
      device_name = base::UTF8ToUTF16(device->display_name);
      if (device->retired || device->tombstone) {
        remote_status =
            SyncText(u"Gerätezugriff widerrufen", u"Device access revoked");
      } else {
        const auto session =
            std::ranges::find(device_tabs_snapshot_.sessions, tab.session_id,
                              &sync::DeviceSessionRecord::id);
        if (session != device_tabs_snapshot_.sessions.end() &&
            session->device_id == tab.device_id && session->active &&
            !session->tombstone &&
            base::Time::Now() - session->last_seen <=
                sync::kRemoteSessionActionableAge) {
          remote_status = SyncText(u"Online", u"Online");
          remote_actions_available = true;
        } else {
          remote_status = SyncText(u"Offline", u"Offline");
        }
      }
    }
    std::u16string workspace_name;
    if (tab.workspace_id.has_value()) {
      const auto workspace =
          std::ranges::find(device_tabs_snapshot_.workspaces, *tab.workspace_id,
                            &sync::WorkspaceRecord::id);
      if (workspace != device_tabs_snapshot_.workspaces.end()) {
        workspace_name = base::UTF8ToUTF16(workspace->name);
      }
    }
    if (!sidebar_discovery_query_.empty() &&
        !sidebar_discovery_device_tab_ids_.contains(
            base::StrCat({tab.device_id.AsLowercaseString(), ":",
                          tab.id.AsLowercaseString()}))) {
      continue;
    }
    const base::TimeDelta elapsed =
        std::max(base::TimeDelta(), base::Time::Now() - tab.last_active);
    remote_tabs_container_->AddChildView(CreateRemoteTabRowView(
        {.tab = tab,
         .device_type = device_type,
         .device_name = std::move(device_name),
         .workspace_name = std::move(workspace_name),
         .relative_activity =
             ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_ELAPSED,
                                    ui::TimeFormat::LENGTH_SHORT, elapsed),
         .remote_status = std::move(remote_status),
         .favicon = GetFaviconForUrl(remote_url),
         .remote_actions_available = remote_actions_available},
        {.open_here = base::BindRepeating(
             [](base::WeakPtr<BrowserSidebarHostView> host,
                sync::RemoteTabRecord remote_tab) {
               if (host && host->OpenRemoteTab(std::move(remote_tab))) {
                 host->ScheduleCloseSidebarDiscoveryAfterActivation();
               }
             },
             weak_ptr_factory_.GetWeakPtr()),
         .take_over = base::BindRepeating(
             [](base::WeakPtr<BrowserSidebarHostView> host,
                sync::RemoteTabRecord remote_tab) {
               if (!host || !remote_tab.tree_node_id ||
                   !host->OpenRemoteTab(remote_tab)) {
                 return;
               }
               if (auto* tab = host->session_bridge_->FindTabByTreeNodeId(
                       *remote_tab.tree_node_id)) {
                 std::ignore = host->session_bridge_->SaveTabAtWorkspaceRoot(
                     tab->GetBrowserWindowInterface(), tab);
               }
             },
             weak_ptr_factory_.GetWeakPtr()),
         // Desktop has no Ed25519 signing identity. Companion stays issuer.
         .focus_remote = {}}));
    ++row_count;
  }
  const bool show_remote_tabs = row_count > 0u;
  remote_tabs_header_->SetVisible(show_remote_tabs);
  // A fresh profile still needs its compact Sync entry point. Already linked
  // pages live in the workspace tree, not as duplicate device-tab rows here.
  remote_tabs_container_->SetVisible(show_remote_tabs ||
                                     (controls && controls->GetVisible()));
  remote_tabs_container_->InvalidateLayout();
}

bool BrowserSidebarHostView::OpenRemoteTab(sync::RemoteTabRecord tab) {
  if (!tab.tree_node_id || !session_bridge_ || !session_bridge_->is_ready()) {
    return false;
  }
  tab_tree::TreeNode page;
  if (session_bridge_->tab_tree_store()->GetNode(*tab.tree_node_id, &page) !=
          tab_tree::TabTreeStore::Result::kOk ||
      page.tombstone) {
    return false;  // Wait for its actual Page, never copy a URL into a new ID.
  }
  return MaterializeSavedPage(page, /*require_local_model=*/false).valid;
}

void BrowserSidebarHostView::OnAhoiDeviceTabsChanged(
    const sync::DeviceTabsSnapshot& snapshot) {
  device_tabs_snapshot_ = snapshot;
  PublishDeviceTabCommands();
  ScheduleRuntimePresentationRefresh();
}

}  // namespace ahoi::sidebar
