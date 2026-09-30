// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_controller.h"

#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_cookie_manager.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_action_executor.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_browsing_data.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "ahoi/browser/ui/developer_toolkit/developer_cache_status_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_cookie_manager_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_profile_editor_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_bubble_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/class_property.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace ahoi {

namespace {

// A focused Textfield (cookie manager, profile editor) can still be the input
// method's text input client while its client-owned Widget is destroyed,
// which trips NativeWidgetMac's focus check. Blurring first detaches it (as
// cc26c51 for the sidebar dialogs; handoff 064).
void BlurBeforeDestruction(views::Widget* widget) {
  if (!widget) {
    return;
  }
  if (widget->GetFocusManager()) {
    widget->GetFocusManager()->ClearFocus();
    // ClearFocus() keeps the stored view, which Cocoa's asynchronous
    // first-responder answer would restore (build 37 crash); drop it.
    widget->GetFocusManager()->SetStoredFocusView(nullptr);
  }
  // Fallback: also detach the text input client.
  if (ui::InputMethod* input_method = widget->GetInputMethod()) {
    input_method->SetFocusedTextInputClient(nullptr);
  }
}

DEFINE_UI_CLASS_PROPERTY_KEY(
    int,
    kDeveloperToolbarSurfaceKey,
    static_cast<int>(DeveloperToolbarSurface::kToolkit))

DeveloperToolbarSurface SurfaceForAnchor(views::View* anchor_view) {
  return anchor_view
             ? static_cast<DeveloperToolbarSurface>(
                   anchor_view->GetProperty(kDeveloperToolbarSurfaceKey))
             : DeveloperToolbarSurface::kToolkit;
}

}  // namespace

DeveloperToolkitController::DeveloperToolkitController(
    BrowserWindowInterface* browser)
    : browser_(browser) {
  CHECK(browser_);
}

DeveloperToolkitController::~DeveloperToolkitController() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  cookie_manager_view_ = nullptr;
  BlurBeforeDestruction(cookie_manager_widget_.get());
  cookie_manager_widget_.reset();
  cookie_manager_delegate_.reset();
  cache_status_view_ = nullptr;
  BlurBeforeDestruction(cache_status_widget_.get());
  cache_status_widget_.reset();
  cache_status_delegate_.reset();
  profile_editor_view_ = nullptr;
  BlurBeforeDestruction(profile_editor_widget_.get());
  profile_editor_widget_.reset();
  profile_editor_delegate_.reset();
  BlurBeforeDestruction(bubble_widget_.get());
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

// static
void DeveloperToolkitController::ConfigureToolbarAnchor(
    views::View* anchor_view,
    DeveloperToolbarSurface surface) {
  if (anchor_view) {
    anchor_view->SetProperty(kDeveloperToolbarSurfaceKey,
                             static_cast<int>(surface));
  }
}

bool DeveloperToolkitController::Show(views::View* anchor_view) {
  switch (SurfaceForAnchor(anchor_view)) {
    case DeveloperToolbarSurface::kCookieManager:
      return ShowCookieManager(anchor_view);
    case DeveloperToolbarSurface::kCacheClear:
      return ShowCacheClear(anchor_view);
    case DeveloperToolbarSurface::kToolkit:
      break;
  }
  if (!anchor_view || !anchor_view->GetWidget()) {
    return false;
  }
  // Toggling an already-open surface must remain possible even if focus moved
  // programmatically to an unsupported pane between press and release.
  if (bubble_widget_ || bubble_close_pending_) {
    if (bubble_widget_) {
      bubble_widget_->Close();
    }
    return true;
  }
  content::WebContents* const contents = GetActiveWebContents();
  if (!contents) {
    return false;
  }
  if (Profile* original = browser_->GetProfile()->GetOriginalProfile()) {
    developer_toolkit_prefs::ActivateToolkit(*original->GetPrefs());
  }
  if (cookie_manager_widget_) {
    cookie_manager_widget_->Close();
  }
  if (cache_status_widget_) {
    cache_status_widget_->Close();
  }

  const GURL context_url = contents->GetLastCommittedURL();
  const url::Origin origin = url::Origin::Create(context_url);
  const std::u16string context_label =
      IsSupportedDeveloperTarget(contents)
          ? base::UTF8ToUTF16(origin.Serialize())
          : base::UTF8ToUTF16(context_url.spec());
  bubble_contents_ = contents->GetWeakPtr();
  auto view = std::make_unique<DeveloperToolkitBubbleView>(
      context_label, GetToolbarVisibility(),
      base::BindRepeating(
          [](base::WeakPtr<DeveloperToolkitController> controller,
             DeveloperAction action) {
            return controller
                       ? controller->Execute(action)
                       : DeveloperActionResult{
                             action, DeveloperActionStatus::kUnavailable};
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<DeveloperToolkitController> controller,
             BrowsingDataClearOptions options,
             BrowsingDataClearCallback callback) {
            return controller &&
                   controller->ClearBrowsingData(options, std::move(callback));
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<DeveloperToolkitController> controller,
             developer_toolkit_prefs::ToolbarVisibility visibility) {
            return controller && controller->SetToolbarVisibility(visibility);
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&DeveloperToolkitController::OpenDevTools,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&DeveloperToolkitController::OpenPasswordManager,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&DeveloperToolkitController::OpenCookieManager,
                          weak_ptr_factory_.GetWeakPtr(), anchor_view),
      base::BindRepeating(&DeveloperToolkitController::OpenProfileEditor,
                          weak_ptr_factory_.GetWeakPtr(), anchor_view),
      browser_->GetProfile()->GetPrefs(), GetActivationState());
  DeveloperToolkitBubbleView* const view_ptr = view.get();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLKIT_TITLE));
  delegate->set_fixed_width(visual_style::kDeveloperToolkitWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kDeveloperToolkitInset,
                                        visual_style::kDeveloperToolkitInset));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(true);

  delegate->SetContentsView(std::move(view));
  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&DeveloperToolkitController::OnBubbleClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    bubble_contents_.reset();
    return false;
  }

  bubble_delegate_ = std::move(delegate);
  bubble_widget_ = std::move(widget);
  view_ptr->ReapplyAppearance();
  bubble_widget_->Show();
  return true;
}

