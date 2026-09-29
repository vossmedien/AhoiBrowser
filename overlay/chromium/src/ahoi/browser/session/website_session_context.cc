// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/website_session_context.h"

#include <optional>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "crypto/hash.h"

namespace ahoi::session {

content::StoragePartitionConfig StoragePartitionConfigForWebsiteSession(
    Profile* profile,
    const WebsiteSessionBinding& binding) {
  CHECK(profile);
  CHECK(!profile->IsOffTheRecord());
  if (binding.is_default()) {
    return content::StoragePartitionConfig::CreateDefault(profile);
  }
  return content::StoragePartitionConfig::Create(
      profile, kWebsiteSessionPartitionDomain,
      binding.context_id.AsLowercaseString(), /*in_memory=*/false);
}

std::optional<WebsiteSessionBinding> WebsiteSessionBindingForSiteInstance(
    Profile* profile,
    content::SiteInstance* site_instance) {
  if (!profile || !site_instance ||
      site_instance->GetBrowserContext() != profile) {
    return std::nullopt;
  }
  content::StoragePartition* partition =
      profile->GetStoragePartition(site_instance);
  if (!partition) {
    return std::nullopt;
  }
  const content::StoragePartitionConfig& config = partition->GetConfig();
  if (config.is_default()) {
    return WebsiteSessionBinding();
  }
  if (config.partition_domain() != kWebsiteSessionPartitionDomain ||
      config.in_memory()) {
    return std::nullopt;
  }
  base::Uuid context_id =
      base::Uuid::ParseLowercase(config.partition_name());
  return context_id.is_valid()
             ? std::make_optional(
                   WebsiteSessionBinding{.context_id = context_id})
             : std::nullopt;
}

std::optional<WebsiteSessionBinding> WebsiteSessionBindingForWebContents(
    Profile* profile,
    content::WebContents* contents) {
  return contents && contents->GetPrimaryMainFrame()
             ? WebsiteSessionBindingForSiteInstance(
                   profile, contents->GetPrimaryMainFrame()->GetSiteInstance())
             : std::nullopt;
}

base::FilePath WebsiteSessionPartitionPath(const base::FilePath& profile_path,
                                           const WebsiteSessionBinding& binding) {
  if (binding.is_default()) {
    return base::FilePath();
  }
  const auto hash =
      crypto::hash::Sha256(binding.context_id.AsLowercaseString());
  return profile_path.AppendASCII("Storage")
      .AppendASCII("ext")
      .AppendASCII(kWebsiteSessionPartitionDomain)
      .AppendASCII(base::HexEncode(base::span(hash).first<6>()));
}

}  // namespace ahoi::session
