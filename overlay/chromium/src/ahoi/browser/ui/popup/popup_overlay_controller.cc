// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/popup/popup_overlay_controller.h"

#include <memory>
#include <utility>

#include "ahoi/browser/popup/popup_types.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/popup/popup_overlay_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_collection.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

namespace ahoi::popup_ui {

namespace {

std::u16string FallbackNotice(popup::PopupFallbackReason reason) {
  switch (reason) {
    case popup::PopupFallbackReason::kSensitiveAuthenticationFlow:
      return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_AUTH);
    case popup::PopupFallbackReason::kPaymentFlow:
      return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_PAYMENT);
    case popup::PopupFallbackReason::kPasskeyFlow:
      return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_PASSKEY);
    case popup::PopupFallbackReason::kFullscreenRequest:
      return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_FULLSCREEN);
    case popup::PopupFallbackReason::kUnsupportedNavigation:
    case popup::PopupFallbackReason::kOpenerClosed:
      return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_SEPARATE_WINDOW);
  }
  return l10n_util::GetStringUTF16(IDS_AHOI_POPUP_FALLBACK_SEPARATE_WINDOW);
}

}  // namespace

PopupOverlayController::PopupOverlayController(Browser* browser,
                                               views::View* contents_host,
                                               OpenerPaneProvider provider)
    : browser_(browser),
      contents_host_(contents_host),
      opener_pane_provider_(std::move(provider)) {
  CHECK(browser_);
  CHECK(contents_host_);
  contents_host_observation_.Observe(contents_host_);
}

PopupOverlayController::~PopupOverlayController() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  opener_pane_observation_.Reset();
  contents_host_observation_.Reset();
  RemoveOverlayViewImmediately();
  if (browser_ && service_.popup_contents()) {
    browser_->SetAsDelegateForAhoiPopupOverlay(service_.popup_contents(),
                                               /*set_delegate=*/false);
  }
  service_.ResetForShutdown();
  ClearFocusReturnTarget();
  browser_ = nullptr;
  contents_host_ = nullptr;
}

