// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_CONTROLLER_H_
#define AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_CONTROLLER_H_

#include <cstdint>
#include <memory>

#include "ahoi/browser/popup/popup_overlay_service.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom-forward.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
}  // namespace views

class Browser;
class GURL;

namespace ahoi::appearance {
class AppearanceRuntimeSignalSource;
struct GlassPolicy;
}  // namespace ahoi::appearance

namespace ahoi::popup_ui {

class PopupOverlayView;

// Window adapter for PopupOverlayService. Views, Browser, TabStripModel and
// native-window fallback are kept here so the service remains independently
// testable and owns exactly one WebContents lifecycle.
class PopupOverlayController final
    : public views::ViewObserver,
      public popup::PopupOverlayService::Observer {
 public:
  using OpenerPaneProvider =
      base::RepeatingCallback<views::View*(content::WebContents*)>;

  PopupOverlayController(Browser* browser,
                         views::View* contents_host,
                         OpenerPaneProvider opener_pane_provider);
  PopupOverlayController(const PopupOverlayController&) = delete;
  PopupOverlayController& operator=(const PopupOverlayController&) = delete;
  ~PopupOverlayController() override;

  // Takes ownership only for a conservative, user-initiated web popup in a
  // normal Ahoi browser window. False leaves Chromium's native popup path
  // completely intact and does not consume `*popup_contents`.
  bool TryShow(content::WebContents* opener,
               std::unique_ptr<content::WebContents>* popup_contents,
               const GURL& target_url,
               WindowOpenDisposition disposition,
               const blink::mojom::WindowFeatures& window_features,
               bool user_gesture);

  bool IsShowing() const { return service_.IsShowing(); }
  bool OwnsContents(const content::WebContents* contents) const;
  bool ActivateOwnedContents(content::WebContents* contents);
  bool CloseOwnedContents(content::WebContents* contents);
  bool HandleBeforeUnloadFired(content::WebContents* contents,
                               bool proceed,
                               bool* proceed_to_fire_unload);

  // Returns true when an owned popup was moved onto Chromium's native window
  // path. Browser uses this before an unsupported content-fullscreen request.
  bool OpenOwnedContentsInSeparateWindow(content::WebContents* contents,
                                         popup::PopupFallbackReason reason);

  // Keeps child-window semantics when the overlay's anchor tab closes.
  void OnContentsWillClose(content::WebContents* contents);

  views::View* overlay_view_for_testing() const;
  PopupOverlayView* popup_view_for_testing() const { return popup_view_; }
  content::WebContents* popup_contents_for_testing() const {
    return service_.popup_contents();
  }

 private:
  void CreateOverlayView();
  void RequestClosePopup();
  void PromotePopupToTab();
  void SplitPopupWithOpener();
  bool OpenPopupInSeparateWindow(popup::PopupFallbackReason reason);
  popup::PopupSplitAvailability GetSplitAvailability() const;
  void ShowSplitRejection(popup::PopupSplitAvailability availability);
  void ObserveOpenerPane();
  void DismissOverlayView(bool animate, bool restore_focus);
  void FinishOverlayDismissal(uint64_t generation);
  void RemoveOverlayViewImmediately();
  void CaptureFocusReturnTarget(content::WebContents* opener);
  void ClearFocusReturnTarget();
  void ScheduleFocusRestore();
  void RestoreFocus();
  void UpdateOverlayBounds();
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);
  void ClearRequestMetadata();

  // popup::PopupOverlayService::Observer:
  void OnPopupServiceStateChanged() override;
  void OnPopupServiceWillDetach() override;
  void OnPopupServiceFallbackRequested(
      popup::PopupFallbackReason reason) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::View> contents_host_ = nullptr;
  OpenerPaneProvider opener_pane_provider_;
  raw_ptr<views::View> opener_pane_ = nullptr;
  popup::PopupOverlayService service_{this};
  raw_ptr<PopupOverlayView> popup_view_ = nullptr;
  blink::mojom::WindowFeaturesPtr initial_window_features_;
  bool original_user_gesture_ = false;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;

  base::ScopedObservation<views::View, views::ViewObserver>
      contents_host_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver>
      opener_pane_observation_{this};
  views::ViewTracker focus_return_view_tracker_;
  base::WeakPtr<content::WebContents> focus_return_contents_;
  uint64_t dismissal_generation_ = 0;
  bool dismissal_pending_ = false;
  bool dismissal_restore_focus_ = false;
  base::WeakPtrFactory<PopupOverlayController> weak_ptr_factory_{this};
};

}  // namespace ahoi::popup_ui

#endif  // AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_CONTROLLER_H_
