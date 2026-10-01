// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <string>
#include <utility>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace ahoi {
namespace {

const char kAhoiUserAgentMarkerKey = 0;
class AhoiUserAgentMarker final : public base::SupportsUserData::Data {
 public:
  explicit AhoiUserAgentMarker(std::string value) : value(std::move(value)) {}
  const std::string value;
};

const AhoiUserAgentMarker* Marker(content::WebContents& contents) {
  return static_cast<const AhoiUserAgentMarker*>(
      contents.GetUserData(&kAhoiUserAgentMarkerKey));
}

}  // namespace

bool HasAhoiUserAgentOverride(content::WebContents& contents) {
  const auto* marker = Marker(contents);
  return marker && contents.GetUserAgentOverride().ua_string_override ==
                       marker->value;
}

void ApplyAhoiUserAgentOverride(content::WebContents& contents,
                                const DeveloperProfile* profile) {
  if (profile && profile->user_agent_enabled && !profile->user_agent.empty()) {
    contents.SetUserAgentOverride(
        blink::UserAgentOverride::UserAgentOnly(profile->user_agent), false);
    contents.SetUserData(&kAhoiUserAgentMarkerKey,
                         std::make_unique<AhoiUserAgentMarker>(profile->user_agent));
    return;
  }
  if (Marker(contents)) {
    // Another native surface may have replaced the shared WebContents value
    // after Ahoi set it. An old marker is not permission to clear that value.
    if (HasAhoiUserAgentOverride(contents)) {
      contents.SetUserAgentOverride(blink::UserAgentOverride(), false);
    }
    contents.RemoveUserData(&kAhoiUserAgentMarkerKey);
  }
}

}  // namespace ahoi