bool DeveloperToolkitController::IsSurfaceShowing(
    DeveloperToolbarSurface surface) const {
  switch (surface) {
    case DeveloperToolbarSurface::kToolkit:
      return bubble_widget_ != nullptr || bubble_close_pending_;
    case DeveloperToolbarSurface::kCookieManager:
      return cookie_manager_widget_ != nullptr ||
             cookie_manager_close_pending_;
    case DeveloperToolbarSurface::kCacheClear:
      return cache_status_widget_ != nullptr || cache_status_close_pending_;
  }
  return false;
}

void DeveloperToolkitController::CloseAllSurfaces() {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  if (cookie_manager_widget_) {
    cookie_manager_widget_->Close();
  }
  if (cache_status_widget_) {
    cache_status_widget_->Close();
  }
  if (profile_editor_widget_) {
    profile_editor_widget_->Close();
  }
}

bool DeveloperToolkitController::CanExecute() const {
  return IsSupportedDeveloperTarget(GetToolkitWebContents());
}

DeveloperActionResult DeveloperToolkitController::Execute(
    DeveloperAction action) {
  content::WebContents* const contents = GetToolkitWebContents();
  if (!IsSupportedDeveloperTarget(contents)) {
    return {action, DeveloperActionStatus::kRejectedUnsupportedTarget};
  }
  DeveloperActionExecutor* const executor = GetOrCreateExecutor();
  return executor ? executor->Execute(contents, action)
                  : DeveloperActionResult{action,
                                          DeveloperActionStatus::kUnavailable};
}

bool DeveloperToolkitController::ClearBrowsingData(
    BrowsingDataClearOptions options,
    BrowsingDataClearCallback callback) {
  content::WebContents* const contents = GetToolkitWebContents();
  if (!IsSupportedDeveloperTarget(contents) || callback.is_null()) {
    return false;
  }
  DeveloperActionExecutor* const executor = GetOrCreateExecutor();
  return executor &&
         executor->ClearBrowsingData(contents, options, std::move(callback));
}

content::WebContents* DeveloperToolkitController::GetActiveWebContents() const {
  return browser_ && browser_->GetTabStripModel()
             ? browser_->GetTabStripModel()->GetActiveWebContents()
             : nullptr;
}

content::WebContents* DeveloperToolkitController::GetToolkitWebContents()
    const {
  return bubble_widget_ ? bubble_contents_.get() : GetActiveWebContents();
}

void DeveloperToolkitController::ActivateToolkitWebContents() {
  content::WebContents* const contents = GetToolkitWebContents();
  TabStripModel* const model =
      browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!contents || !model) {
    return;
  }
  const int index = model->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab && index != model->active_index()) {
    model->ActivateTabAt(index);
  }
}

developer_toolkit_prefs::ToolbarVisibility
DeveloperToolkitController::GetToolbarVisibility() const {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  profile = profile ? profile->GetOriginalProfile() : nullptr;
  return profile ? developer_toolkit_prefs::GetToolbarVisibility(
                       *profile->GetPrefs())
                 : developer_toolkit_prefs::ToolbarVisibility();
}

bool DeveloperToolkitController::SetToolbarVisibility(
    developer_toolkit_prefs::ToolbarVisibility visibility) {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  profile = profile ? profile->GetOriginalProfile() : nullptr;
  return profile && developer_toolkit_prefs::SetToolbarVisibility(
                        *profile->GetPrefs(), visibility);
}

