// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_PRESET_MENU_MODEL_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_PRESET_MENU_MODEL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/split_tab_menu_model.h"
#include "ui/menus/simple_menu_model.h"

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace ahoi::sidebar {

// The sidebar row menu's "Arrange split view" submenu for a split of three
// or four panes (SPLIT-10, SPLIT-34). Ahoi hides Chromium's tab strip,
// toolbar split button and mini-toolbar menu, so the sidebar row is the
// only pointer surface for the layout presets. The submenu lists exactly
// the checked presets of Chromium's SplitTabMenuModel and forwards the
// check state and the command to it, so both menus share one layout
// implementation and neither replaces nor reloads a WebContents.
class SidebarSplitPresetMenuModel : public ui::SimpleMenuModel,
                                    public ui::SimpleMenuModel::Delegate {
 public:
  // Returns nullptr unless `tab` belongs to a split of three or four panes
  // in `tab_strip_model`; two-pane splits keep the row's own items.
  static std::unique_ptr<SidebarSplitPresetMenuModel> CreateFor(
      TabStripModel* tab_strip_model,
      tabs::TabInterface* tab);

  SidebarSplitPresetMenuModel(const SidebarSplitPresetMenuModel&) = delete;
  SidebarSplitPresetMenuModel& operator=(const SidebarSplitPresetMenuModel&) =
      delete;
  ~SidebarSplitPresetMenuModel() override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  SidebarSplitPresetMenuModel(TabStripModel* tab_strip_model,
                              tabs::TabInterface* tab,
                              int tab_index);

  // True while the row's tab is still a split member at the index the
  // presets were built for; a stale menu then does nothing.
  bool IsCurrent() const;

  raw_ptr<TabStripModel> tab_strip_model_;
  base::WeakPtr<tabs::TabInterface> tab_;
  const int tab_index_;
  SplitTabMenuModel presets_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_PRESET_MENU_MODEL_H_
