// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_

#include <cstddef>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
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

// Opens only the saved pages that belong to validated Arc split descriptors,
// binds each real Chromium tab to its durable tree node, and reconstructs the
// native split in source order. A failure closes every tab opened by this
// operation, which also removes any split membership created along the way.
ArcSplitRuntimeResult ReconstructArcSplits(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan);

void CloseArcImportRuntimeTabs(
    std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_SPLIT_RUNTIME_H_
