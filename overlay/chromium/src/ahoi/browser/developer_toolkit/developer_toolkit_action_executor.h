// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_ACTION_EXECUTOR_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_ACTION_EXECUTOR_H_

#include <memory>

#include "ahoi/browser/developer_toolkit/developer_toolkit.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_chromium_adapters.h"
#include "base/memory/raw_ptr.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace ahoi {

class DeveloperScreenshotCapture;

// Dispatches one closed, user-triggered action and returns a stable status for
// UI/Command Bar callers. It owns no observers, timers, or navigation hooks.
class DeveloperActionExecutor {
 public:
  DeveloperActionExecutor(
      std::unique_ptr<BrowsingDataRemovalAdapter> data_adapter,
      std::unique_ptr<ContentSettingsAdapter> settings_adapter,
      content::BrowserContext* browser_context = nullptr);
  DeveloperActionExecutor(const DeveloperActionExecutor&) = delete;
  DeveloperActionExecutor& operator=(const DeveloperActionExecutor&) = delete;
  ~DeveloperActionExecutor();

  DeveloperActionResult Execute(content::WebContents* web_contents,
                                DeveloperAction action);
  bool ClearBrowsingData(content::WebContents* web_contents,
                         BrowsingDataClearOptions options,
                         BrowsingDataClearCallback callback);
  DeveloperActivationState GetActivationState(
      content::WebContents* web_contents) const;

 private:
  bool ResetAllPageModifications(content::WebContents* web_contents);

  DeveloperToolkit toolkit_;
  ChromiumDocumentActionExecutor document_action_executor_;
  std::unique_ptr<DeveloperScreenshotCapture> screenshot_capture_;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Convenience factory for the normal browser path. It binds both production
// adapters to the supplied regular or off-the-record BrowserContext.
std::unique_ptr<DeveloperActionExecutor> CreateChromiumDeveloperActionExecutor(
    content::BrowserContext* context);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_ACTION_EXECUTOR_H_
