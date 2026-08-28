// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace ui {
class KeyEvent;
}

namespace views {
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace ahoi::sidebar {

class SidebarDiscoveryResultRow;

// Native, focusable sidebar discovery surface. The host swaps it with the
// normal tab-tree surface instead of rebuilding that tree, so selection,
// expansion, scroll, previews and drag state survive every search session.
class SidebarDiscoveryView final : public views::View,
                                   public views::TextfieldController,
                                   public SidebarDiscoveryModelObserver {
  METADATA_HEADER(SidebarDiscoveryView, views::View)

 public:
  using ActivateCommandCallback =
      base::RepeatingCallback<bool(const CommandItem&)>;
  using RestoreCallback = base::RepeatingCallback<bool(SessionID)>;
  using CloseCallback = base::RepeatingClosure;

  SidebarDiscoveryView(SidebarDiscoveryModel* model,
                       ActivateCommandCallback activate_command_callback,
                       RestoreCallback restore_callback,
                       CloseCallback close_callback);
  SidebarDiscoveryView(const SidebarDiscoveryView&) = delete;
  SidebarDiscoveryView& operator=(const SidebarDiscoveryView&) = delete;
  ~SidebarDiscoveryView() override;

  void Open();
  void Reset();
  bool CloseOrClear();

  // Semantic test seams; callers never need to depend on child order.
  views::Textfield* search_field_for_testing() { return search_field_; }
  size_t result_count_for_testing() const { return items_.size(); }
  std::optional<size_t> selected_index_for_testing() const {
    return selected_index_;
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // SidebarDiscoveryModelObserver:
  void OnSidebarDiscoveryModelChanged() override;

 private:
  void RefreshResults();
  void SelectIndex(std::optional<size_t> index, bool request_focus);
  bool MoveSelection(int delta, bool request_focus);
  bool AcceptSelection();
  bool HandleResultKeyEvent(const std::string& stable_id,
                            const ui::KeyEvent& event);
  void OnResultHovered(const std::string& stable_id);
  void ScheduleAcceptStableId(std::string stable_id);
  void AcceptStableId(const std::string& stable_id);
  std::optional<size_t> FindItemIndex(const std::string& stable_id) const;

  const raw_ptr<SidebarDiscoveryModel> model_;
  const ActivateCommandCallback activate_command_callback_;
  const RestoreCallback restore_callback_;
  const CloseCallback close_callback_;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::Label> section_label_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;
  raw_ptr<views::ScrollView> results_scroll_view_ = nullptr;
  std::vector<SidebarDiscoveryItem> items_;
  std::vector<raw_ptr<SidebarDiscoveryResultRow>> rows_;
  std::optional<size_t> selected_index_;
  bool suppress_contents_refresh_ = false;
  base::WeakPtrFactory<SidebarDiscoveryView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_
