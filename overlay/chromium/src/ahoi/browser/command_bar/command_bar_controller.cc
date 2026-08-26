// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_controller.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/command_bar/command_bar_view.h"
#include "ahoi/browser/command_bar/command_execution_adapter.h"
#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/command_service_factory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/url_constants.h"

namespace ahoi {

namespace {

constexpr size_t kMaximumSuggestions = 5u;
constexpr size_t kMaximumIndexedCandidates = 64u;
constexpr base::TimeDelta kHistoryRefreshInterval = base::Seconds(30);

const gfx::VectorIcon* GetCommandIcon(std::string_view stable_id) {
  if (stable_id == "browser.downloads") {
    return &vector_icons::kDownloadIcon;
  }
  if (stable_id == "browser.history") {
    return &vector_icons::kHistoryIcon;
  }
  if (stable_id == "browser.settings") {
    return &vector_icons::kSettingsIcon;
  }
  if (stable_id == "browser.reload" ||
      stable_id == "browser.reload-bypassing-cache" ||
      stable_id == "developer.clear-site-cache" ||
      stable_id == "developer.reset-page") {
    return &vector_icons::kRefreshIcon;
  }
  if (stable_id == "developer.reveal-passwords") {
    return &vector_icons::kPasswordManagerIcon;
  }
  if (stable_id.starts_with("http-auth.")) {
    return &vector_icons::kPasswordManagerIcon;
  }
  if (stable_id == "developer.toggle-images" ||
      stable_id == "developer.screenshot-visible" ||
      stable_id == "developer.screenshot-full-page") {
    return &vector_icons::kPhotoIcon;
  }
  if (stable_id.starts_with("developer.") || stable_id == "browser.devtools" ||
      stable_id == "browser.view-source") {
    return &vector_icons::kCodeIcon;
  }
  return nullptr;
}

}  // namespace

CommandBarController::CommandBarController(
    Browser* browser,
    ModalOverlayController* modal_overlay_controller,
    views::View* sidebar_host)
    : browser_(browser), modal_overlay_controller_(modal_overlay_controller) {
  CHECK(browser_);
  CHECK(modal_overlay_controller_);

  Profile* profile = browser_->GetProfile();
  if (!profile) {
    return;
  }
  if (profile->IsOffTheRecord()) {
    // Strict OTR boundary: this parser/index exists only for this controller,
    // remains empty, and is destroyed with the private window.
    ephemeral_command_service_ = std::make_unique<CommandService>();
    command_service_ = ephemeral_command_service_.get();
  } else if (profile->IsRegularProfile()) {
    command_service_ = CommandServiceFactory::GetForProfile(profile);
    history_service_ = HistoryServiceFactory::GetForProfile(
        profile, ServiceAccessType::EXPLICIT_ACCESS);
    favicon_service_ = FaviconServiceFactory::GetForProfile(
        profile, ServiceAccessType::EXPLICIT_ACCESS);
  }

  if (command_service_) {
    PublishBrowserCommands();
    RefreshHistoryItems();
    execution_adapter_ = CommandExecutionAdapter::CreateForBrowser(
        browser_, command_service_, sidebar_host);
  }
}

CommandBarController::~CommandBarController() {
  // Widget teardown destroys the content view. Keep the separate delegate
  // alive until that has completed, and prevent teardown callbacks from
  // re-entering this partially destroyed controller.
  weak_ptr_factory_.InvalidateWeakPtrs();
  view_ = nullptr;
  if (bubble_widget_) {
    modal_overlay_controller_->DismissPanelImmediately(bubble_widget_.get());
  }
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

bool CommandBarController::Show(CommandBarDisposition disposition) {
  // Quick Window uses a real Chromium popup with the normal profile. It shares
  // this native command surface while every other special browser type keeps
  // Chromium's own UI and lifecycle.
  views::View* const anchor_view = modal_overlay_controller_->center_anchor();
  if ((!browser_->is_type_normal() && !browser_->is_type_popup()) ||
      !command_service_ || !execution_adapter_ || !anchor_view ||
      !anchor_view->GetWidget()) {
    return false;
  }
  RefreshHistoryItems();

  if (bubble_widget_) {
    // CreateBubble() installs a synchronous close callback, so this clears all
    // three bubble pointers before a replacement is constructed.
    bubble_widget_->Close();
  }
  CHECK(!view_);
  CHECK(!bubble_widget_);
  CHECK(!bubble_delegate_);

  auto view = std::make_unique<CommandBarView>(
      disposition, GetPlaceholder(),
      base::BindRepeating(
          [](base::WeakPtr<CommandBarController> controller,
             std::u16string_view input) {
            return controller ? controller->GetSuggestions(input)
                              : std::vector<CommandBarSuggestion>();
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<CommandBarController> controller,
             const CommandBarSuggestion& suggestion,
             std::u16string_view original_input) {
            return controller &&
                   controller->ExecuteSuggestion(suggestion, original_input);
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&CommandBarController::RequestClose,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&CommandBarController::OnViewDestroyed,
                     weak_ptr_factory_.GetWeakPtr()),
      browser_->GetProfile()->GetPrefs());

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::FLOAT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_TOUCH_BAR_GOOGLE_SEARCH));
  delegate->set_fixed_width(visual_style::kCommandBarWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kCommandBarPanelInset,
                                        visual_style::kCommandBarPanelInset));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  // The content view paints the semantic material across the dialog client.
  // Keep BubbleBorder itself transparent so it cannot flatten glass into an
  // opaque second background.
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(false);
  delegate->SetCancelCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<CommandBarController> controller) {
        if (controller) {
          controller->RequestClose();
        }
        // The modal overlay closes the Widget after its fade-out.
        return false;
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetInitiallyFocusedView(view->textfield_for_testing());

  CommandBarView* const view_ptr = delegate->SetContentsView(std::move(view));
  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&CommandBarController::OnBubbleClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    return false;
  }

  view_ = view_ptr;
  bubble_delegate_ = std::move(delegate);
  bubble_widget_ = std::move(widget);
  view_->ReapplyAppearance();

  const std::u16string initial_query = GetInitialQuery(disposition);
  view_->SetInitialQuery(initial_query,
                         /*prefer_input_fallback=*/
                         disposition == CommandBarDisposition::kCurrentTab &&
                             !initial_query.empty());
  if (!modal_overlay_controller_->ShowPanel(
          bubble_widget_.get(),
          base::BindRepeating(&CommandBarController::CloseBubbleNow,
                              weak_ptr_factory_.GetWeakPtr()))) {
    view_ = nullptr;
    bubble_widget_.reset();
    bubble_delegate_.reset();
    return false;
  }
  if (!view_) {
    return false;
  }
  view_->FocusInput();
  return true;
}

