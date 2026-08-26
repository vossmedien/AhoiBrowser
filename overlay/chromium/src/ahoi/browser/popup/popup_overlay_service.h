// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_POPUP_OVERLAY_SERVICE_H_
#define AHOI_BROWSER_POPUP_POPUP_OVERLAY_SERVICE_H_

#include <memory>

#include "ahoi/browser/popup/popup_types.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/origin.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace ahoi::popup {

// Browser-neutral owner for one overlaid Chromium WebContents. It neither
// creates a second renderer stack nor owns Views. The window adapter takes the
// same WebContents only for promotion, split insertion, or native fallback.
class PopupOverlayService final : public content::WebContentsObserver {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void OnPopupServiceStateChanged() = 0;
    virtual void OnPopupServiceWillDetach() = 0;
    virtual void OnPopupServiceFallbackRequested(
        PopupFallbackReason reason) = 0;
  };

  explicit PopupOverlayService(Observer* observer);
  PopupOverlayService(const PopupOverlayService&) = delete;
  PopupOverlayService& operator=(const PopupOverlayService&) = delete;
  ~PopupOverlayService() override;

  // Consumes `*popup_contents` only on success. This keeps Browser's native
  // popup fallback in full ownership when preflight or adoption is rejected.
  bool Adopt(content::WebContents* opener,
             std::unique_ptr<content::WebContents>* popup_contents);
  // Split rollback may race with opener destruction. Unlike initial adoption,
  // restoration therefore accepts a null opener and keeps the exact popup
  // available for close or tab promotion instead of silently dropping it.
  bool RestoreAfterRejectedTransfer(
      content::WebContents* opener,
      std::unique_ptr<content::WebContents>* popup_contents);

  bool IsShowing() const { return popup_contents_ != nullptr; }
  bool OwnsContents(const content::WebContents* contents) const;
  content::WebContents* popup_contents() const { return popup_contents_.get(); }
  content::WebContents* opener() const { return opener_.get(); }
  url::Origin displayed_origin() const;

  void RequestClose();
  bool CloseOwnedContents(content::WebContents* contents);
  bool HandleBeforeUnloadFired(content::WebContents* contents,
                               bool proceed,
                               bool* proceed_to_fire_unload);

  // Notifies the observer before ownership changes so a Views::WebView can
  // detach synchronously. The returned WebContents is never navigated or
  // recreated by this service.
  std::unique_ptr<content::WebContents> ReleaseForTransfer();

  // Called during BrowserView teardown. Normal renderer crashes intentionally
  // do not call this: Chromium's crashed-page state remains visible and can be
  // promoted or closed like any other WebContents.
  void ResetForShutdown();

  void RequestNativeFallback(PopupFallbackReason reason);

 private:
  void ResetOwnedContents(bool notify_observer);

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation) override;

  raw_ptr<Observer> observer_ = nullptr;
  base::WeakPtr<content::WebContents> opener_;
  std::unique_ptr<content::WebContents> popup_contents_;
  bool fallback_request_pending_ = false;
};

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_POPUP_OVERLAY_SERVICE_H_
