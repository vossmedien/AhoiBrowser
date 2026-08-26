// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/command_bar/command_execution_adapter.h"
#include "ahoi/browser/command_bar/command_execution_adapter_internal.h"
#include "ahoi/browser/command_bar/quick_window.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_action_executor.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "ahoi/browser/http_auth/http_auth_management_dialog.h"
#include "ahoi/browser/http_auth/http_auth_session_controller.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/uuid.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/search_engines/search_terms_data.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi {

namespace {

class TemplateUrlSearchResolver final : public CommandBarSearchResolver {
 public:
  explicit TemplateUrlSearchResolver(Profile* profile)
      : service_(TemplateURLServiceFactory::GetForProfile(profile)) {
    if (service_ && !service_->loaded()) {
      // Loads the profile's local search-engine model. This does not start a
      // suggestion request; an unresolved model simply produces no fallback
      // until Chromium finishes loading it.
      service_->Load();
    }
  }

  std::u16string GetEngineName() const override {
    const TemplateURL* provider = GetProvider();
    return provider ? provider->short_name() : std::u16string();
  }

  std::optional<ResolvedCommandBarSearch> Resolve(
      std::u16string_view terms) const override {
    const TemplateURL* provider = GetProvider();
    if (!provider || terms.empty()) {
      return std::nullopt;
    }

    const TemplateURLRef& url_ref = provider->url_ref();
    const SearchTermsData& search_terms_data = service_->search_terms_data();
    if (!url_ref.SupportsReplacement(search_terms_data)) {
      return std::nullopt;
    }

    TemplateURLRef::PostContent post_content;
    const GURL url(url_ref.ReplaceSearchTerms(
        TemplateURLRef::SearchTermsArgs(std::u16string(terms)),
        search_terms_data, &post_content));
    if (!url.is_valid() || url.is_empty() ||
        (!post_content.second.empty() && post_content.first.empty())) {
      return std::nullopt;
    }

    return ResolvedCommandBarSearch{
        .url = url,
        .engine_name = provider->short_name(),
        .content_type = std::move(post_content.first),
        .post_data = std::move(post_content.second),
    };
  }

 private:
  const TemplateURL* GetProvider() const {
    // GetDefaultSearchProvider() deliberately exposes the pre-loading provider
    // until keyword WebData finishes loading. Gating on loaded() would make a
    // cold-start Cmd+L/Cmd+T unable to search even though Chromium already has
    // a valid configured default provider.
    return service_ ? service_->GetDefaultSearchProvider() : nullptr;
  }

  raw_ptr<TemplateURLService> service_ = nullptr;
};

class BrowserCommandExecutionDelegate final : public CommandExecutionDelegate {
 public:
  BrowserCommandExecutionDelegate(Browser* browser, views::View* sidebar_host)
      : browser_(browser), sidebar_host_(sidebar_host) {
    CHECK(browser_);
  }

  bool ActivateOpenTab(const CommandItem& item) override {
    if (!item.url.has_value()) {
      return false;
    }

    Profile* profile = browser_->profile();
    if (!profile->IsOffTheRecord()) {
      SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile);
      if (bridge) {
        tabs::TabInterface* tab =
            bridge->FindTabForOpenTabStableId(item.stable_id);
        TabStripModel* model = bridge->FindTabStripModelForTab(tab);
        if (tab && model) {
          const int index = model->GetIndexOfTab(tab);
          BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
          if (index >= 0 && window && window->GetWindow()) {
            model->ActivateTabAt(
                index, TabStripUserGestureDetails(
                           TabStripUserGestureDetails::GestureType::kKeyboard));
            window->GetWindow()->Activate();
            return true;
          }
        }
      }
    }

    TabStripModel* model = browser_->tab_strip_model();
    for (int index = 0; index < model->count(); ++index) {
      content::WebContents* contents = model->GetWebContentsAt(index);
      if (contents && (contents->GetVisibleURL() == *item.url ||
                       contents->GetLastCommittedURL() == *item.url)) {
        model->ActivateTabAt(
            index, TabStripUserGestureDetails(
                       TabStripUserGestureDetails::GestureType::kKeyboard));
        return true;
      }
    }
    return false;
  }