std::vector<CommandBarSuggestion> CommandBarController::GetSuggestions(
    std::u16string_view input) {
  std::vector<CommandBarSuggestion> suggestions;
  const std::optional<CommandBarSuggestion> fallback =
      execution_adapter_->PreviewInput(input);
  suggestions.reserve(kMaximumSuggestions);

  SessionBridge* session_bridge =
      browser_->GetProfile()->IsOffTheRecord()
          ? nullptr
          : SessionBridgeFactory::GetForProfile(browser_->GetProfile());

  for (const RankedCommand& ranked :
       command_service_->Query(input, kMaximumIndexedCandidates)) {
    if (!execution_adapter_->CanExecuteItem(ranked.item)) {
      continue;
    }
    CommandBarSuggestion suggestion{
        .kind = CommandBarSuggestionKind::kLocalItem,
        .title = ranked.item.title,
        .secondary_text = ranked.item.secondary_text,
        .item = ranked.item,
        .destination_url = ranked.item.url,
    };
    if (ranked.item.type == CommandItemType::kBrowserCommand) {
      if (const gfx::VectorIcon* const icon =
              GetCommandIcon(ranked.item.stable_id)) {
        suggestion.icon = ui::ImageModel::FromVectorIcon(
            *icon, visual_style::kMutedText,
            visual_style::kCommandBarResultIconSize);
      }
    }
    if ((ranked.item.type == CommandItemType::kOpenTab ||
         ranked.item.type == CommandItemType::kSavedPage) &&
        session_bridge) {
      tabs::TabInterface* tab =
          session_bridge->FindTabForOpenTabStableId(ranked.item.stable_id);
      content::WebContents* contents = tab ? tab->GetContents() : nullptr;
      favicon::ContentFaviconDriver* favicon_driver =
          contents ? favicon::ContentFaviconDriver::FromWebContents(contents)
                   : nullptr;
      if (favicon_driver) {
        suggestion.icon =
            ui::ImageModel::FromImage(favicon_driver->GetFavicon());
      }
    }
    if (suggestion.icon.IsEmpty() && ranked.item.url.has_value() &&
        (ranked.item.type == CommandItemType::kOpenTab ||
         ranked.item.type == CommandItemType::kSavedPage ||
         ranked.item.type == CommandItemType::kHistory)) {
      suggestion.icon = GetOrRequestFavicon(*ranked.item.url);
    }
    suggestions.push_back(std::move(suggestion));
    if (suggestions.size() == kMaximumSuggestions) {
      break;
    }
  }

  if (fallback.has_value()) {
    suggestions.push_back(*fallback);
  }
  suggestions = DeduplicateSuggestionsByDestination(std::move(suggestions));
  if (suggestions.size() > kMaximumSuggestions) {
    // Keep the typed navigation/search fallback as the final action while
    // still guaranteeing a non-scrolling five-row result surface.
    suggestions.erase(suggestions.begin() + (kMaximumSuggestions - 1u));
  }
  return suggestions;
}

