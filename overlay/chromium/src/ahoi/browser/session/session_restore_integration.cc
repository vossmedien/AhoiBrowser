// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_restore_integration.h"
#include "ahoi/browser/session/website_session_context.h"

#include <optional>
#include <string>

#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"

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

std::optional<WebsiteSessionBinding> ResolveWebsiteSessionBindingForNewTab(
    BrowserWindowInterface* browser,
    content::SiteInstance* initiating_site_instance) {
  if (!browser || !browser->GetProfile()) {
    return std::nullopt;
  }
  Profile* profile = browser->GetProfile();
  if (profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return WebsiteSessionBinding();
  }
  if (initiating_site_instance &&
      initiating_site_instance->GetBrowserContext() == profile) {
    const std::optional<WebsiteSessionBinding> inherited =
        WebsiteSessionBindingForSiteInstance(profile,
                                             initiating_site_instance);
    content::StoragePartition* partition =
        profile->GetStoragePartition(initiating_site_instance);
    if (!inherited &&
        (!partition || partition->GetConfig().partition_domain() ==
                           kWebsiteSessionPartitionDomain)) {
      return std::nullopt;
    }
    // App/extension-owned partitions are not Ahoi website sessions; their
    // native special-page rules continue to control the target.
    return inherited.value_or(WebsiteSessionBinding());
  }
  WorkspaceSessionMetadataProvider* provider =
      GetProviderForSessionBrowser(browser);
  return provider ? provider->GetWebsiteSessionBindingForWindow(browser)
                  : std::make_optional(WebsiteSessionBinding());
}

std::optional<WebsiteSessionBinding> ReadRestoredWebsiteSessionBinding(
    Profile* profile,
    const std::map<std::string, std::string>& extra_data) {
  const auto serialized = extra_data.find(kTabSessionMetadataExtraDataKey);
  if (serialized == extra_data.end()) {
    return WebsiteSessionBinding();
  }
  TabSessionMetadata metadata;
  if (DecodeTabSessionMetadata(serialized->second, &metadata) !=
      SessionMetadataDecodeResult::kSuccess) {
    return std::nullopt;
  }
  WebsiteSessionBinding binding{
      .context_id = metadata.website_session_context_id.value_or(base::Uuid())};
  return IsKnownWebsiteSessionBinding(profile ? profile->GetPrefs() : nullptr,
                                      binding)
             ? std::make_optional(binding)
             : std::nullopt;
}

}  // namespace ahoi::session
