// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_

#include <cstddef>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "ahoi/browser/importer/arc/arc_split_receipt.h"
#include "base/memory/weak_ptr.h"

class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}

namespace ahoi {
class SessionBridge;
}

namespace ahoi::importer::arc {

struct ArcSplitRuntimeResult {
  ArcImportStatus status = ArcImportStatus::kRuntimeFailed;
  size_t reconstructed_split_count = 0;
  size_t approximated_four_pane_ratio_count = 0;
  std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs;
};

// Read-only classification of the live model. A completely matching split is
// exact. Missing tabs and correctly bound but unsplit members are repairable;
// partial, foreign, differently ordered, or visually different splits are a
// conflict. Unavailable is reserved for a missing runtime observation seam.
// When require_focus is true, exact additionally requires the focused member of
// the final source descriptor to be the active tab inside the target window.
// Operating-system window/app activation is deliberately outside the receipt:
// a long-running import must neither steal focus nor fail when the user changes
// apps while it commits.
ArcSplitVerification VerifyArcSplitRuntime(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan,
                                           bool require_focus = false);

// Opens only missing saved pages that belong to validated Arc descriptors,
// binds each real Chromium tab to its durable tree node, and reconstructs only
// repairable native splits in source order. Existing exact splits are reused;
// partial or foreign split state fails closed before mutation.
ArcSplitRuntimeResult ReconstructArcSplits(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan);

void CloseArcImportRuntimeTabs(
    std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs);

namespace internal {

// Pure focus classifier shared with the focused unit test. Missing focused-tab
// binding in the target window is repairable runtime state, never an exact
// receipt. Operating-system window activation is intentionally not an input.
ArcSplitVerification ClassifyArcSplitFocus(bool focused_tab_present,
                                           bool focused_tab_in_target_window,
                                           bool focused_tab_active);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_