bool PopupOverlayController::TryShow(
    content::WebContents* opener,
    std::unique_ptr<content::WebContents>* popup_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture) {
  if (!browser_ || !browser_->is_type_normal() || !opener || !popup_contents ||
      !*popup_contents || !user_gesture || IsShowing() || popup_view_ ||
      !contents_host_ || !contents_host_->GetWidget() ||
      opener->GetBrowserContext() != (*popup_contents)->GetBrowserContext() ||
      opener->GetBrowserContext() != browser_->profile()) {
    return false;
  }
  if (!opener_pane_provider_ || !opener_pane_provider_.Run(opener)) {
    // A background or detached opener has no pane to cover without creating a
    // misleading window-wide overlay. Keep it on Chromium's native path.
    return false;
  }
  if (!popup::IsOverlayEligible(popup::ClassifyPopupForOverlay(
          target_url, disposition, window_features))) {
    return false;
  }

  // A detached popup has no TabModel yet. Browser attaches Chromium's
  // idempotent tab helpers and all delegate-side observers explicitly; the
  // matching teardown happens synchronously before every ownership transfer.
  browser_->SetAsDelegateForAhoiPopupOverlay(popup_contents->get(),
                                             /*set_delegate=*/true);
  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          browser_->profile()->GetPrefs(),
          base::BindRepeating(&PopupOverlayController::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  initial_window_features_ = window_features.Clone();
  original_user_gesture_ = user_gesture;
  CaptureFocusReturnTarget(opener);
  if (!service_.Adopt(opener, popup_contents)) {
    browser_->SetAsDelegateForAhoiPopupOverlay(popup_contents->get(),
                                               /*set_delegate=*/false);
    ClearFocusReturnTarget();
    ClearRequestMetadata();
    appearance_signal_source_.reset();
    return false;
  }
  ObserveOpenerPane();
  CreateOverlayView();
  if (popup_view_) {
    return true;
  }

  // View creation is expected to be infallible after the preflight above, but
  // never consume a WebContents if its browser-owned host disappeared during
  // setup. ReleaseForTransfer() performs the symmetric delegate teardown.
  *popup_contents = service_.ReleaseForTransfer();
  ClearFocusReturnTarget();
  ClearRequestMetadata();
  appearance_signal_source_.reset();
  return false;
}

bool PopupOverlayController::OwnsContents(
    const content::WebContents* contents) const {
  return service_.OwnsContents(contents);
}

bool PopupOverlayController::ActivateOwnedContents(
    content::WebContents* contents) {
  if (!OwnsContents(contents) || !popup_view_) {
    return false;
  }
  if (browser_ && browser_->window()) {
    browser_->window()->Activate();
  }
  popup_view_->FocusWebContents();
  return true;
}

bool PopupOverlayController::CloseOwnedContents(
    content::WebContents* contents) {
  if (!service_.CloseOwnedContents(contents)) {
    return false;
  }
  DismissOverlayView(/*animate=*/true, /*restore_focus=*/true);
  ClearRequestMetadata();
  return true;
}

bool PopupOverlayController::HandleBeforeUnloadFired(
    content::WebContents* contents,
    bool proceed,
    bool* proceed_to_fire_unload) {
  return service_.HandleBeforeUnloadFired(contents, proceed,
                                          proceed_to_fire_unload);
}

bool PopupOverlayController::OpenOwnedContentsInSeparateWindow(
    content::WebContents* contents,
    popup::PopupFallbackReason reason) {
  return OwnsContents(contents) && OpenPopupInSeparateWindow(reason);
}

void PopupOverlayController::OnContentsWillClose(
    content::WebContents* contents) {
  if (contents && contents == service_.opener()) {
    OpenPopupInSeparateWindow(popup::PopupFallbackReason::kOpenerClosed);
  }
}

views::View* PopupOverlayController::overlay_view_for_testing() const {
  return popup_view_;
}

void PopupOverlayController::CreateOverlayView() {
  if (popup_view_ || !service_.popup_contents() || !contents_host_ ||
      !appearance_signal_source_) {
    return;
  }
  auto popup_view = std::make_unique<PopupOverlayView>(
      service_.popup_contents()->GetBrowserContext(),
      base::BindRepeating(&PopupOverlayController::RequestClosePopup,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&PopupOverlayController::PromotePopupToTab,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&PopupOverlayController::SplitPopupWithOpener,
                          weak_ptr_factory_.GetWeakPtr()),
      appearance_signal_source_->policy());
  popup_view_ = contents_host_->AddChildView(std::move(popup_view));
  popup_view_->SetWebContents(service_.popup_contents());
  popup_view_->SetDisplayedOrigin(service_.displayed_origin());
  popup_view_->SetSplitAvailability(GetSplitAvailability());
  if (initial_window_features_) {
    popup_view_->SetPreferredCardSize(
        gfx::Size(initial_window_features_->has_width
                      ? initial_window_features_->bounds.width()
                      : 0,
                  initial_window_features_->has_height
                      ? initial_window_features_->bounds.height()
                      : 0));
  }
  popup_view_->SetVisible(true);
  UpdateOverlayBounds();
  contents_host_->ReorderChildView(popup_view_,
                                   contents_host_->children().size() - 1);
  popup_view_->AnimateIn();
  popup_view_->FocusWebContents();
}

void PopupOverlayController::RequestClosePopup() {
  service_.RequestClose();
}

void PopupOverlayController::PromotePopupToTab() {
  if (!browser_ || !service_.popup_contents()) {
    return;
  }
  content::WebContents* const popup = service_.popup_contents();
  std::unique_ptr<content::WebContents> contents =
      service_.ReleaseForTransfer();
  if (!contents) {
    return;
  }
  DismissOverlayView(/*animate=*/true, /*restore_focus=*/false);
  ClearFocusReturnTarget();
  ClearRequestMetadata();
  browser_->tab_strip_model()->AddWebContents(
      std::move(contents), TabStripModel::kNoTab, ui::PAGE_TRANSITION_LINK,
      AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER);
  DCHECK_NE(browser_->tab_strip_model()->GetIndexOfWebContents(popup),
            TabStripModel::kNoTab);
}

void PopupOverlayController::SplitPopupWithOpener() {
  if (!browser_ || !service_.popup_contents()) {
    return;
  }
  const popup::PopupSplitAvailability availability = GetSplitAvailability();
  if (availability != popup::PopupSplitAvailability::kAvailable) {
    ShowSplitRejection(availability);
    return;
  }

  TabStripModel* const model = browser_->tab_strip_model();
  base::WeakPtr<content::WebContents> opener = service_.opener()->GetWeakPtr();
  content::WebContents* const popup = service_.popup_contents();
  std::unique_ptr<content::WebContents> contents =
      service_.ReleaseForTransfer();
  if (!contents) {
    return;
  }

  // Full groups were rejected before ownership changed. The unexpected-failure
  // rollback below still restores the same WebContents, form state and history
  // instead of silently promoting it to a standalone tab.
  model->AddWebContents(std::move(contents), TabStripModel::kNoTab,
                        ui::PAGE_TRANSITION_LINK,
                        AddTabTypes::ADD_INHERIT_OPENER);
  const int popup_index = model->GetIndexOfWebContents(popup);
  const int opener_index = model->GetIndexOfWebContents(opener.get());
  const bool did_split =
      popup_index != TabStripModel::kNoTab &&
      opener_index != TabStripModel::kNoTab &&
      model
          ->CreateOrAddToSplitFromDrop(popup_index, opener_index,
                                       split_tabs::SplitTabArrangement::kLinear)
          .has_value();
  if (did_split) {
    const int focused_popup_index = model->GetIndexOfWebContents(popup);
    if (focused_popup_index != TabStripModel::kNoTab) {
      model->ActivateTabAt(focused_popup_index);
    }
    DismissOverlayView(/*animate=*/true, /*restore_focus=*/false);
    ClearFocusReturnTarget();
    ClearRequestMetadata();
    return;
  }

  std::unique_ptr<content::WebContents> rejected_contents;
  const int inserted_index = model->GetIndexOfWebContents(popup);
  if (inserted_index != TabStripModel::kNoTab) {
    rejected_contents = model->DetachWebContentsAtForInsertion(inserted_index);
  }
  if (rejected_contents &&
      service_.RestoreAfterRejectedTransfer(opener.get(), &rejected_contents)) {
    browser_->SetAsDelegateForAhoiPopupOverlay(service_.popup_contents(),
                                               /*set_delegate=*/true);
    ObserveOpenerPane();
    const int restored_opener_index =
        opener ? model->GetIndexOfWebContents(opener.get())
               : TabStripModel::kNoTab;
    if (restored_opener_index != TabStripModel::kNoTab) {
      model->ActivateTabAt(restored_opener_index);
    }
    ShowSplitRejection(opener
                           ? popup::PopupSplitAvailability::kOpenerNotInWindow
                           : popup::PopupSplitAvailability::kOpenerMissing);
    return;
  }

  // The service was empty immediately before insertion, so a detached popup
  // must always be restorable. Reaching this means Chromium destroyed the
  // inserted WebContents itself; there is no standalone-tab fallback.
  DCHECK(!rejected_contents);
  DismissOverlayView(/*animate=*/true, /*restore_focus=*/false);
  ClearFocusReturnTarget();
  ClearRequestMetadata();
}

bool PopupOverlayController::OpenPopupInSeparateWindow(
    popup::PopupFallbackReason reason) {
  if (!browser_ || !service_.popup_contents()) {
    return false;
  }
  content::WebContents* const opener = service_.opener();
  content::WebContents* const popup = service_.popup_contents();
  const GURL target_url = popup->GetVisibleURL();
  if (popup_view_) {
    popup_view_->ShowStatus(FallbackNotice(reason), /*is_error=*/false);
  }
  std::unique_ptr<content::WebContents> contents =
      service_.ReleaseForTransfer();
  if (!contents) {
    return false;
  }
  DismissOverlayView(/*animate=*/true, /*restore_focus=*/false);
  ClearFocusReturnTarget();

  blink::mojom::WindowFeatures default_features;
  const blink::mojom::WindowFeatures& window_features =
      initial_window_features_ ? *initial_window_features_ : default_features;
  content::WebContents* const inserted = chrome::AddWebContents(
      browser_, opener, std::move(contents), target_url,
      WindowOpenDisposition::NEW_POPUP, window_features,
      NavigateParams::WindowAction::kShowWindow, original_user_gesture_);
  ClearRequestMetadata();
  return inserted == popup;
}

popup::PopupSplitAvailability PopupOverlayController::GetSplitAvailability()
    const {
  if (!browser_ || !service_.opener()) {
    return popup::ClassifySplitAvailability(service_.opener() != nullptr, false,
                                            0u,
                                            tabs::SplitTabCollection::kMaxTabs);
  }
  TabStripModel* const model = browser_->tab_strip_model();
  const int opener_index = model->GetIndexOfWebContents(service_.opener());
  size_t pane_count = 1u;
  if (opener_index != TabStripModel::kNoTab) {
    tabs::TabInterface* const opener_tab = model->GetTabAtIndex(opener_index);
    if (opener_tab && opener_tab->IsSplit()) {
      split_tabs::SplitTabData* const split_data =
          model->GetSplitData(opener_tab->GetSplit().value());
      pane_count = split_data ? split_data->ListTabs().size()
                              : tabs::SplitTabCollection::kMaxTabs;
    }
  }
  return popup::ClassifySplitAvailability(
      true, opener_index != TabStripModel::kNoTab, pane_count,
      tabs::SplitTabCollection::kMaxTabs);
}

void PopupOverlayController::ShowSplitRejection(
    popup::PopupSplitAvailability availability) {
  if (!popup_view_) {
    return;
  }
  popup_view_->SetSplitAvailability(availability);
  popup_view_->ShowStatus(
      l10n_util::GetStringUTF16(
          availability == popup::PopupSplitAvailability::kSplitFull
              ? IDS_AHOI_POPUP_SPLIT_FULL
              : IDS_AHOI_POPUP_SPLIT_UNAVAILABLE),
      /*is_error=*/true);
}

void PopupOverlayController::ObserveOpenerPane() {
  opener_pane_observation_.Reset();
  opener_pane_ = nullptr;
  if (!opener_pane_provider_ || !service_.opener()) {
    return;
  }
  opener_pane_ = opener_pane_provider_.Run(service_.opener());
  if (opener_pane_ && opener_pane_ != contents_host_) {
    opener_pane_observation_.Observe(opener_pane_);
  }
  UpdateOverlayBounds();
}

void PopupOverlayController::DismissOverlayView(bool animate,
                                                bool restore_focus) {
  if (!popup_view_) {
    if (restore_focus) {
      ScheduleFocusRestore();
    } else {
      ClearFocusReturnTarget();
    }
    return;
  }
  popup_view_->SetWebContents(nullptr);
  if (!animate) {
    RemoveOverlayViewImmediately();
    if (restore_focus) {
      ScheduleFocusRestore();
    } else {
      ClearFocusReturnTarget();
    }
    return;
  }
  if (dismissal_pending_) {
    dismissal_restore_focus_ |= restore_focus;
    return;
  }
  dismissal_pending_ = true;
  dismissal_restore_focus_ = restore_focus;
  const uint64_t generation = ++dismissal_generation_;
  popup_view_->AnimateOut(
      base::BindOnce(&PopupOverlayController::FinishOverlayDismissal,
                     weak_ptr_factory_.GetWeakPtr(), generation));
}

void PopupOverlayController::FinishOverlayDismissal(uint64_t generation) {
  if (!dismissal_pending_ || generation != dismissal_generation_) {
    return;
  }
  const bool restore_focus = dismissal_restore_focus_;
  RemoveOverlayViewImmediately();
  if (restore_focus) {
    ScheduleFocusRestore();
  } else {
    ClearFocusReturnTarget();
  }
}

void PopupOverlayController::RemoveOverlayViewImmediately() {
  ++dismissal_generation_;
  dismissal_pending_ = false;
  dismissal_restore_focus_ = false;
  appearance_signal_source_.reset();
  if (!popup_view_) {
    return;
  }
  PopupOverlayView* const popup_view = popup_view_;
  popup_view_ = nullptr;
  popup_view->SetWebContents(nullptr);
  if (contents_host_ && popup_view->parent() == contents_host_) {
    contents_host_->RemoveChildViewT(popup_view);
  }
}

void PopupOverlayController::CaptureFocusReturnTarget(
    content::WebContents* opener) {
  ClearFocusReturnTarget();
  if (opener) {
    focus_return_contents_ = opener->GetWeakPtr();
  }
  if (!contents_host_) {
    return;
  }
  if (views::FocusManager* const focus_manager =
          contents_host_->GetFocusManager()) {
    focus_return_view_tracker_.SetView(focus_manager->GetFocusedView());
  }
}

void PopupOverlayController::ClearFocusReturnTarget() {
  focus_return_view_tracker_.SetView(nullptr);
  focus_return_contents_.reset();
}

void PopupOverlayController::ScheduleFocusRestore() {
  if (!focus_return_view_tracker_.view() && !focus_return_contents_) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PopupOverlayController::RestoreFocus,
                                weak_ptr_factory_.GetWeakPtr()));
}

