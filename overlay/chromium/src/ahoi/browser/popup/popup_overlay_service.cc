// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/popup_overlay_service.h"

#include <utility>

#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi::popup {

PopupOverlayService::PopupOverlayService(Observer* observer)
    : observer_(observer) {}

PopupOverlayService::~PopupOverlayService() {
  ResetOwnedContents(/*notify_observer=*/false);
}

bool PopupOverlayService::Adopt(
    content::WebContents* opener,
    std::unique_ptr<content::WebContents>* popup_contents) {
  if (!opener || !popup_contents || !*popup_contents || popup_contents_) {
    return false;
  }
  opener_ = opener->GetWeakPtr();
  popup_contents_ = std::move(*popup_contents);
  fallback_request_pending_ = false;
  Observe(popup_contents_.get());
  if (observer_) {
    observer_->OnPopupServiceStateChanged();
  }
  return true;
}

bool PopupOverlayService::RestoreAfterRejectedTransfer(
    content::WebContents* opener,
    std::unique_ptr<content::WebContents>* popup_contents) {
  if (!popup_contents || !*popup_contents || popup_contents_) {
    return false;
  }
  opener_ =
      opener ? opener->GetWeakPtr() : base::WeakPtr<content::WebContents>();
  popup_contents_ = std::move(*popup_contents);
  fallback_request_pending_ = false;
  Observe(popup_contents_.get());
  if (observer_) {
    observer_->OnPopupServiceStateChanged();
  }
  return true;
}

bool PopupOverlayService::OwnsContents(
    const content::WebContents* contents) const {
  return contents && popup_contents_.get() == contents;
}

url::Origin PopupOverlayService::displayed_origin() const {
  if (!popup_contents_) {
    return url::Origin();
  }
  const url::Origin popup_origin =
      popup_contents_->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  if (!popup_origin.opaque()) {
    return popup_origin;
  }
  if (opener_) {
    return opener_->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  }
  return popup_origin;
}

void PopupOverlayService::RequestClose() {
  if (popup_contents_) {
    popup_contents_->ClosePage();
  }
}

bool PopupOverlayService::CloseOwnedContents(content::WebContents* contents) {
  if (!OwnsContents(contents)) {
    return false;
  }
  ResetOwnedContents(/*notify_observer=*/true);
  return true;
}

bool PopupOverlayService::HandleBeforeUnloadFired(
    content::WebContents* contents,
    bool proceed,
    bool* proceed_to_fire_unload) {
  if (!OwnsContents(contents) || !proceed_to_fire_unload) {
    return false;
  }
  *proceed_to_fire_unload = proceed;
  return true;
}

std::unique_ptr<content::WebContents>
PopupOverlayService::ReleaseForTransfer() {
  if (!popup_contents_) {
    return nullptr;
  }
  if (observer_) {
    observer_->OnPopupServiceWillDetach();
  }
  Observe(nullptr);
  fallback_request_pending_ = false;
  opener_.reset();
  return std::move(popup_contents_);
}

void PopupOverlayService::ResetForShutdown() {
  ResetOwnedContents(/*notify_observer=*/false);
  observer_ = nullptr;
}

void PopupOverlayService::RequestNativeFallback(PopupFallbackReason reason) {
  if (!popup_contents_ || fallback_request_pending_) {
    return;
  }
  fallback_request_pending_ = true;
  if (observer_) {
    observer_->OnPopupServiceFallbackRequested(reason);
  }
}

void PopupOverlayService::ResetOwnedContents(bool notify_observer) {
  if (!popup_contents_) {
    opener_.reset();
    return;
  }
  if (notify_observer && observer_) {
    observer_->OnPopupServiceWillDetach();
  }
  Observe(nullptr);
  popup_contents_->SetDelegate(nullptr);
  popup_contents_.reset();
  opener_.reset();
  fallback_request_pending_ = false;
  if (notify_observer && observer_) {
    observer_->OnPopupServiceStateChanged();
  }
}

void PopupOverlayService::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (!navigation || !navigation->HasCommitted() ||
      !navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument() ||
      !popup_contents_) {
    return;
  }

  if (observer_) {
    observer_->OnPopupServiceStateChanged();
  }

  blink::mojom::WindowFeatures default_features;
  const PopupSafety safety = ClassifyPopupForOverlay(
      navigation->GetURL(), WindowOpenDisposition::NEW_POPUP, default_features);
  if (!IsOverlayEligible(safety)) {
    RequestNativeFallback(FallbackReasonForSafety(safety));
  }
}

}  // namespace ahoi::popup