bool CommandBarController::ExecuteSuggestion(
    const CommandBarSuggestion& suggestion,
    std::u16string_view original_input) {
  if (suggestion.kind == CommandBarSuggestionKind::kInputFallback) {
    return execution_adapter_->ExecuteInput(
        original_input,
        view_ ? view_->disposition() : CommandBarDisposition::kCurrentTab);
  }
  return suggestion.item.has_value() && view_ &&
         execution_adapter_->ExecuteItem(*suggestion.item,
                                         view_->disposition());
}

std::u16string CommandBarController::GetInitialQuery(
    CommandBarDisposition disposition) const {
  if (disposition != CommandBarDisposition::kCurrentTab) {
    return std::u16string();
  }
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return std::u16string();
  }
  const GURL& url = contents->GetVisibleURL();
  if (!url.is_valid() || url.is_empty() || url == GURL(url::kAboutBlankURL) ||
      (url.SchemeIs("chrome") && url.host() == "newtab")) {
    return std::u16string();
  }
  // Never surface URL-embedded credentials in the command bar. Keep the
  // scheme (unlike kFormatUrlOmitDefaults) so accepting an unchanged HTTP URL
  // cannot silently reinterpret it through the HTTPS-default input parser.
  return url_formatter::FormatUrl(
      url, url_formatter::kFormatUrlOmitUsernamePassword,
      base::UnescapeRule::NORMAL, nullptr, nullptr, nullptr);
}

std::u16string CommandBarController::GetPlaceholder() const {
  const std::u16string engine_name =
      execution_adapter_->GetDefaultSearchEngineName();
  return engine_name.empty()
             ? l10n_util::GetStringUTF16(IDS_TOUCH_BAR_GOOGLE_SEARCH)
             : l10n_util::GetStringFUTF16(IDS_OMNIBOX_PLACEHOLDER_TEXT,
                                          engine_name);
}

