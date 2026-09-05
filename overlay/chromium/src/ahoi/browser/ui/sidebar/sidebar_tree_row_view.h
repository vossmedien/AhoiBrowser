// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_ROW_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_ROW_VIEW_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/memory/raw_ptr.h"
#include "base/uuid.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Textfield;
}  // namespace views

namespace ahoi::sidebar {

class SidebarTabTitleLabel;
class SidebarTreeView;

// A recycled visual row. It never owns model data and is rebound by UUID when
// it enters the viewport.
class SidebarTreeRowView final : public views::View,
                                 public views::TextfieldController {
  METADATA_HEADER(SidebarTreeRowView, views::View)

 public:
  static constexpr int kRowHeight = visual_style::kSidebarTabRowHeight;
  static constexpr int kIndentWidth = visual_style::kTreeIndent;

  SidebarTreeRowView(SidebarTreeView* owner, std::u16string split_with_prefix);
  SidebarTreeRowView(const SidebarTreeRowView&) = delete;
  SidebarTreeRowView& operator=(const SidebarTreeRowView&) = delete;
  ~SidebarTreeRowView() override;

  void Bind(size_t row_index,
            const SidebarTreeViewModel::Row& row,
            const tab_tree::TreeNode& node,
            bool selected,
            size_t split_segment_index = 0,
            size_t split_segment_count = 1,
            ui::ImageModel page_icon = ui::ImageModel(),
            ui::ImageModel media_indicator = ui::ImageModel(),
            std::u16string status_text = {},
            std::vector<gfx::ImageSkia> drag_thumbnails = {},
            bool running = false,
            bool sleeping = false);
  void Unbind();
  void SetSelected(bool selected);
  void SetDropPosition(
      std::optional<SidebarTreeController::DropPosition> position);
  void SetSplitDropTarget(bool split_drop_target);
  // Clips a split segment to the one shared group bubble. The bounds are in
  // the tree parent's coordinate space; ordinary rows pass std::nullopt to
  // restore their independent rounded surface.
  void SetSplitGroupClipBounds(std::optional<gfx::Rect> group_bounds);
  // Retained solely for the bounded fold-out animation after model removal.
  // No pointer, keyboard, accessibility or drag action may target this row.
  void SetExiting(bool exiting);
  bool is_exiting() const { return exiting_; }
  gfx::ImageSkia GetDragImage();
  void SetIsDragging(bool dragging);
  void StartEditing();
  void StopEditing(bool restore_model_title);

  const base::Uuid& node_id() const { return node_id_; }
  size_t row_index() const { return row_index_; }
  bool is_bound() const { return node_id_.is_valid(); }
  bool is_editing() const { return is_editing_; }
  bool is_folder() const { return type_ == tab_tree::TreeNodeType::kFolder; }
  bool expanded() const { return expanded_; }
  bool is_dragging_for_presentation() const { return dragging_; }
  bool is_native_drag_in_progress() const { return InDrag(); }
  bool is_split_segment_for_testing() const { return split_segment_count_ > 1; }
  bool is_split_drop_target_for_testing() const { return split_drop_target_; }
  bool disclosure_visible_for_testing() const;
  bool uses_open_folder_icon() const {
    return is_folder() && expanded_ && !folder_navigation_result_;
  }
  bool uses_open_folder_icon_for_testing() const {
    return uses_open_folder_icon();
  }
  bool title_visible_for_testing() const;
  gfx::Rect title_bounds_for_testing() const;
  gfx::Rect title_paint_bounds_for_testing() const;
  gfx::Rect title_paint_clip_bounds_for_testing() const;
  bool should_paint_trailing_state_for_testing() const {
    return ShouldPaintTrailingState();
  }
  const std::u16string& title() const { return title_; }
  std::u16string editor_text_for_testing() const;
  bool IsTrailingActionAt(const gfx::Point& point) const;
  std::optional<SidebarTreeController::DropPosition> drop_position_for_testing()
      const {
    return drop_position_;
  }

  // views::View:
  void Layout(PassKey) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnDragDone() override;
  bool HandleAccessibleAction(const ui::AXActionData& action_data) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  gfx::Rect DisclosureBounds() const;
  gfx::Rect IconBounds() const;
  gfx::Rect TitleBounds() const;
  gfx::Rect MediaIndicatorBounds() const;
  gfx::Rect TrailingActionBounds() const;
  void UpdateTitleBounds();
  bool ShouldShowTrailingAction() const;
  bool ShouldPaintTrailingState() const;
  void UpdateAccessibility();
  void UpdateSplitGroupClipPath();

  const raw_ptr<SidebarTreeView> owner_;
  const std::u16string split_with_prefix_;
  raw_ptr<SidebarTabTitleLabel> title_label_ = nullptr;
  raw_ptr<views::Textfield> editor_ = nullptr;
  base::Uuid node_id_;
  size_t row_index_ = 0;
  size_t depth_ = 0;
  size_t position_in_parent_ = 0;
  size_t sibling_count_ = 0;
  tab_tree::TreeNodeType type_ = tab_tree::TreeNodeType::kFolder;
  std::u16string title_;
  std::u16string folder_icon_id_;
  ui::ImageModel folder_emblem_;
  std::u16string folder_glyph_;
  std::optional<uint32_t> accent_argb_;
  ui::ImageModel page_icon_;
  ui::ImageModel media_indicator_;
  std::u16string status_text_;
  std::vector<gfx::ImageSkia> drag_thumbnails_;
  bool expanded_ = false;
  bool selected_ = false;
  bool hovered_ = false;
  bool running_ = false;
  bool sleeping_ = false;
  bool folder_navigation_result_ = false;
  bool is_editing_ = false;
  bool pressed_disclosure_ = false;
  bool pressed_trailing_action_ = false;
  bool split_drop_target_ = false;
  bool dragging_ = false;
  bool exiting_ = false;
  size_t split_segment_index_ = 0;
  size_t split_segment_count_ = 1;
  std::optional<SidebarTreeController::DropPosition> drop_position_;
  std::optional<gfx::Rect> split_group_bounds_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_ROW_VIEW_H_