  bool Navigate(const GURL& url,
                std::string_view content_type,
                std::string_view post_data,
                CommandBarDisposition disposition,
                bool is_search,
                CommandBarNavigationHints navigation_hints) override {
    if (!url.is_valid() || url.is_empty()) {
      return false;
    }

    const ui::PageTransition transition =
        ui::PageTransitionFromInt((is_search ? ui::PAGE_TRANSITION_GENERATED
                                             : ui::PAGE_TRANSITION_TYPED) |
                                  ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
    NavigateParams params(browser_, url, transition);
    params.disposition = disposition == CommandBarDisposition::kCurrentTab
                             ? WindowOpenDisposition::CURRENT_TAB
                             : WindowOpenDisposition::NEW_FOREGROUND_TAB;
    // The regular omnibox populates these fields before handing the request to
    // Navigate(). The command bar has no LocationBarNavigationParams, so carry
    // the equivalent input semantics explicitly. Without the HTTP hint,
    // HTTPS-first can upgrade an explicitly typed plain-HTTP URL and leave a
    // local/external HTTP server waiting forever for a request it will never
    // receive.
    params.is_using_https_as_default_scheme =
        navigation_hints.is_using_https_as_default_scheme;
    params.url_typed_with_http_scheme =
        navigation_hints.url_typed_with_http_scheme;

    if (!post_data.empty()) {
      if (content_type.empty()) {
        return false;
      }
      params.post_data = network::ResourceRequestBody::CreateFromCopyOfBytes(
          base::as_byte_span(post_data));
      params.extra_headers = base::StringPrintf(
          "%s: %s\r\n", net::HttpRequestHeaders::kContentType,
          std::string(content_type).c_str());
    }

    const base::WeakPtr<content::NavigationHandle> navigation_handle =
        ::Navigate(&params);
    return navigation_handle || params.navigated_or_inserted_contents;
  }

  bool SwitchWorkspace(std::string_view stable_id) override {
    Profile* profile = browser_->profile();
    if (profile->IsOffTheRecord()) {
      return false;
    }
    const base::Uuid workspace_id = base::Uuid::ParseLowercase(stable_id);
    SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile);
    return workspace_id.is_valid() && bridge &&
           bridge->SetActiveWorkspaceForWindow(
               browser_, workspace_id, WorkspaceActivationSource::kKeyboard);
  }

  bool CanRevealFolder(std::string_view stable_id) const override {
    return sidebar_host_ && base::Uuid::ParseLowercase(stable_id).is_valid();
  }

  bool RevealFolder(std::string_view stable_id) override {
    const base::Uuid folder_id = base::Uuid::ParseLowercase(stable_id);
    return folder_id.is_valid() && sidebar_host_ &&
           sidebar::RevealBrowserSidebarFolder(sidebar_host_, folder_id);
  }

  bool CanExecuteBrowserCommand(std::string_view stable_id) const override {
    const std::optional<int> command_id =
        internal::GetAllowlistedBrowserCommand(stable_id);
    if (command_id == internal::kOpenPrivacyModeCommand) {
      BrowserView* const browser_view =
          BrowserView::GetBrowserViewForBrowser(browser_);
      return browser_view && browser_view->CanShowAhoiPrivacyMode() &&
             browser_view->GetLocationBarView();
    }
    if (command_id == internal::kOpenInNormalWindowCommand) {
      return quick_window::CanMoveActiveTabToNormalWindow(browser_);
    }
    if (command_id == internal::kManageHttpAuthCredentialsCommand) {
      return browser_->profile() && browser_->profile()->IsRegularProfile() &&
             browser_->tab_strip_model()->GetActiveWebContents();
    }
    if (command_id == internal::kSwitchHttpAuthAccountCommand ||
        command_id == internal::kForgetHttpAuthRealmCommand) {
      content::WebContents* const contents =
          browser_->tab_strip_model()->GetActiveWebContents();
      if (!contents || !contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
        return false;
      }
      if (command_id == internal::kSwitchHttpAuthAccountCommand) {
        return true;
      }
      if (browser_->profile()->IsOffTheRecord()) {
        return false;
      }
      HttpAuthSessionController* const controller =
          HttpAuthSessionController::GetOrCreate(contents);
      return controller && controller->active_protection_space().has_value();
    }
    return command_id.has_value() &&
           chrome::IsCommandEnabled(browser_, *command_id);
  }

