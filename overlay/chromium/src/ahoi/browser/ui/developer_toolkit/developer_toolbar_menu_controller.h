// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLBAR_MENU_CONTROLLER_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLBAR_MENU_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"

class PrefService;

namespace views {
class MenuRunner;
}

namespace ahoi {

// Shared context menu for every developer location-bar action. It provides a
// recovery path even when the main toolkit button itself is hidden.
class DeveloperToolbarMenuController final
    : public views::ContextMenuController,
      public ui::SimpleMenuModel::Delegate {
 public:
  explicit DeveloperToolbarMenuController(PrefService* prefs);
  DeveloperToolbarMenuController(const DeveloperToolbarMenuController&) =
      delete;
  DeveloperToolbarMenuController& operator=(
      const DeveloperToolbarMenuController&) = delete;
  ~DeveloperToolbarMenuController() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  enum CommandId {
    kToggleCookies = 1,
    kToggleCache = 2,
    kToggleToolkit = 3,
  };

  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();

  raw_ptr<PrefService> prefs_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLBAR_MENU_CONTROLLER_H_