DeveloperActionExecutor* DeveloperToolkitController::GetOrCreateExecutor() {
  if (!executor_) {
    content::WebContents* const contents = GetActiveWebContents();
    if (!contents) {
      return nullptr;
    }
    executor_ =
        CreateChromiumDeveloperActionExecutor(contents->GetBrowserContext());
  }
  return executor_.get();
}

DeveloperActivationState DeveloperToolkitController::GetActivationState() {
  content::WebContents* const contents = GetActiveWebContents();
  DeveloperActivationState state;
  if (!contents) {
    return state;
  }
  if (DeveloperActionExecutor* executor = GetOrCreateExecutor()) {
    state = executor->GetActivationState(contents);
  }
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!profile || profile->IsOffTheRecord()) {
    return state;
  }
  PrefDeveloperProfileStore store(profile->GetPrefs(), false);
  const GURL url = contents->GetLastCommittedURL();
  for (const DeveloperAsset& asset :
       GetDeveloperAssetsForNavigation(store, url)) {
    state.Set(asset.kind == DeveloperAssetKind::kStyle
                  ? DeveloperActivation::kCss
                  : DeveloperActivation::kJavaScript,
              true);
  }
  const std::optional<DeveloperProfile> exact =
      store.Get(url::Origin::Create(url));
  if (exact) {
    state.Set(
        DeveloperActivation::kHeaders,
        exact->header_rules_enabled || exact->response_header_rules_enabled);
    state.Set(DeveloperActivation::kCacheOff, exact->cache_disabled);
  }
  return state;
}

void DeveloperToolkitController::OpenDevTools() {
  if (!browser_) {
    return;
  }
  // Close the transient helper surface first so the DevTools window or docked
  // pane receives focus without a stale toolbar bubble above it. Dispatching
  // Chromium's normal command preserves policy checks, metrics and docking
  // behavior instead of creating a parallel DevTools implementation.
  ActivateToolkitWebContents();
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  chrome::ExecuteCommand(browser_, IDC_DEV_TOOLS_TOGGLE);
}

void DeveloperToolkitController::OpenPasswordManager() {
  if (!browser_) {
    return;
  }
  // Reuse Chromium's command path so its profile policy, singleton-tab routing
  // and OS-authenticated plaintext boundary remain authoritative. Ahoi never
  // requests, receives or retains a saved credential.
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  chrome::ExecuteCommand(browser_, IDC_SHOW_PASSWORD_MANAGER);
}

void DeveloperToolkitController::OpenCookieManager(views::View* anchor_view) {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  ShowCookieManager(anchor_view);
}

bool DeveloperToolkitController::ShowCookieManager(views::View* anchor_view) {
  if (!anchor_view || !anchor_view->GetWidget()) {
    return false;
  }
  if (cookie_manager_widget_ || cookie_manager_close_pending_) {
    if (cookie_manager_widget_) {
      cookie_manager_widget_->Close();
    }
    return true;
  }
  content::WebContents* const contents = GetActiveWebContents();
  if (!IsSupportedDeveloperTarget(contents)) {
    return false;
  }
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  if (cache_status_widget_) {
    cache_status_widget_->Close();
  }

  const GURL site_url = contents->GetLastCommittedURL();
  auto view = std::make_unique<DeveloperCookieManagerView>(
      site_url,
      // Handoff 060: the active tab's own partition, which differs from the
      // default one in a Workspace with its own website sessions.
      CreateChromiumDeveloperCookieAdapter(
          contents->GetBrowserContext(),
          contents->GetPrimaryMainFrame()->GetStoragePartition()->GetConfig()),
      browser_->GetProfile()->GetPrefs());
  DeveloperCookieManagerView* const view_ptr = view.get();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_MANAGER_TITLE));
  delegate->set_fixed_width(visual_style::kDeveloperCookieManagerWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kDeveloperToolkitInset,
                                        visual_style::kDeveloperToolkitInset));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(true);
  delegate->SetInitiallyFocusedView(view_ptr->initially_focused_view());
  delegate->SetContentsView(std::move(view));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&DeveloperToolkitController::OnCookieManagerClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    return false;
  }
  cookie_manager_view_ = view_ptr;
  cookie_manager_delegate_ = std::move(delegate);
  cookie_manager_widget_ = std::move(widget);
  cookie_manager_view_->ReapplyAppearance();
  cookie_manager_widget_->Show();
  return true;
}