  bool ExecuteBrowserCommand(std::string_view stable_id) override {
    const std::optional<int> command_id =
        internal::GetAllowlistedBrowserCommand(stable_id);
    if (command_id == internal::kOpenPrivacyModeCommand) {
      BrowserView* const browser_view =
          BrowserView::GetBrowserViewForBrowser(browser_);
      return browser_view && browser_view->ShowAhoiPrivacyMode(
                                 browser_view->GetLocationBarView());
    }
    if (command_id == internal::kOpenInNormalWindowCommand) {
      return quick_window::MoveActiveTabToNormalWindow(browser_);
    }
    if (command_id == internal::kManageHttpAuthCredentialsCommand) {
      content::WebContents* const contents =
          browser_->tab_strip_model()->GetActiveWebContents();
      return browser_->profile() && browser_->profile()->IsRegularProfile() &&
             ShowHttpAuthManagementDialog(contents);
    }
    if (command_id == internal::kSwitchHttpAuthAccountCommand ||
        command_id == internal::kForgetHttpAuthRealmCommand) {
      content::WebContents* const contents =
          browser_->tab_strip_model()->GetActiveWebContents();
      if (!contents || !contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
        return false;
      }
      HttpAuthSessionController* const controller =
          HttpAuthSessionController::GetOrCreate(contents);
      if (!controller) {
        return false;
      }
      if (command_id == internal::kSwitchHttpAuthAccountCommand) {
        controller->SwitchAccount();
        return true;
      }
      if (browser_->profile()->IsOffTheRecord() ||
          !controller->active_protection_space()) {
        return false;
      }
      controller->ForgetRealmAndSwitch();
      return true;
    }
    return command_id.has_value() &&
           chrome::IsCommandEnabled(browser_, *command_id) &&
           chrome::ExecuteCommand(browser_, *command_id);
  }

  bool CanExecuteDeveloperAction(DeveloperAction /*action*/) const override {
    return IsSupportedDeveloperTarget(
        browser_->tab_strip_model()->GetActiveWebContents());
  }

  bool ExecuteDeveloperAction(DeveloperAction action) override {
    content::WebContents* const contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    if (!IsSupportedDeveloperTarget(contents)) {
      return false;
    }
    if (!developer_action_executor_) {
      developer_action_executor_ =
          CreateChromiumDeveloperActionExecutor(contents->GetBrowserContext());
    }
    return developer_action_executor_ &&
           developer_action_executor_->Execute(contents, action).succeeded();
  }

 private:
  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::View> sidebar_host_ = nullptr;
  std::unique_ptr<DeveloperActionExecutor> developer_action_executor_;
};

}  // namespace

// static
std::unique_ptr<CommandExecutionAdapter>
CommandExecutionAdapter::CreateForBrowser(Browser* browser,
                                          CommandService* command_service,
                                          views::View* sidebar_host) {
  if (!browser || !command_service || !browser->profile()) {
    return nullptr;
  }
  Profile* profile = browser->profile();
  return std::make_unique<CommandExecutionAdapter>(
      command_service,
      std::make_unique<ChromeAutocompleteSchemeClassifier>(profile),
      std::make_unique<TemplateUrlSearchResolver>(profile),
      std::make_unique<BrowserCommandExecutionDelegate>(browser, sidebar_host));
}

}  // namespace ahoi
