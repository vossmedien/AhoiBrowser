// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_screenshot_capture.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

std::string FileStemForUrl(const GURL& url) {
  std::string stem(url.host());
  if (stem.empty()) {
    stem = "page";
  }
  std::replace_if(
      stem.begin(), stem.end(),
      [](unsigned char value) {
        return !base::IsAsciiAlphaNumeric(value) && value != '-' &&
               value != '_';
      },
      '-');
  constexpr size_t kMaximumStemLength = 80;
  stem.resize(std::min(stem.size(), kMaximumStemLength));
  return stem;
}

base::FilePath SuggestedPath(const GURL& url, DeveloperScreenshotType type) {
  return base::FilePath::FromUTF8Unsafe(
      FileStemForUrl(url) + (type == DeveloperScreenshotType::kFullPage
                                 ? "-full-page.png"
                                 : "-visible.png"));
}

void WritePng(base::FilePath path, std::vector<uint8_t> bytes) {
  if (!path.empty() && !bytes.empty()) {
    base::WriteFile(path, bytes);
  }
}

}  // namespace

DeveloperScreenshotCapture::DeveloperScreenshotCapture() = default;

DeveloperScreenshotCapture::~DeveloperScreenshotCapture() {
  ResetCaptureSession();
  ResetSaveDialog();
}

bool DeveloperScreenshotCapture::Capture(content::WebContents* web_contents,
                                         DeveloperScreenshotType type) {
  if (pending_ || !web_contents || !web_contents->GetNativeView() ||
      !web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  Profile* const profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile || profile->GetPrefs()->GetBoolean(prefs::kDisableScreenshots)) {
    return false;
  }

  scoped_refptr<content::DevToolsAgentHost> host =
      content::DevToolsAgentHost::GetOrCreateFor(web_contents);
  if (!host || !host->AttachClient(this)) {
    return false;
  }

  agent_host_ = std::move(host);
  web_contents_ = web_contents->GetWeakPtr();
  type_ = type;
  pending_ = true;
  ++request_id_;

  base::DictValue params;
  params.Set("format", "png");
  params.Set("fromSurface", true);
  params.Set("captureBeyondViewport",
             type == DeveloperScreenshotType::kFullPage);
  params.Set("optimizeForSpeed", true);
  base::DictValue command;
  command.Set("id", request_id_);
  command.Set("method", "Page.captureScreenshot");
  command.Set("params", std::move(params));
  std::string message;
  if (!base::JSONWriter::Write(command, &message)) {
    ResetCaptureSession();
    return false;
  }
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(message));
  return true;
}

void DeveloperScreenshotCapture::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  if (!pending_ || agent_host != agent_host_.get()) {
    return;
  }
  std::optional<base::Value> response = base::JSONReader::Read(
      base::as_string_view(message), base::JSON_REPLACE_INVALID_CHARACTERS);
  if (!response || !response->is_dict() ||
      response->GetDict().FindInt("id") != request_id_) {
    return;
  }

  const base::DictValue* result = response->GetDict().FindDict("result");
  const std::string* encoded = result ? result->FindString("data") : nullptr;
  std::optional<std::vector<uint8_t>> png_bytes =
      encoded ? base::Base64Decode(*encoded) : std::nullopt;
  ResetCaptureSession();
  if (!png_bytes || png_bytes->empty() || !web_contents_) {
    pending_ = false;
    return;
  }
  ShowSaveDialog(std::move(*png_bytes));
}

void DeveloperScreenshotCapture::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  if (agent_host == agent_host_.get()) {
    agent_host_.reset();
    pending_ = false;
  }
}

std::string DeveloperScreenshotCapture::GetTypeForMetrics() {
  return "Other";
}

void DeveloperScreenshotCapture::FileSelected(const ui::SelectedFileInfo& file,
                                              int /*index*/) {
  std::vector<uint8_t> bytes = std::move(pending_png_bytes_);
  const base::FilePath path = file.path();
  ResetSaveDialog();
  pending_ = false;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WritePng, path, std::move(bytes)));
}

void DeveloperScreenshotCapture::FileSelectionCanceled() {
  pending_png_bytes_.clear();
  ResetSaveDialog();
  pending_ = false;
}

void DeveloperScreenshotCapture::ResetCaptureSession() {
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_.reset();
  }
}

void DeveloperScreenshotCapture::ShowSaveDialog(
    std::vector<uint8_t> png_bytes) {
  if (!web_contents_ || !web_contents_->GetNativeView()) {
    pending_ = false;
    return;
  }
  pending_png_bytes_ = std::move(png_bytes);
  select_file_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(web_contents_.get()));
  if (!select_file_dialog_) {
    pending_png_bytes_.clear();
    pending_ = false;
    return;
  }

  ui::SelectFileDialog::FileTypeInfo file_types;
  file_types.extensions = {{FILE_PATH_LITERAL("png")}};
  file_types.include_all_files = false;
  file_types.allowed_paths = ui::SelectFileDialog::FileTypeInfo::NATIVE_PATH;
  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(),
      SuggestedPath(web_contents_->GetLastCommittedURL(), type_), &file_types,
      0, FILE_PATH_LITERAL("png"), web_contents_->GetTopLevelNativeWindow(),
      nullptr);
}

void DeveloperScreenshotCapture::ResetSaveDialog() {
  if (select_file_dialog_) {
    select_file_dialog_->ListenerDestroyed();
    select_file_dialog_.reset();
  }
}

}  // namespace ahoi
