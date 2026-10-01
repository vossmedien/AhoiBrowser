// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_preset_menu_model.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/models/menu_model.h"

namespace ahoi::sidebar {

// static
std::unique_ptr<SidebarSplitPresetMenuModel>
SidebarSplitPresetMenuModel::CreateFor(TabStripModel* tab_strip_model,
                                       tabs::TabInterface* tab) {
  if (!tab_strip_model || !tab || !tab->GetSplit().has_value()) {
    return nullptr;
  }
  const int index = tab_strip_model->GetIndexOfTab(tab);
  const split_tabs::SplitTabData* const split_data =
      tab_strip_model->GetSplitData(*tab->GetSplit());
  if (index == TabStripModel::kNoTab || !split_data) {
    return nullptr;
  }
  const size_t pane_count = split_data->ListTabs().size();
  if (pane_count != 3u && pane_count != 4u) {
    return nullptr;
  }
  return base::WrapUnique(
      new SidebarSplitPresetMenuModel(tab_strip_model, tab, index));
}

SidebarSplitPresetMenuModel::SidebarSplitPresetMenuModel(
    TabStripModel* tab_strip_model,
    tabs::TabInterface* tab,
    int tab_index)
    : ui::SimpleMenuModel(this),
      tab_strip_model_(tab_strip_model),
      tab_(tab->GetWeakPtr()),
      tab_index_(tab_index),
      presets_(tab_strip_model,
               SplitTabMenuModel::MenuSource::kTabContextMenu,
               tab_index) {
  // The presets are SplitTabMenuModel's only check items: six for three
  // panes, the two 2x2 traversal orders for four.
  for (size_t index = 0; index < presets_.GetItemCount(); ++index) {
    if (presets_.GetTypeAt(index) == ui::MenuModel::TYPE_CHECK) {
      AddCheckItem(presets_.GetCommandIdAt(index), presets_.GetLabelAt(index));
    }
  }
}

SidebarSplitPresetMenuModel::~SidebarSplitPresetMenuModel() = default;

bool SidebarSplitPresetMenuModel::IsCurrent() const {
  return tab_ && tab_->GetSplit().has_value() &&
         tab_strip_model_->GetIndexOfTab(tab_.get()) == tab_index_;
}

bool SidebarSplitPresetMenuModel::IsCommandIdChecked(int command_id) const {
  return IsCurrent() && presets_.IsCommandIdChecked(command_id);
}

bool SidebarSplitPresetMenuModel::IsCommandIdEnabled(int command_id) const {
  return IsCurrent();
}

void SidebarSplitPresetMenuModel::ExecuteCommand(int command_id,
                                                 int event_flags) {
  if (IsCurrent()) {
    presets_.ExecuteCommand(command_id, event_flags);
  }
}

}  // namespace ahoi::sidebar
