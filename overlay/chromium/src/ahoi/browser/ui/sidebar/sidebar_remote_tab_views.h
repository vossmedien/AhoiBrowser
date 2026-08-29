// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_REMOTE_TAB_VIEWS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_REMOTE_TAB_VIEWS_H_

#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_model.h"
#include "base/functional/callback.h"
#include "ui/base/models/image_model.h"

namespace views {
class View;
}

namespace ahoi::sidebar {

struct RemoteTabRowModel {
  sync::RemoteTabRecord tab;
  sync::DeviceType device_type = sync::DeviceType::kOther;
  std::u16string device_name;
  std::u16string workspace_name;
  std::u16string relative_activity;
  std::u16string remote_status;
  ui::ImageModel favicon;
  bool remote_actions_available = false;
};

struct RemoteTabRowActions {
  base::RepeatingCallback<void(sync::RemoteTabRecord)> open_here;
  base::RepeatingCallback<void(sync::RemoteTabRecord)> take_over;
  // A remote focus callback is intentionally optional. Callers must supply it
  // only when the command has a provisioned signing identity and the target
  // device is current; the row never falls back to an unsigned request.
  base::RepeatingCallback<void(sync::RemoteTabRecord)> focus_remote;
};

// Compact native row shown directly in the unified sidebar. It deliberately
// is not a link to a separate management page: one click adopts the URL into
// the current desktop browsing session.
std::unique_ptr<views::View> CreateRemoteTabRowView(
    RemoteTabRowModel model,
    RemoteTabRowActions actions);
std::optional<sync::RemoteTabRecord> GetRemoteTabForView(views::View* view);
void SetRemoteTabSearchSelected(views::View* view, bool selected);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_REMOTE_TAB_VIEWS_H_
