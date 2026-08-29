// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
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
class Button;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace ahoi::sidebar {

class SidebarDiscoveryResultRow;

// Native, focusable sidebar discovery container. It permanently owns the
// normal tab-tree surface and filters that surface in place, so hierarchy,
// expansion, scroll, previews and drag state survive every search session.
class SidebarDiscoveryView final : public views::View,
                                   public views::TextfieldController,
                                   public SidebarDiscoveryModelObserver {
  METADATA_HEADER(SidebarDiscoveryView, views::View)

 public:
  // Navigation of results that remain in the normal sidebar hierarchy. The
  // host owns those rows, so discovery delegates selection without coupling
  // this view to the tree, temporary-tab, or device-tab implementations.
  enum class PrimaryResultAction {
    kClearSelection,
    kSelectFirst,
    kSelectLast,
    kSelectNext,
    kSelectPrevious,
    kActivateSelection,
  };

  using ActivateCommandCallback =
      base::RepeatingCallback<bool(const CommandItem&)>;
  using RestoreCallback = base::RepeatingCallback<bool(SessionID)>;
  using CloseCallback = base::RepeatingClosure;
  // Filters `primary_surface` without rebuilding it and returns the identities
  // that it consumed inline. The discovery view can then keep only search
  // sources that have no native representation in its compact supplemental
  // result list.
  using FilterCallback = base::RepeatingCallback<std::set<std::string>(
      const std::u16string&,
      const std::vector<SidebarDiscoveryItem>&)>;
  // Returns true when the requested action was handled. Selection actions
  // must return false at the corresponding edge so keyboard navigation can
  // continue into the supplemental result sections below the primary surface.
  // kClearSelection only clears transient search highlighting; it must not
  // mutate the persistent tree selection or synchronously refresh discovery.
  using PrimaryResultCallback =
      base::RepeatingCallback<bool(PrimaryResultAction)>;

  SidebarDiscoveryView(SidebarDiscoveryModel* model,
                       std::unique_ptr<views::View> primary_surface,
                       FilterCallback filter_callback,
                       PrimaryResultCallback primary_result_callback,
                       ActivateCommandCallback activate_command_callback,
                       RestoreCallback restore_callback,
                       CloseCallback close_callback);
  SidebarDiscoveryView(const SidebarDiscoveryView&) = delete;
  SidebarDiscoveryView& operator=(const SidebarDiscoveryView&) = delete;
  ~SidebarDiscoveryView() override;

  void Open();
  void Close();
  bool is_open() const { return is_open_; }
  void Reset();
  bool CloseOrClear();
  // The host rebuilds primary rows asynchronously when tab or sync state
  // changes. Drop the view-side navigation latch before those row identities
  // become invalid so Enter and the next arrow key cannot target stale state.
  void InvalidatePrimaryResultSelection();

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
  bool RunPrimaryResultAction(PrimaryResultAction action);
  void ClearPrimarySelection();
  bool HandleResultKeyEvent(const std::string& stable_id,
                            const ui::KeyEvent& event);
  void OnResultHovered(const std::string& stable_id);
  void ScheduleAcceptStableId(std::string stable_id);
  void AcceptStableId(const std::string& stable_id);
  std::optional<size_t> FindItemIndex(const std::string& stable_id) const;

  const raw_ptr<SidebarDiscoveryModel> model_;
  const FilterCallback filter_callback_;
  const PrimaryResultCallback primary_result_callback_;
  const ActivateCommandCallback activate_command_callback_;
  const RestoreCallback restore_callback_;
  const CloseCallback close_callback_;
  raw_ptr<views::View> input_shell_ = nullptr;
  raw_ptr<views::View> primary_surface_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::Button> close_button_ = nullptr;
  raw_ptr<views::Label> no_results_label_ = nullptr;
  raw_ptr<views::View> supplemental_section_ = nullptr;
  raw_ptr<views::Label> supplemental_section_label_ = nullptr;
  raw_ptr<views::View> supplemental_results_container_ = nullptr;
  raw_ptr<views::ScrollView> supplemental_results_scroll_view_ = nullptr;
  raw_ptr<views::View> recently_closed_section_ = nullptr;
  raw_ptr<views::View> recently_closed_results_container_ = nullptr;
  raw_ptr<views::ScrollView> recently_closed_results_scroll_view_ = nullptr;
  std::vector<SidebarDiscoveryItem> items_;
  std::vector<raw_ptr<SidebarDiscoveryResultRow>> rows_;
  size_t supplemental_result_count_ = 0;
  std::optional<size_t> selected_index_;
  bool primary_selection_active_ = false;
  bool suppress_contents_refresh_ = false;
  bool is_open_ = false;
  base::WeakPtrFactory<SidebarDiscoveryView> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_VIEW_H_
