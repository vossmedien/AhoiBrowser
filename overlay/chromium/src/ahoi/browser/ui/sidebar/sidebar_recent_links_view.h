// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RECENT_LINKS_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RECENT_LINKS_VIEW_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

namespace views {
class View;
}

namespace ahoi::sidebar {

struct RecentGroupLink {
  base::Uuid node_id;
  std::u16string title;
  GURL url;
  base::Time last_visit;
  ui::ImageModel favicon;
};

using ActivateRecentGroupLinkCallback =
    base::RepeatingCallback<void(const base::Uuid&)>;
using RecentGroupLinksHoverCallback = base::RepeatingCallback<void(bool)>;

std::unique_ptr<views::View> CreateGroupRecentLinksView(
    std::vector<RecentGroupLink> links,
    ActivateRecentGroupLinkCallback activate_callback,
    RecentGroupLinksHoverCallback hover_callback);

void UpdateGroupRecentLinkFavicon(views::View* view,
                                  const GURL& url,
                                  ui::ImageModel favicon);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RECENT_LINKS_VIEW_H_
