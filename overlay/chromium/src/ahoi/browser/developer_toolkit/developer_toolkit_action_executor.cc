// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_action_executor.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_screenshot_capture.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "base/check.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ref_counted.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {
namespace {

class ClearCompletionState final
    : public base::RefCounted<ClearCompletionState> {
 public:
  ClearCompletionState(BrowsingDataClearOptions options,
                       size_t pending_tasks,
                       BrowsingDataClearCallback callback)
      : options_(options),
        pending_tasks_(pending_tasks),
        callback_(std::move(callback)) {
    CHECK_GT(pending_tasks_, 0u);
    CHECK(!callback_.is_null());
  }

  void Complete(uint32_t failed_data_type_mask) {
    CHECK_GT(pending_tasks_, 0u);
    failed_data_type_mask_ |= failed_data_type_mask & options_.data_type_mask;
    if (--pending_tasks_ != 0) {
      return;
    }
    std::move(callback_).Run(BrowsingDataClearResult{
        .options = options_,
        .failed_data_type_mask = failed_data_type_mask_,
    });
  }

 private:
  friend class base::RefCounted<ClearCompletionState>;
  ~ClearCompletionState() = default;

  const BrowsingDataClearOptions options_;
  size_t pending_tasks_;
  uint32_t failed_data_type_mask_ = 0;
  BrowsingDataClearCallback callback_;
};

}  // namespace

DeveloperActionExecutor::DeveloperActionExecutor(
    std::unique_ptr<BrowsingDataRemovalAdapter> data_adapter,
    std::unique_ptr<ContentSettingsAdapter> settings_adapter,
    content::BrowserContext* browser_context)
    : toolkit_(std::move(data_adapter), std::move(settings_adapter)),
      browser_context_(browser_context) {}

DeveloperActionExecutor::~DeveloperActionExecutor() = default;

DeveloperActionResult DeveloperActionExecutor::Execute(
    content::WebContents* web_contents,
    DeveloperAction action) {
  if (!IsSupportedDeveloperTarget(web_contents)) {
    return {action, DeveloperActionStatus::kRejectedUnsupportedTarget};
  }

  bool executed = false;
  switch (action) {
    case DeveloperAction::kClearCache:
      executed = toolkit_.ClearCache(web_contents, base::DoNothing());
      break;
    case DeveloperAction::kClearSiteData:
      executed = ClearBrowsingData(
          web_contents,
          BrowsingDataOptionsForScope(BrowsingDataScope::kFullSiteData),
          base::DoNothing());
      break;
    case DeveloperAction::kToggleCss:
      executed = document_action_executor_.Execute(web_contents,
                                                   DocumentAction::kToggleCss);
      break;
    case DeveloperAction::kTogglePasswordFields:
      executed = document_action_executor_.Execute(
          web_contents, DocumentAction::kTogglePasswordFields);
      break;
    case DeveloperAction::kToggleStructureOutlines:
      executed = document_action_executor_.Execute(
          web_contents, DocumentAction::kToggleStructureOutlines);
      break;
    case DeveloperAction::kToggleAltTitleLabels:
      executed = document_action_executor_.Execute(
          web_contents, DocumentAction::kToggleAltTitleLabels);
      break;
    case DeveloperAction::kToggleDocumentMetadata:
      executed = document_action_executor_.Execute(
          web_contents, DocumentAction::kToggleDocumentMetadata);
      break;
    case DeveloperAction::kResetDocumentModifications:
      executed = ResetAllPageModifications(web_contents);
      break;
    case DeveloperAction::kToggleJavaScript:
      executed = toolkit_.ToggleJavaScript(web_contents).has_value();
      break;
    case DeveloperAction::kToggleImages:
      executed = toolkit_.ToggleImages(web_contents).has_value();
      break;
    case DeveloperAction::kCaptureVisibleScreenshot:
      if (!screenshot_capture_) {
        screenshot_capture_ = std::make_unique<DeveloperScreenshotCapture>();
      }
      executed = screenshot_capture_->Capture(
          web_contents, DeveloperScreenshotType::kVisibleArea);
      break;
    case DeveloperAction::kCaptureFullPageScreenshot:
      if (!screenshot_capture_) {
        screenshot_capture_ = std::make_unique<DeveloperScreenshotCapture>();
      }
      executed = screenshot_capture_->Capture(
          web_contents, DeveloperScreenshotType::kFullPage);
      break;
  }

  return {action, executed ? DeveloperActionStatus::kExecuted
                           : DeveloperActionStatus::kUnavailable};
}

