// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_execution_adapter.h"

#include <optional>
#include <utility>

#include "ahoi/browser/command_bar/command_execution_adapter_internal.h"
#include "base/check.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/app/chrome_command_ids.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_scheme_classifier.h"
#include "url/url_constants.h"

namespace ahoi {

namespace internal {

std::optional<int> GetAllowlistedBrowserCommand(std::string_view stable_id) {
  if (stable_id == "browser.back") {
    return IDC_BACK;
  }
  if (stable_id == "browser.forward") {
    return IDC_FORWARD;
  }
  if (stable_id == "browser.reload") {
    return IDC_RELOAD;
  }
  if (stable_id == "browser.reload-bypassing-cache") {
    return IDC_RELOAD_BYPASSING_CACHE;
  }
  if (stable_id == "browser.stop") {
    return IDC_STOP;
  }
  if (stable_id == "browser.home") {
    return IDC_HOME;
  }
  if (stable_id == "browser.downloads") {
    return IDC_SHOW_DOWNLOADS;
  }
  if (stable_id == "browser.history") {
    return IDC_SHOW_HISTORY;
  }
  if (stable_id == "browser.devtools") {
    return IDC_DEV_TOOLS;
  }
  if (stable_id == "browser.settings") {
    return IDC_OPTIONS;
  }
  if (stable_id == "browser.clear-browsing-data") {
    return IDC_CLEAR_BROWSING_DATA;
  }
  if (stable_id == "browser.view-source") {
    return IDC_VIEW_SOURCE;
  }
  if (stable_id == "browser.print") {
    return IDC_PRINT;
  }
  if (stable_id == "browser.new-window") {
    return IDC_NEW_WINDOW;
  }
  if (stable_id == "browser.new-incognito-window") {
    return IDC_NEW_INCOGNITO_WINDOW;
  }
  if (stable_id == "browser.open-in-normal-window") {
    return kOpenInNormalWindowCommand;
  }
  if (stable_id == "privacy.open") {
    return kOpenPrivacyModeCommand;
  }
  if (stable_id == "http-auth.switch") {
    return kSwitchHttpAuthAccountCommand;
  }
  if (stable_id == "http-auth.forget") {
    return kForgetHttpAuthRealmCommand;
  }
  if (stable_id == "http-auth.manage") {
    return kManageHttpAuthCredentialsCommand;
  }
  return std::nullopt;
}

std::optional<DeveloperAction> GetAllowlistedDeveloperAction(
    std::string_view stable_id) {
  if (stable_id == "developer.clear-site-cache") {
    return DeveloperAction::kClearCache;
  }
  if (stable_id == "developer.toggle-css") {
    return DeveloperAction::kToggleCss;
  }
  if (stable_id == "developer.reveal-passwords") {
    return DeveloperAction::kTogglePasswordFields;
  }
  if (stable_id == "developer.reset-page") {
    return DeveloperAction::kResetDocumentModifications;
  }
  if (stable_id == "developer.toggle-javascript") {
    return DeveloperAction::kToggleJavaScript;
  }
  if (stable_id == "developer.toggle-images") {
    return DeveloperAction::kToggleImages;
  }
  if (stable_id == "developer.screenshot-visible") {
    return DeveloperAction::kCaptureVisibleScreenshot;
  }
  if (stable_id == "developer.screenshot-full-page") {
    return DeveloperAction::kCaptureFullPageScreenshot;
  }
  return std::nullopt;
}

}  // namespace internal

CommandExecutionAdapter::CommandExecutionAdapter(
    CommandService* command_service,
    std::unique_ptr<AutocompleteSchemeClassifier> scheme_classifier,
    std::unique_ptr<CommandBarSearchResolver> search_resolver,
    std::unique_ptr<CommandExecutionDelegate> execution_delegate)
    : command_service_(command_service),
      scheme_classifier_(std::move(scheme_classifier)),
      search_resolver_(std::move(search_resolver)),
      execution_delegate_(std::move(execution_delegate)) {
  CHECK(command_service_);
  CHECK(scheme_classifier_);
  CHECK(search_resolver_);
  CHECK(execution_delegate_);
}

CommandExecutionAdapter::~CommandExecutionAdapter() = default;

std::u16string CommandExecutionAdapter::GetDefaultSearchEngineName() const {
  return search_resolver_->GetEngineName();
}

std::optional<CommandBarSuggestion> CommandExecutionAdapter::PreviewInput(
    std::u16string_view input) const {
  const ParsedCommandInput parsed =
      command_service_->ParseInput(input, *scheme_classifier_);
  if (parsed.kind == CommandInputKind::kEmpty ||
      parsed.kind == CommandInputKind::kLocalOnly) {
    return std::nullopt;
  }

  CommandBarSuggestion suggestion;
  suggestion.kind = CommandBarSuggestionKind::kInputFallback;
  suggestion.title = parsed.text;
  if (parsed.kind == CommandInputKind::kUrl) {
    suggestion.secondary_text = base::UTF8ToUTF16(parsed.url.spec());
    suggestion.destination_url = parsed.url;
    return suggestion;
  }

  const std::optional<ResolvedCommandBarSearch> search =
      search_resolver_->Resolve(parsed.text);
  if (!search.has_value()) {
    return std::nullopt;
  }
  suggestion.secondary_text = search->engine_name;
  return suggestion;
}

bool CommandExecutionAdapter::CanExecuteItem(const CommandItem& item) const {
  switch (item.type) {
    case CommandItemType::kOpenTab:
    case CommandItemType::kSavedPage:
    case CommandItemType::kHistory:
      return item.url.has_value() && item.url->is_valid() &&
             !item.url->is_empty();
    case CommandItemType::kWorkspace:
      return base::Uuid::ParseLowercase(item.stable_id).is_valid();
    case CommandItemType::kFolder:
      return base::Uuid::ParseLowercase(item.stable_id).is_valid() &&
             execution_delegate_->CanRevealFolder(item.stable_id);
    case CommandItemType::kBrowserCommand:
      if (const auto action =
              internal::GetAllowlistedDeveloperAction(item.stable_id)) {
        return execution_delegate_->CanExecuteDeveloperAction(*action);
      }
      return internal::GetAllowlistedBrowserCommand(item.stable_id)
                 .has_value() &&
             execution_delegate_->CanExecuteBrowserCommand(item.stable_id);
    case CommandItemType::kDeviceTab:
      // Device tabs require fresh snapshot revalidation in the native sidebar.
      return false;
  }
  return false;
}

bool CommandExecutionAdapter::ExecuteInput(std::u16string_view input,
                                           CommandBarDisposition disposition) {
  const ParsedCommandInput parsed =
      command_service_->ParseInput(input, *scheme_classifier_);
  if (parsed.kind == CommandInputKind::kUrl) {
    const CommandBarNavigationHints navigation_hints{
        .is_using_https_as_default_scheme =
            !AutocompleteInput::HasHTTPScheme(std::u16string(input)) &&
            !AutocompleteInput::HasHTTPSScheme(std::u16string(input)) &&
            parsed.url.SchemeIs(url::kHttpsScheme),
        .url_typed_with_http_scheme =
            AutocompleteInput::HasHTTPScheme(std::u16string(input)),
    };
    return execution_delegate_->Navigate(parsed.url, /*content_type=*/{},
                                         /*post_data=*/{}, disposition,
                                         /*is_search=*/false, navigation_hints);
  }
  if (parsed.kind == CommandInputKind::kEmpty) {
    return false;
  }
  if (parsed.kind == CommandInputKind::kLocalOnly) {
    return false;
  }

  const std::optional<ResolvedCommandBarSearch> search =
      search_resolver_->Resolve(parsed.text);
  return search.has_value() &&
         execution_delegate_->Navigate(search->url, search->content_type,
                                       search->post_data, disposition,
                                       /*is_search=*/true);
}

bool CommandExecutionAdapter::ExecuteItem(const CommandItem& item,
                                          CommandBarDisposition disposition) {
  if (!CanExecuteItem(item)) {
    return false;
  }

  switch (item.type) {
    case CommandItemType::kOpenTab:
      return execution_delegate_->ActivateOpenTab(item) ||
             execution_delegate_->Navigate(*item.url, /*content_type=*/{},
                                           /*post_data=*/{}, disposition,
                                           /*is_search=*/false);
    case CommandItemType::kSavedPage:
    case CommandItemType::kHistory:
      return execution_delegate_->Navigate(*item.url, /*content_type=*/{},
                                           /*post_data=*/{}, disposition,
                                           /*is_search=*/false);
    case CommandItemType::kWorkspace:
      return execution_delegate_->SwitchWorkspace(item.stable_id);
    case CommandItemType::kFolder:
      return execution_delegate_->RevealFolder(item.stable_id);
    case CommandItemType::kBrowserCommand:
      if (const auto action =
              internal::GetAllowlistedDeveloperAction(item.stable_id)) {
        return execution_delegate_->ExecuteDeveloperAction(*action);
      }
      return execution_delegate_->ExecuteBrowserCommand(item.stable_id);
    case CommandItemType::kDeviceTab:
      return false;
  }
  return false;
}

}  // namespace ahoi