void CommandBarController::PublishBrowserCommands() {
  if (!command_service_) {
    return;
  }
  struct BrowserCommandDefinition {
    const char* stable_id;
    int title_id;
    std::vector<std::u16string> keywords;
    int priority;
  };
  std::vector<BrowserCommandDefinition> definitions = {
      {"browser.reload",
       IDS_RELOAD_MENU_NORMAL_RELOAD_ITEM,
       {u"reload", u"neu laden"},
       260},
      {"browser.reload-bypassing-cache",
       IDS_RELOAD_MENU_HARD_RELOAD_ITEM,
       {u"hard reload", u"cache", u"hart neu laden"},
       255},
      {"browser.downloads",
       IDS_DOWNLOAD_HISTORY_TITLE,
       {u"downloads", u"herunterladen"},
       230},
      {"browser.history", IDS_HISTORY_MENU, {u"history", u"verlauf"}, 225},
      {"browser.settings",
       IDS_SETTINGS,
       {u"settings", u"preferences", u"einstellungen"},
       220},
      {"browser.clear-browsing-data",
       IDS_CLEAR_BROWSING_DATA,
       {u"clear cache", u"cookies", u"browserdaten löschen"},
       215},
      {"browser.devtools",
       IDS_DEV_TOOLS,
       {u"developer tools", u"inspect", u"entwicklertools"},
       210},
      {"browser.view-source",
       IDS_VIEW_SOURCE,
       {u"view source", u"quelltext"},
       205},
      {"browser.print", IDS_CONTENT_CONTEXT_PRINT, {u"print", u"drucken"}, 200},
      {"browser.new-window",
       IDS_NEW_WINDOW,
       {u"new window", u"neues fenster"},
       195},
      {"browser.new-incognito-window",
       IDS_NEW_INCOGNITO_WINDOW,
       {u"incognito", u"private", u"inkognito"},
       190},
      {"browser.open-in-normal-window",
       IDS_AHOI_COMMAND_OPEN_IN_NORMAL_WINDOW,
       {u"open in normal window", u"quick window",
        u"in normales fenster übernehmen"},
       265},
      {"privacy.open",
       IDS_AHOI_PRIVACY_OPEN_COMMAND,
       {u"privacy open", u"privacy", u"tracking", u"datenschutz",
        u"website-schutz"},
       255},
      {"http-auth.switch",
       IDS_AHOI_HTTP_AUTH_SWITCH_ACCOUNT_COMMAND,
       {u"http auth switch account", u"basic auth", u"htaccess",
        u"http-zugang wechseln", u"konto wechseln"},
       254},
      {"http-auth.forget",
       IDS_AHOI_HTTP_AUTH_FORGET_REALM_COMMAND,
       {u"http auth forget", u"basic auth", u"htaccess",
        u"http-zugang vergessen", u"schutzbereich vergessen"},
       253},
      {"http-auth.manage",
       IDS_AHOI_HTTP_AUTH_MANAGE_COMMAND,
       {u"http auth manage", u"basic auth", u"digest", u"htaccess",
        u"http-zugänge verwalten", u"passwörter und authentifizierung"},
       252},
      {"developer.clear-site-cache",
       IDS_AHOI_DEVELOPER_CLEAR_SITE_CACHE,
       {u"site cache", u"clear current cache", u"website cache leeren"},
       250},
      {"developer.toggle-css",
       IDS_AHOI_DEVELOPER_TOGGLE_CSS,
       {u"css", u"styles", u"stylesheets", u"css deaktivieren"},
       245},
      {"developer.reveal-passwords",
       IDS_AHOI_DEVELOPER_TOGGLE_PASSWORD_FIELDS,
       {u"password", u"passwort", u"show password", u"passwort anzeigen"},
       240},
      {"developer.toggle-javascript",
       IDS_AHOI_DEVELOPER_TOGGLE_JAVASCRIPT,
       {u"javascript", u"js", u"disable javascript"},
       235},
      {"developer.toggle-images",
       IDS_AHOI_DEVELOPER_TOGGLE_IMAGES,
       {u"images", u"bilder", u"disable images"},
       230},
      {"developer.reset-page",
       IDS_AHOI_DEVELOPER_RESET_DOCUMENT,
       {u"reset page", u"reset css", u"seite zurücksetzen"},
       225},
      {"developer.screenshot-visible",
       IDS_AHOI_DEVELOPER_SCREENSHOT_VISIBLE,
       {u"screenshot", u"viewport", u"sichtbarer bereich"},
       220},
      {"developer.screenshot-full-page",
       IDS_AHOI_DEVELOPER_SCREENSHOT_FULL_PAGE,
       {u"full page screenshot", u"ganze seite", u"vollseite"},
       215},
  };

  std::vector<CommandItem> commands;
  commands.reserve(definitions.size());
  for (BrowserCommandDefinition& definition : definitions) {
    commands.push_back({
        .type = CommandItemType::kBrowserCommand,
        .stable_id = definition.stable_id,
        .title = l10n_util::GetStringUTF16(definition.title_id),
        .keywords = std::move(definition.keywords),
        .priority = definition.priority,
    });
  }
  CHECK(command_service_->ReplaceItems(CommandItemType::kBrowserCommand,
                                       std::move(commands)));
}

