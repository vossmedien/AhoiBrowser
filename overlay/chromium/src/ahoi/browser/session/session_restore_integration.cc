// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_restore_integration.h"

#include <optional>
#include <string>

#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::session {

namespace {

using ProviderMap =
    std::map<Profile*, raw_ptr<WorkspaceSessionMetadataProvider>>;

ProviderMap& GetProviderMap() {
  static base::NoDestructor<ProviderMap> providers;
  return *providers;
}

WorkspaceSessionMetadataProvider* GetProviderForSessionBrowser(
    BrowserWindowInterface* browser,
    const tabs::TabInterface* tab = nullptr) {
  if (!browser || browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return nullptr;
  }
  Profile* profile = browser->GetProfile();
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      !profile->AllowsBrowserWindows()) {
    return nullptr;
  }
  if (tab && (tab->GetBrowserWindowInterface() != browser ||
              tab->GetProfile() != profile)) {
    return nullptr;
  }
  auto provider = GetProviderMap().find(profile);
  return provider == GetProviderMap().end() ? nullptr : provider->second.get();
}

}  // namespace

bool RegisterWorkspaceSessionMetadataProvider(
    Profile* profile,
    WorkspaceSessionMetadataProvider* provider) {
  if (!profile || !provider || profile->IsOffTheRecord() ||
      !profile->IsRegularProfile() || !profile->AllowsBrowserWindows()) {
    return false;
  }
  return GetProviderMap().try_emplace(profile, provider).second;
}

void UnregisterWorkspaceSessionMetadataProvider(
    Profile* profile,
    WorkspaceSessionMetadataProvider* provider) {
  auto registered = GetProviderMap().find(profile);
  if (registered != GetProviderMap().end() &&
      registered->second.get() == provider) {
    GetProviderMap().erase(registered);
  }
}

bool PopulateWindowSessionExtraData(
    BrowserWindowInterface* browser,
    std::map<std::string, std::string>* extra_data) {
  if (!extra_data) {
    return false;
  }
  WorkspaceSessionMetadataProvider* provider =
      GetProviderForSessionBrowser(browser);
  if (!provider) {
    return false;
  }
  const std::optional<WindowSessionMetadata> metadata =
      provider->GetWindowSessionMetadata(browser);
  if (!metadata.has_value()) {
    return false;
  }
  const std::optional<std::string> serialized =
      EncodeWindowSessionMetadata(*metadata);
  if (!serialized.has_value()) {
    return false;
  }
  extra_data->insert_or_assign(kWindowSessionMetadataExtraDataKey, *serialized);
  return true;
}

bool PopulateTabSessionExtraData(
    BrowserWindowInterface* browser,
    tabs::TabInterface* tab,
    std::map<std::string, std::string>* extra_data) {
  if (!extra_data) {
    return false;
  }
  WorkspaceSessionMetadataProvider* provider =
      GetProviderForSessionBrowser(browser, tab);
  if (!provider) {
    return false;
  }
  const std::optional<TabSessionMetadata> metadata =
      provider->GetTabSessionMetadata(tab);
  if (!metadata.has_value()) {
    return false;
  }
  const std::optional<std::string> serialized =
      EncodeTabSessionMetadata(*metadata);
  if (!serialized.has_value()) {
    return false;
  }
  extra_data->insert_or_assign(kTabSessionMetadataExtraDataKey, *serialized);
  return true;
}

bool RestoreWindowSessionExtraData(
    BrowserWindowInterface* browser,
    const std::map<std::string, std::string>& extra_data) {
  const auto serialized = extra_data.find(kWindowSessionMetadataExtraDataKey);
  if (serialized == extra_data.end()) {
    return false;
  }
  WindowSessionMetadata metadata;
  if (DecodeWindowSessionMetadata(serialized->second, &metadata) !=
      SessionMetadataDecodeResult::kSuccess) {
    return false;
  }
  WorkspaceSessionMetadataProvider* provider =
      GetProviderForSessionBrowser(browser);
  return provider && provider->RestoreWindowSessionMetadata(browser, metadata);
}

bool RestoreTabSessionExtraData(
    BrowserWindowInterface* browser,
    tabs::TabInterface* tab,
    const std::map<std::string, std::string>& extra_data) {
  const auto serialized = extra_data.find(kTabSessionMetadataExtraDataKey);
  if (serialized == extra_data.end()) {
    return false;
  }
  TabSessionMetadata metadata;
  if (DecodeTabSessionMetadata(serialized->second, &metadata) !=
      SessionMetadataDecodeResult::kSuccess) {
    return false;
  }
  WorkspaceSessionMetadataProvider* provider =
      GetProviderForSessionBrowser(browser, tab);
  return provider && provider->RestoreTabSessionMetadata(tab, metadata);
}

}  // namespace ahoi::session
