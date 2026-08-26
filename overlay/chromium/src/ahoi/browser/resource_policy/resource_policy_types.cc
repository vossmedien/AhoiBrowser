// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/resource_policy/resource_policy_types.h"

namespace ahoi::resource_policy {

SleepBlockReason GetPrimaryBlockReason(const CriticalSignals& signals) {
  if (signals.active_pane) {
    return SleepBlockReason::kActivePane;
  }
  if (signals.visible_pane) {
    return SleepBlockReason::kVisiblePane;
  }
  if (signals.audible) {
    return SleepBlockReason::kAudible;
  }
  if (signals.recently_audible) {
    return SleepBlockReason::kRecentlyAudible;
  }
  if (signals.media_session) {
    return SleepBlockReason::kMediaSession;
  }
  if (signals.picture_in_picture) {
    return SleepBlockReason::kPictureInPicture;
  }
  if (signals.capture) {
    return SleepBlockReason::kCapture;
  }
  if (signals.download) {
    return SleepBlockReason::kDownload;
  }
  if (signals.upload) {
    return SleepBlockReason::kUpload;
  }
  if (signals.unsaved_form) {
    return SleepBlockReason::kUnsavedForm;
  }
  if (signals.before_unload) {
    return SleepBlockReason::kBeforeUnload;
  }
  if (signals.devtools) {
    return SleepBlockReason::kDevTools;
  }
  if (signals.http_auth) {
    return SleepBlockReason::kHttpAuth;
  }
  if (signals.permission_prompt) {
    return SleepBlockReason::kPermissionPrompt;
  }
  if (signals.file_chooser) {
    return SleepBlockReason::kFileChooser;
  }
  if (signals.modal_flow) {
    return SleepBlockReason::kModalFlow;
  }
  if (signals.product_protection) {
    return SleepBlockReason::kProductProtection;
  }
  if (signals.never_sleep) {
    return SleepBlockReason::kNeverSleep;
  }
  if (signals.enterprise_policy) {
    return SleepBlockReason::kEnterprisePolicy;
  }
  if (signals.upstream_protected) {
    return SleepBlockReason::kUpstreamPolicy;
  }
  return SleepBlockReason::kNone;
}

bool HasAutomaticAhoiProtection(const CriticalSignals& signals) {
  // Active/visible, audible/recently-audible, PiP, capture, unsaved form,
  // DevTools, per-site exceptions and enterprise constraints are already
  // native DiscardEligibilityPolicy inputs. The states below close product
  // gaps without creating a parallel discard scheduler.
  return signals.media_session || signals.download || signals.upload ||
         signals.before_unload || signals.http_auth ||
         signals.permission_prompt || signals.file_chooser ||
         signals.modal_flow || signals.product_protection;
}

std::string_view ToString(TabLifecycleState state) {
  switch (state) {
    case TabLifecycleState::kAwake:
      return "awake";
    case TabLifecycleState::kSleeping:
      return "sleeping";
    case TabLifecycleState::kWaking:
      return "waking";
    case TabLifecycleState::kUnavailable:
      return "unavailable";
  }
  return "unavailable";
}

std::string_view ToString(SleepBlockReason reason) {
  switch (reason) {
    case SleepBlockReason::kNone:
      return "none";
    case SleepBlockReason::kActivePane:
      return "active-pane";
    case SleepBlockReason::kVisiblePane:
      return "visible-pane";
    case SleepBlockReason::kAudible:
      return "audible";
    case SleepBlockReason::kRecentlyAudible:
      return "recently-audible";
    case SleepBlockReason::kMediaSession:
      return "media-session";
    case SleepBlockReason::kPictureInPicture:
      return "picture-in-picture";
    case SleepBlockReason::kCapture:
      return "capture";
    case SleepBlockReason::kDownload:
      return "download";
    case SleepBlockReason::kUpload:
      return "upload";
    case SleepBlockReason::kUnsavedForm:
      return "unsaved-form";
    case SleepBlockReason::kBeforeUnload:
      return "before-unload";
    case SleepBlockReason::kDevTools:
      return "devtools";
    case SleepBlockReason::kHttpAuth:
      return "http-auth";
    case SleepBlockReason::kPermissionPrompt:
      return "permission-prompt";
    case SleepBlockReason::kFileChooser:
      return "file-chooser";
    case SleepBlockReason::kModalFlow:
      return "modal-flow";
    case SleepBlockReason::kProductProtection:
      return "product-protection";
    case SleepBlockReason::kNeverSleep:
      return "never-sleep";
    case SleepBlockReason::kEnterprisePolicy:
      return "enterprise-policy";
    case SleepBlockReason::kRecentlyVisible:
      return "recently-visible";
    case SleepBlockReason::kUpstreamPolicy:
      return "upstream-policy";
    case SleepBlockReason::kUnavailable:
      return "unavailable";
  }
  return "unavailable";
}

}  // namespace ahoi::resource_policy
