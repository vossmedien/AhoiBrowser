// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SCREENSHOT_CAPTURE_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SCREENSHOT_CAPTURE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace ahoi {

enum class DeveloperScreenshotType {
  kVisibleArea,
  kFullPage,
};

// One lightweight, per-window capture coordinator. It attaches to DevTools
// only for the duration of an explicit screenshot request and writes PNG data
// only after the user confirms a native save dialog.
class DeveloperScreenshotCapture final
    : public content::DevToolsAgentHostClient,
      public ui::SelectFileDialog::Listener {
 public:
  DeveloperScreenshotCapture();
  DeveloperScreenshotCapture(const DeveloperScreenshotCapture&) = delete;
  DeveloperScreenshotCapture& operator=(const DeveloperScreenshotCapture&) =
      delete;
  ~DeveloperScreenshotCapture() override;

  bool Capture(content::WebContents* web_contents,
               DeveloperScreenshotType type);
  bool is_busy() const { return pending_; }

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  std::string GetTypeForMetrics() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  void ResetCaptureSession();
  void ShowSaveDialog(std::vector<uint8_t> png_bytes);
  void ResetSaveDialog();

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  base::WeakPtr<content::WebContents> web_contents_;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  std::vector<uint8_t> pending_png_bytes_;
  DeveloperScreenshotType type_ = DeveloperScreenshotType::kVisibleArea;
  int request_id_ = 0;
  bool pending_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_SCREENSHOT_CAPTURE_H_