void PopupOverlayController::RestoreFocus() {
  views::View* const view = focus_return_view_tracker_.view();
  base::WeakPtr<content::WebContents> contents = focus_return_contents_;
  ClearFocusReturnTarget();
  if (popup_view_) {
    return;
  }
  if (view && view->GetVisible() && view->GetWidget()) {
    view->RequestFocus();
    return;
  }
  if (!contents || !browser_ || browser_->IsAttemptingToCloseBrowser() ||
      browser_->IsDeleteScheduled()) {
    return;
  }
  const int opener_index =
      browser_->tab_strip_model()->GetIndexOfWebContents(contents.get());
  if (opener_index == TabStripModel::kNoTab || !opener_pane_provider_ ||
      !opener_pane_provider_.Run(contents.get())) {
    return;
  }
  browser_->tab_strip_model()->ActivateTabAt(opener_index);
  if (browser_->window()) {
    browser_->window()->Activate();
  }
  contents->Focus();
}

void PopupOverlayController::UpdateOverlayBounds() {
  if (popup_view_ && contents_host_) {
    gfx::Rect bounds = contents_host_->GetLocalBounds();
    if (opener_pane_ && opener_pane_->GetVisible() &&
        opener_pane_->GetWidget() == contents_host_->GetWidget()) {
      bounds = opener_pane_->GetLocalBounds();
      bounds = views::View::ConvertRectToTarget(opener_pane_, contents_host_,
                                                bounds);
    }
    popup_view_->SetBoundsRect(bounds);
    popup_view_->InvalidateLayout();
  }
}