bool DeveloperToolkitController::ShowCacheClear(views::View* anchor_view) {
  if (!anchor_view || !anchor_view->GetWidget()) {
    return false;
  }
  if (cache_status_widget_ || cache_status_close_pending_) {
    if (cache_status_widget_) {
      cache_status_widget_->Close();
    }
    return true;
  }
  content::WebContents* const contents = GetActiveWebContents();
  if (!IsSupportedDeveloperTarget(contents)) {
    return false;
  }
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  if (cookie_manager_widget_) {
    cookie_manager_widget_->Close();
  }
  const bool start_new_request = !cache_clear_in_flight_;

  const url::Origin origin =
      url::Origin::Create(contents->GetLastCommittedURL());
  auto view = std::make_unique<DeveloperCacheStatusView>(
      base::UTF8ToUTF16(origin.Serialize()),
      browser_->GetProfile()->GetPrefs());
  DeveloperCacheStatusView* const view_ptr = view.get();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_CACHE_STATUS_TITLE));
  delegate->set_fixed_width(visual_style::kDeveloperCacheStatusWidth);
  delegate->set_margins(gfx::Insets::VH(10, 12));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(true);
  delegate->SetContentsView(std::move(view));
  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&DeveloperToolkitController::OnCacheStatusClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    return false;
  }
  cache_status_view_ = view_ptr;
  cache_status_delegate_ = std::move(delegate);
  cache_status_widget_ = std::move(widget);
  cache_status_view_->ReapplyAppearance();
  cache_status_widget_->Show();

  // If the user closed the status bubble while deletion was still running,
  // reopening the button attaches to that one request instead of racing a
  // second cache deletion against it.
  if (!start_new_request) {
    return true;
  }
  cache_clear_in_flight_ = true;
  DeveloperActionExecutor* const executor = GetOrCreateExecutor();
  const bool accepted =
      executor &&
      executor->ClearBrowsingData(
          contents, BrowsingDataOptionsForScope(BrowsingDataScope::kCacheOnly),
          base::BindOnce(&DeveloperToolkitController::OnCacheClearFinished,
                         weak_ptr_factory_.GetWeakPtr()));
  if (!accepted) {
    cache_clear_in_flight_ = false;
    cache_status_view_->SetState(DeveloperCacheStatusView::State::kFailed);
  }
  return true;
}

void DeveloperToolkitController::OnCacheClearFinished(
    BrowsingDataClearResult result) {
  if (!cache_clear_in_flight_) {
    return;
  }
  cache_clear_in_flight_ = false;
  if (cache_status_view_) {
    cache_status_view_->SetState(
        result.status() == BrowsingDataClearStatus::kSucceeded
            ? DeveloperCacheStatusView::State::kSucceeded
            : DeveloperCacheStatusView::State::kFailed);
  }
}

void DeveloperToolkitController::OnBubbleClosed() {
  BlurBeforeDestruction(bubble_widget_.get());
  bubble_contents_.reset();
  bubble_close_pending_ = true;
  std::unique_ptr<views::Widget> closed_widget = std::move(bubble_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(bubble_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<DeveloperToolkitController> controller,
                        std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       BlurBeforeDestruction(widget.get());
                       widget.reset();
                       delegate.reset();
                       if (controller) {
                         controller->bubble_close_pending_ = false;
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(closed_widget), std::move(closed_delegate)));
}

void DeveloperToolkitController::OnCookieManagerClosed() {
  BlurBeforeDestruction(cookie_manager_widget_.get());
  cookie_manager_view_ = nullptr;
  cookie_manager_close_pending_ = true;
  std::unique_ptr<views::Widget> closed_widget =
      std::move(cookie_manager_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(cookie_manager_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<DeveloperToolkitController> controller,
                        std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       BlurBeforeDestruction(widget.get());
                       widget.reset();
                       delegate.reset();
                       if (controller) {
                         controller->cookie_manager_close_pending_ = false;
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(closed_widget), std::move(closed_delegate)));
}

void DeveloperToolkitController::OnCacheStatusClosed() {
  BlurBeforeDestruction(cache_status_widget_.get());
  cache_status_view_ = nullptr;
  cache_status_close_pending_ = true;
  std::unique_ptr<views::Widget> closed_widget =
      std::move(cache_status_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(cache_status_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<DeveloperToolkitController> controller,
                        std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       BlurBeforeDestruction(widget.get());
                       widget.reset();
                       delegate.reset();
                       if (controller) {
                         controller->cache_status_close_pending_ = false;
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(closed_widget), std::move(closed_delegate)));
}

void DeveloperToolkitController::OnProfileEditorClosed() {
  BlurBeforeDestruction(profile_editor_widget_.get());
  profile_editor_view_ = nullptr;
  std::unique_ptr<views::Widget> closed_widget =
      std::move(profile_editor_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(profile_editor_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       BlurBeforeDestruction(widget.get());
                       widget.reset();
                       delegate.reset();
                     },
                     std::move(closed_widget), std::move(closed_delegate)));
}

}  // namespace ahoi