bool DeveloperActionExecutor::ResetAllPageModifications(
    content::WebContents* web_contents) {
  const bool document_reset = document_action_executor_.Execute(
      web_contents, DocumentAction::kResetDocumentModifications);
  const bool settings_reset = toolkit_.ResetContentSettings(web_contents);
  bool profile_reset = true;
  if (browser_context_) {
    PrefService* const prefs = user_prefs::UserPrefs::Get(browser_context_);
    const url::Origin origin =
        url::Origin::Create(web_contents->GetLastCommittedURL());
    if (!prefs || origin.opaque()) {
      profile_reset = false;
    } else {
      PrefDeveloperProfileStore store(prefs,
                                      browser_context_->IsOffTheRecord());
      if (store.Get(origin)) {
        profile_reset = store.Remove(origin);
      }
      ApplyAhoiUserAgentOverride(*web_contents, nullptr);
      ClearDeveloperProfileNavigationRequest(*web_contents);
      UpdateDeveloperProfileNetworkState(*web_contents, GURL(), std::nullopt);
      if (content::NavigationEntry* entry =
              web_contents->GetController().GetLastCommittedEntry()) {
        entry->SetIsOverridingUserAgent(false);
      }
    }
  }
  if (document_reset && settings_reset && profile_reset && browser_context_) {
    web_contents->GetController().Reload(content::ReloadType::NORMAL, true);
  }
  return document_reset && settings_reset && profile_reset;
}

bool DeveloperActionExecutor::ClearBrowsingData(
    content::WebContents* web_contents,
    BrowsingDataClearOptions options,
    BrowsingDataClearCallback callback) {
  if (!IsSupportedDeveloperTarget(web_contents) || callback.is_null() ||
      options.data_type_mask == 0 ||
      (options.data_type_mask & ~kAllDeveloperBrowsingDataTypes) != 0) {
    return false;
  }

  const bool clear_session_storage =
      options.data_type_mask & ToMask(BrowsingDataType::kSessionStorage);
  if (clear_session_storage &&
      options.target != BrowsingDataTarget::kCurrentSite) {
    // Session Storage is a live tab namespace, not a profile-wide timestamped
    // bucket. The UI must never silently reinterpret a global request as an
    // active-tab operation.
    return false;
  }

  BrowsingDataClearOptions remover_options = options;
  remover_options.data_type_mask &= ~ToMask(BrowsingDataType::kSessionStorage);
  const size_t pending_tasks = (clear_session_storage ? 1u : 0u) +
                               (remover_options.data_type_mask ? 1u : 0u);
  CHECK_GT(pending_tasks, 0u);
  auto completion = base::MakeRefCounted<ClearCompletionState>(
      options, pending_tasks, std::move(callback));

  if (clear_session_storage) {
    const bool dispatched = document_action_executor_.ExecuteAndReply(
        web_contents, DocumentAction::kClearSessionStorage,
        base::BindOnce(
            [](scoped_refptr<ClearCompletionState> state, bool succeeded) {
              state->Complete(
                  succeeded ? 0u : ToMask(BrowsingDataType::kSessionStorage));
            },
            completion));
    if (!dispatched) {
      completion->Complete(ToMask(BrowsingDataType::kSessionStorage));
    }
  }

  if (remover_options.data_type_mask != 0) {
    const uint32_t requested_remover_mask = remover_options.data_type_mask;
    const bool accepted = toolkit_.ClearBrowsingData(
        web_contents, remover_options,
        base::BindOnce(
            [](scoped_refptr<ClearCompletionState> state,
               BrowsingDataClearResult result) {
              state->Complete(result.failed_data_type_mask);
            },
            completion));
    if (!accepted) {
      completion->Complete(requested_remover_mask);
    }
  }
  return true;
}

DeveloperActivationState DeveloperActionExecutor::GetActivationState(
    content::WebContents* web_contents) const {
  DeveloperActivationState state;
  state.Set(DeveloperActivation::kJavaScript,
            toolkit_.GetContentSetting(web_contents,
                                       ContentSettingType::kJavaScript) ==
                ContentSettingValue::kBlock);
  state.Set(
      DeveloperActivation::kImages,
      toolkit_.GetContentSetting(web_contents, ContentSettingType::kImages) ==
          ContentSettingValue::kBlock);
  return state;
}

std::unique_ptr<DeveloperActionExecutor> CreateChromiumDeveloperActionExecutor(
    content::BrowserContext* context) {
  return std::make_unique<DeveloperActionExecutor>(
      CreateChromiumBrowsingDataRemovalAdapter(context),
      CreateChromiumContentSettingsAdapter(context), context);
}

}  // namespace ahoi
