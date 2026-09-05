// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/shared_tab_target_policy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace ahoi::session {
namespace {

constexpr size_t kMaxSharedWebUrlBytes = 131072;
constexpr auto kLocalSchemes = std::to_array<std::string_view>(
    {"about", "chrome", "chrome-extension", "file", "blob", "data",
     "javascript", "other"});

bool IsAllowedLocalScheme(std::string_view scheme) {
  return std::ranges::contains(kLocalSchemes, scheme);
}

bool IsPortableWebUrl(std::string_view value) {
  if (value.empty() || value.size() > kMaxSharedWebUrlBytes) {
    return false;
  }
  const GURL url(value);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
         !url.has_username() && !url.has_password() && url.spec() == value;
}

}  // namespace

std::optional<SharedTabTarget> DescribeNativeSharedTabTarget(
    const GURL& native_url,
    NativeSharedTabParticipation participation) {
  if (participation == NativeSharedTabParticipation::kExcluded) {
    return std::nullopt;
  }
  if (participation == NativeSharedTabParticipation::kExplicitEmptyTemporary) {
    if (!native_url.is_empty()) {
      return std::nullopt;
    }
    return SharedTabTarget{.kind = SharedTabTargetKind::kNewTab};
  }
  if (participation != NativeSharedTabParticipation::kNormal ||
      native_url.is_empty()) {
    return std::nullopt;
  }
  if (native_url.is_valid() && IsPortableWebUrl(native_url.spec())) {
    return SharedTabTarget{.kind = SharedTabTargetKind::kWeb,
                           .url = native_url.spec()};
  }
  // Unknown, invalid, overlong or credential-bearing web targets remain local
  // too. Never publish a stripped/fake URL that changes their meaning.
  const std::string scheme = native_url.scheme();
  return SharedTabTarget{
      .kind = SharedTabTargetKind::kLocalOnly,
      .local_scheme = IsAllowedLocalScheme(scheme) ? scheme : "other"};
}

bool IsValidSharedPageTarget(const SharedTabTarget& target, bool is_temporary) {
  switch (target.kind) {
    case SharedTabTargetKind::kWeb:
      return !target.local_scheme && IsPortableWebUrl(target.url);
    case SharedTabTargetKind::kNewTab:
      return is_temporary && target.url.empty() && !target.local_scheme;
    case SharedTabTargetKind::kLocalOnly:
      return target.url.empty() && target.local_scheme &&
             IsAllowedLocalScheme(*target.local_scheme);
  }
  return false;
}

bool SharedTabPresenceMatchesPage(
    const SharedTabTarget& presence,
    const std::optional<base::Uuid>& presence_tree_node_id,
    const base::Uuid& page_id,
    const SharedTabTarget& page,
    bool page_is_temporary) {
  return page_id.is_valid() && presence_tree_node_id &&
         presence_tree_node_id->is_valid() &&
         *presence_tree_node_id == page_id &&
         IsValidSharedPageTarget(page, page_is_temporary) && presence == page;
}

SharedTabTargetAction SelectSharedTabTargetAction(
    const base::Uuid& page_id,
    const SharedTabTarget& target,
    bool is_temporary,
    const std::optional<base::Uuid>& local_runtime_tree_node_id) {
  if (!page_id.is_valid() || !IsValidSharedPageTarget(target, is_temporary)) {
    return SharedTabTargetAction::kUnavailable;
  }
  switch (target.kind) {
    case SharedTabTargetKind::kWeb:
      return SharedTabTargetAction::kWebNavigation;
    case SharedTabTargetKind::kNewTab:
      return SharedTabTargetAction::kPlatformNewTab;
    case SharedTabTargetKind::kLocalOnly:
      return local_runtime_tree_node_id &&
                     *local_runtime_tree_node_id == page_id
                 ? SharedTabTargetAction::kExistingLocalRuntime
                 : SharedTabTargetAction::kUnavailable;
  }
  return SharedTabTargetAction::kUnavailable;
}

}  // namespace ahoi::session