void CommandBarController::RefreshHistoryItems() {
  if (!history_service_ || !command_service_) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (history_query_in_flight_ ||
      (!last_history_refresh_.is_null() &&
       now - last_history_refresh_ < kHistoryRefreshInterval)) {
    return;
  }
  history_query_in_flight_ = true;
  history::QueryOptions options;
  options.SetRecentDayRange(180);
  options.max_count = 256;
  options.duplicate_policy = history::QueryOptions::REMOVE_ALL_DUPLICATES;
  options.visit_order = history::QueryOptions::RECENT_FIRST;
  history_service_->QueryHistory(
      std::u16string(), options,
      base::BindOnce(&CommandBarController::OnHistoryQueryCompleted,
                     weak_ptr_factory_.GetWeakPtr()),
      &history_task_tracker_);
}

void CommandBarController::OnHistoryQueryCompleted(
    history::QueryResults results) {
  history_query_in_flight_ = false;
  last_history_refresh_ = base::TimeTicks::Now();
  if (!command_service_) {
    return;
  }
  std::set<GURL> seen_urls;
  std::vector<CommandItem> items;
  items.reserve(results.size());
  for (const history::URLResult& result : results) {
    const GURL& url = result.url();
    if (!url.is_valid() || url.is_empty() || result.hidden() ||
        result.blocked_visit() || !seen_urls.insert(url).second) {
      continue;
    }
    const std::u16string display_url = base::UTF8ToUTF16(url.spec());
    std::u16string title = result.title();
    if (title.empty()) {
      title = base::UTF8ToUTF16(url.host());
    }
    if (title.empty()) {
      title = display_url;
    }
    items.push_back({
        .type = CommandItemType::kHistory,
        .stable_id = base::StrCat({"history:", url.spec()}),
        .title = std::move(title),
        .secondary_text = display_url,
        .keywords = {display_url, base::UTF8ToUTF16(url.host())},
        .url = url,
        .priority = 40 + std::min(result.visit_count(), 20) +
                    std::min(result.typed_count() * 2, 30),
        .last_used = result.visit_time(),
    });
  }
  CHECK(command_service_->ReplaceItems(CommandItemType::kHistory,
                                       std::move(items)));
  if (view_) {
    view_->RefreshSuggestions();
  }
}

ui::ImageModel CommandBarController::GetOrRequestFavicon(const GURL& page_url) {
  const auto cached = favicon_cache_.Get(page_url);
  if (cached != favicon_cache_.end()) {
    return cached->second;
  }
  if (favicon_service_ && page_url.is_valid() && !page_url.is_empty() &&
      requested_favicon_urls_.insert(page_url).second) {
    favicon_service_->GetFaviconImageForPageURL(
        page_url,
        base::BindOnce(&CommandBarController::OnFaviconAvailable,
                       weak_ptr_factory_.GetWeakPtr(), page_url),
        &favicon_task_tracker_);
  }
  return ui::ImageModel();
}

void CommandBarController::OnFaviconAvailable(
    const GURL& page_url,
    const favicon_base::FaviconImageResult& result) {
  requested_favicon_urls_.erase(page_url);
  if (result.image.IsEmpty()) {
    // Negative results are cached too, so reopening Cmd+T cannot repeatedly
    // hit the favicon service for the same missing icon.
    favicon_cache_.Put(page_url, ui::ImageModel());
    return;
  }
  favicon_cache_.Put(page_url, ui::ImageModel::FromImage(result.image));
  if (view_) {
    view_->RefreshSuggestions();
  }
}

void CommandBarController::RequestClose() {
  if (bubble_widget_ &&
      !modal_overlay_controller_->RequestClose(bubble_widget_.get())) {
    CloseBubbleNow();
  }
}

void CommandBarController::CloseBubbleNow() {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
}

void CommandBarController::OnViewDestroyed() {
  view_ = nullptr;
}

void CommandBarController::OnBubbleClosed() {
  // On macOS this callback can run from inside
  // BubbleWidgetObserver::OnWidgetActivationChanged. Destroying the Widget
  // there invalidates the observer currently being dispatched. Relinquish the
  // controller-visible state synchronously, then destroy the Widget and its
  // longer-lived delegate after the activation stack has unwound.
  view_ = nullptr;
  if (bubble_widget_) {
    modal_overlay_controller_->NotifyPanelClosed(bubble_widget_.get());
  }
  std::unique_ptr<views::Widget> closed_widget = std::move(bubble_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(bubble_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       widget.reset();
                       delegate.reset();
                     },
                     std::move(closed_widget), std::move(closed_delegate)));
}

}  // namespace ahoi
