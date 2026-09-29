// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "ahoi/browser/ui/split_drop/split_drop_controller.h"
#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::split_drop {

void SplitDropController::ClearOverlayIntent() {
  preview_intent_.reset();
  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->ClearIntent();
  }
}

void SplitDropController::EndOverlayPresentation() {
  preview_intent_.reset();
  if (overlay_view_tracker_) {
    static_cast<SplitDropOverlayView*>(overlay_view_tracker_.view())
        ->EndDragPresentation();
  }
}

}  // namespace ahoi::split_drop
