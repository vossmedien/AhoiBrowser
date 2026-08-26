// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_INTEGRATION_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_INTEGRATION_H_

#include <optional>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "url/gurl.h"

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace ahoi {

// Resolves an active profile once at a navigation or document-activation
// boundary. It performs no observers, timers, or background work.
std::optional<DeveloperProfile> GetDeveloperProfileForNavigation(
    const DeveloperProfileStore& store,
    const GURL& url);

// Resolves all matching source assets across their explicit tab/origin/domain
// or path scopes. The result order is stable by owner origin and asset order.
std::vector<DeveloperAsset> GetDeveloperAssetsForNavigation(
    const DeveloperProfileStore& store,
    const GURL& url,
    std::string_view current_tab_token = {});

// The embedder owns these two seams because Chromium applies request headers
// and user-agent overrides before a request, while CSS/JavaScript must run in
// the target RenderFrameHost's isolated world after commit. Implementations
// must invoke them only when the corresponding profile enabled bit is true.
// Navigation, frame replacement, and origin changes naturally invalidate any
// previously applied document override; no observer is required here.
class DeveloperProfileRequestOverrideAdapter {
 public:
  virtual ~DeveloperProfileRequestOverrideAdapter() = default;

  // Called from a URLLoader/navigation throttle before request dispatch.
  virtual bool ApplyUserAgentOverride(content::WebContents& web_contents,
                                      std::string_view user_agent) = 0;
  virtual bool ApplyHeaderRules(
      content::WebContents& web_contents,
      const std::vector<DeveloperHeaderRule>& rules) = 0;
};

class DeveloperProfileDocumentOverrideAdapter {
 public:
  virtual ~DeveloperProfileDocumentOverrideAdapter() = default;

  // Called after the target document commits. The adapter is responsible for
  // isolated-world execution and must not evaluate arbitrary helper code.
  virtual bool ApplyCss(content::RenderFrameHost& frame,
                        std::string_view css_source) = 0;
  virtual bool ApplyJavaScript(content::RenderFrameHost& frame,
                               std::string_view javascript_source) = 0;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_INTEGRATION_H_
