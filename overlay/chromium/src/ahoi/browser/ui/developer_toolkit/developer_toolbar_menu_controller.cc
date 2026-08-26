// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_toolbar_menu_controller.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/check.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {

DeveloperToolbarMenuController::DeveloperToolbarMenuController(
    PrefService* prefs)
    : prefs_(prefs) {
  CHECK(prefs_);
}

DeveloperToolbarMenuController::~DeveloperToolbarMenuController() = default;

void DeveloperToolbarMenuController::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!source || !source->GetWidget()) {
    return;
  }
  menu_model_ = BuildMenuModel();
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool DeveloperToolbarMenuController::IsCommandIdChecked(int command_id) const {
  const auto visibility =
      developer_toolkit_prefs::GetToolbarVisibility(*prefs_);
  switch (command_id) {
    case kToggleCookies:
      return visibility.cookie;
    case kToggleCache:
      return visibility.cache;
    case kToggleToolkit:
      return visibility.toolkit;
  }
  return false;
}

bool DeveloperToolbarMenuController::IsCommandIdEnabled(int command_id) const {
  return command_id == kToggleCookies || command_id == kToggleCache ||
         command_id == kToggleToolkit;
}

void DeveloperToolbarMenuController::ExecuteCommand(int command_id,
                                                    int /*event_flags*/) {
  auto visibility = developer_toolkit_prefs::GetToolbarVisibility(*prefs_);
  switch (command_id) {
    case kToggleCookies:
      visibility.cookie = !visibility.cookie;
      break;
    case kToggleCache:
      visibility.cache = !visibility.cache;
      break;
    case kToggleToolkit:
      visibility.toolkit = !visibility.toolkit;
      break;
    default:
      return;
  }
  developer_toolkit_prefs::SetToolbarVisibility(*prefs_, visibility);
}

std::unique_ptr<ui::SimpleMenuModel>
DeveloperToolbarMenuController::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddCheckItem(kToggleCookies, l10n_util::GetStringUTF16(
                                          IDS_AHOI_DEVELOPER_TOOLBAR_COOKIES));
  model->AddCheckItem(kToggleCache, l10n_util::GetStringUTF16(
                                        IDS_AHOI_DEVELOPER_TOOLBAR_CACHE));
  model->AddCheckItem(kToggleToolkit, l10n_util::GetStringUTF16(
                                          IDS_AHOI_DEVELOPER_TOOLBAR_HELPERS));
  return model;
}

}  // namespace ahoi