void PopupOverlayController::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  if (!popup_view_) {
    return;
  }
  popup_view_->SetAppearance(policy);
  if (policy.reduced_motion && dismissal_pending_) {
    // Do not delete AppearanceRuntimeSignalSource from inside its own change
    // callback. Completing on the next UI task still makes reduced motion
    // effectively immediate and keeps the source lifetime non-reentrant.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&PopupOverlayController::FinishOverlayDismissal,
                       weak_ptr_factory_.GetWeakPtr(), dismissal_generation_));
  }
}

void PopupOverlayController::ClearRequestMetadata() {
  opener_pane_observation_.Reset();
  opener_pane_ = nullptr;
  initial_window_features_.reset();
  original_user_gesture_ = false;
}

void PopupOverlayController::OnPopupServiceStateChanged() {
  if (!popup_view_ || !service_.popup_contents()) {
    return;
  }
  popup_view_->SetWebContents(service_.popup_contents());
  popup_view_->SetDisplayedOrigin(service_.displayed_origin());
  popup_view_->SetSplitAvailability(GetSplitAvailability());
}

void PopupOverlayController::OnPopupServiceWillDetach() {
  if (browser_ && service_.popup_contents()) {
    browser_->SetAsDelegateForAhoiPopupOverlay(service_.popup_contents(),
                                               /*set_delegate=*/false);
  }
  if (popup_view_) {
    popup_view_->SetWebContents(nullptr);
  }
}

