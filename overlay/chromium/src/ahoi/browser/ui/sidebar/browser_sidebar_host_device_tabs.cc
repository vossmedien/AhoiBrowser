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
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
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
  std::vector<sync::LocalTabState> tabs;
  tabs.reserve(tab_strip_model_->count());
  tabs::TabInterface* const active_tab = tab_strip_model_->GetActiveTab();
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    content::WebContents* const contents = tab ? tab->GetContents() : nullptr;
    if (!tab || !contents) {
      continue;
    }
    GURL url = contents->GetVisibleURL();
    if (!url.is_valid() || url.is_empty()) {
      url = contents->GetLastCommittedURL();
    }
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
        url.has_password()) {
      continue;
    }
    tabs.push_back(sync::LocalTabState{
        .stable_key = window_id_->AsLowercaseString() + ":" +
                      base::NumberToString(tab->GetHandle().raw_value()),
        .workspace_id = session_bridge_->GetWorkspaceForTab(tab),
        .url = url.spec(),
        .title = base::UTF16ToUTF8(tab->GetTitle()),
        .pinned =
            tab_strip_model_->IsTabPinned(tab_strip_model_->GetIndexOfTab(tab)),
        .active = tab == active_tab});
  }
  profile_sync_service_->PublishWindowTabs(window_id_->AsLowercaseString(),
                                           std::move(tabs));
}

void BrowserSidebarHostView::PublishDeviceTabCommands() {
  if (!command_service_) {
    return;
  }
  CHECK(command_service_->ReplaceItems(
      CommandItemType::kDeviceTab,
      BuildDeviceTabCommandItems(device_tabs_snapshot_, base::Time::Now())));
}

void BrowserSidebarHostView::RefreshRemoteTabPresentation() {
  if (!remote_tabs_header_ || !remote_tabs_container_) {
    return;
  }
  std::set<base::Uuid> remote_device_ids;
  for (const sync::RemoteTabRecord& tab : device_tabs_snapshot_.remote_tabs) {
    const GURL remote_url(tab.url);
    if (!tab.tombstone && remote_url.is_valid() &&
        remote_url.SchemeIsHTTPOrHTTPS() && !remote_url.has_username() &&
        !remote_url.has_password()) {
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
    if (tab.tombstone || !remote_url.is_valid() ||
        !remote_url.SchemeIsHTTPOrHTTPS() || remote_url.has_username() ||
        remote_url.has_password() ||
        !SidebarSyncControlsMatchesDevice(controls, tab.device_id)) {
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
        {.open_here =
             base::BindRepeating(&BrowserSidebarHostView::OpenRemoteTab,
                                 weak_ptr_factory_.GetWeakPtr()),
         .take_over = base::BindRepeating(
             [](base::WeakPtr<BrowserSidebarHostView> host,
                sync::RemoteTabRecord remote_tab) {
               if (!host) {
                 return;
               }
               const GURL url(remote_tab.url);
               if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
                   url.has_username() || url.has_password()) {
                 return;
               }
               NavigateParams params(host->browser_, url,
                                     ui::PAGE_TRANSITION_LINK);
               params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
               Navigate(&params);
               tabs::TabInterface* opened =
                   params.navigated_or_inserted_contents
                       ? host->session_bridge_->FindTabByWebContents(
                             params.navigated_or_inserted_contents)
                       : nullptr;
               if (!opened && host->tab_strip_model_ &&
                   params.navigated_or_inserted_contents) {
                 tabs::TabInterface* const active_tab =
                     host->tab_strip_model_->GetActiveTab();
                 if (active_tab && active_tab->GetContents() ==
                                       params.navigated_or_inserted_contents) {
                   opened = active_tab;
                 }
               }
               if (opened) {
                 std::ignore = host->SaveTemporaryTabAtWorkspaceRoot(
                     opened->GetHandle().raw_value(), nullptr);
               }
             },
             weak_ptr_factory_.GetWeakPtr()),
         // Desktop has no Ed25519 signing identity. Companion stays issuer.
         .focus_remote = {}}));
    ++row_count;
  }
  const bool show_remote_tabs = row_count > 0u;
  remote_tabs_header_->SetVisible(show_remote_tabs);
  // Sync controls belong to the remote-tab section and must not reserve a
  // phantom row between saved and temporary tabs when there is no remote data.
  remote_tabs_container_->SetVisible(show_remote_tabs);
  remote_tabs_container_->InvalidateLayout();
}

void BrowserSidebarHostView::OpenRemoteTab(sync::RemoteTabRecord tab) {
  const GURL url(tab.url);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
      url.has_password()) {
    return;
  }
  NavigateParams params(browser_, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void BrowserSidebarHostView::OnAhoiDeviceTabsChanged(
    const sync::DeviceTabsSnapshot& snapshot) {
  device_tabs_snapshot_ = snapshot;
  PublishDeviceTabCommands();
  ScheduleRuntimePresentationRefresh();
}

}  // namespace ahoi::sidebar