void PopupOverlayController::OnPopupServiceFallbackRequested(
    popup::PopupFallbackReason reason) {
  // A committed-navigation observer must not reparent its WebContents while
  // Chromium is still unwinding that commit. Transfer on the next UI task.
  if (popup_view_) {
    popup_view_->ShowStatus(FallbackNotice(reason), /*is_error=*/false);
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<PopupOverlayController> controller,
             popup::PopupFallbackReason reason) {
            if (controller) {
              controller->OpenPopupInSeparateWindow(reason);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), reason),
      visual_style::kPopupFallbackNoticeDuration);
}

void PopupOverlayController::OnViewBoundsChanged(views::View* observed_view) {
  if (observed_view == contents_host_ || observed_view == opener_pane_) {
    UpdateOverlayBounds();
  }
}

void PopupOverlayController::OnViewIsDeleting(views::View* observed_view) {
  if (observed_view == opener_pane_) {
    opener_pane_observation_.Reset();
    opener_pane_ = nullptr;
    UpdateOverlayBounds();
    // MultiContentsView may replace a pane host while preserving the opener
    // WebContents. Re-resolve after deletion instead of leaving the overlay
    // bound to the whole window for the remainder of its lifetime.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<PopupOverlayController> controller) {
                         if (controller && controller->service_.IsShowing()) {
                           controller->ObserveOpenerPane();
                         }
                       },
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  if (observed_view != contents_host_) {
    return;
  }
  contents_host_observation_.Reset();
  opener_pane_observation_.Reset();
  ++dismissal_generation_;
  dismissal_pending_ = false;
  dismissal_restore_focus_ = false;
  popup_view_ = nullptr;
  opener_pane_ = nullptr;
  contents_host_ = nullptr;
  if (browser_ && service_.popup_contents()) {
    browser_->SetAsDelegateForAhoiPopupOverlay(service_.popup_contents(),
                                               /*set_delegate=*/false);
  }
  service_.ResetForShutdown();
  appearance_signal_source_.reset();
  ClearFocusReturnTarget();
  ClearRequestMetadata();
}

}  // namespace ahoi::popup_ui
