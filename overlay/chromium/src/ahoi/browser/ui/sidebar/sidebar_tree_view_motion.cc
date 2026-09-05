// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ahoi::sidebar {

void SidebarTreeView::OnFolderExpansionChanging(const base::Uuid& node_id,
                                                bool expanded) {
  pending_folder_reveal_.reset();
  const auto* folder = GetMaterializedRowForTesting(node_id);
  const bool native_drag =
      std::ranges::any_of(materialized_rows_, [](const auto& entry) {
        return entry.second && entry.second->is_native_drag_in_progress();
      });
  if (folder && !folder->bounds().IsEmpty() && !native_drag &&
      gfx::Animation::ShouldRenderRichAnimation()) {
    pending_folder_reveal_ =
        FolderReveal{node_id, expanded, folder->bounds().bottom()};
  }
}

void SidebarTreeView::PrepareFolderExit(size_t first_row, size_t count) {
  if (!pending_folder_reveal_ || pending_folder_reveal_->expanded) {
    return;
  }
  // The splice has removed model rows but the last materialized indices still
  // describe them. Retain only this bounded visible set, never hidden children.
  for (const auto& [id, row] : materialized_rows_) {
    if (!row || exiting_rows_.contains(id) || row->row_index() < first_row ||
        row->row_index() - first_row >= count ||
        row->is_native_drag_in_progress() ||
        row->is_dragging_for_presentation()) {
      continue;
    }
    exiting_rows_.insert(id);
    row->SetExiting(true);
    row->set_drag_controller(nullptr);
    gfx::Rect target = row->bounds();
    target.set_y(pending_folder_reveal_->origin_y);
    target.set_height(0);
    row_bounds_animator_.AnimateViewTo(row, target);
  }
  for (const auto& group : materialized_split_clip_groups_) {
    if (std::ranges::all_of(group,
                            [this](const base::Uuid& id) {
                              return exiting_rows_.contains(id);
                            }) &&
        std::ranges::find(exiting_split_clip_groups_, group) ==
            exiting_split_clip_groups_.end()) {
      exiting_split_clip_groups_.push_back(group);
    }
  }
}

std::optional<int> SidebarTreeView::FolderEntryOrigin(size_t row_index) const {
  if (!pending_folder_reveal_ || !pending_folder_reveal_->expanded ||
      !pending_folder_reveal_->splice_ready) {
    return std::nullopt;
  }
  const auto& row = model().rows()[row_index];
  const tab_tree::TreeNode* node = model().GetNode(row.node_id);
  for (size_t level = 0; node && node->parent_id && level <= row.depth;
       ++level) {
    if (*node->parent_id == pending_folder_reveal_->folder_id) {
      return pending_folder_reveal_->origin_y;
    }
    node = model().GetNode(*node->parent_id);
  }
  return std::nullopt;
}

void SidebarTreeView::ScheduleExitedRowCleanup() {
  if (exiting_rows_.empty() || exited_row_cleanup_pending_) {
    return;
  }
  exited_row_cleanup_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarTreeView::CleanupExitedRows,
                                weak_ptr_factory_.GetWeakPtr()));
}

void SidebarTreeView::CleanupExitedRows() {
  exited_row_cleanup_pending_ = false;
  const auto completed = exiting_rows_;
  for (const base::Uuid& id : completed) {
    auto* row = GetMaterializedRowForTesting(id);
    if (!row) {
      exiting_rows_.erase(id);
    } else if (!row_bounds_animator_.IsAnimating(row) &&
               !model().GetRowForNode(id).has_value()) {
      RecycleRow(id);
    }
  }
  std::erase_if(exiting_split_clip_groups_, [this](const auto& group) {
    return std::ranges::none_of(group, [this](const base::Uuid& id) {
      return exiting_rows_.contains(id);
    });
  });
}

void SidebarTreeView::HandleVisualLayoutChanged() {
  const int target_height = GetVisualRowsHeight(BuildVisualRows());
  row_bounds_animation_pending_ = target_height != last_visual_height_ &&
                                  gfx::Animation::ShouldRenderRichAnimation();
  if (in_batch_update_) {
    if (!pending_animation_from_height_.has_value()) {
      pending_animation_from_height_ = last_visual_height_;
    }
  } else {
    StartPreferredHeightAnimation(last_visual_height_, target_height);
  }
  last_visual_height_ = target_height;
}

void SidebarTreeView::StartPreferredHeightAnimation(int from_height,
                                                    int to_height) {
  const int current_height =
      preferred_height_animation_active_
          ? GetAnimatedHeight()
          : std::max(from_height, SidebarTreeRowView::kRowHeight);
  // Reset synchronously cancels an in-flight animation and clears active_ in
  // AnimationCanceled(). Establish the replacement state only afterwards, and
  // start at the displayed intermediate height rather than the previous target.
  preferred_height_animation_.Reset(0.0);
  preferred_height_animation_active_ = false;
  animated_height_from_ = current_height;
  animated_height_to_ = std::max(to_height, SidebarTreeRowView::kRowHeight);
  if (animated_height_from_ == animated_height_to_ ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    preferred_height_animation_.Reset(1.0);
    preferred_height_animation_active_ = false;
    PreferredSizeChanged();
    return;
  }
  preferred_height_animation_active_ = true;
  preferred_height_animation_.Show();
}

void SidebarTreeView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &preferred_height_animation_) {
    PreferredSizeChanged();
    InvalidateLayout();
  }
}

void SidebarTreeView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &preferred_height_animation_) {
    preferred_height_animation_active_ = false;
    PreferredSizeChanged();
    InvalidateLayout();
    MaybeScheduleSelectionReveal();
  }
}

void SidebarTreeView::AnimationCanceled(const gfx::Animation* animation) {
  AnimationEnded(animation);
}

void SidebarTreeView::OnBoundsAnimatorProgressed(views::BoundsAnimator*) {
  UpdateAnimatedSplitClips();
}

void SidebarTreeView::OnBoundsAnimatorDone(views::BoundsAnimator*) {
  UpdateAnimatedSplitClips();
  // Never remove children from inside BoundsAnimator's container iteration.
  ScheduleExitedRowCleanup();
  MaybeScheduleSelectionReveal();
}

void SidebarTreeView::UpdateAnimatedSplitClips() {
  // BoundsAnimator notifies after every row in its shared container has moved.
  // Use that same current geometry for the group bubble, never its end
  // position.
  const auto update_group = [this](const auto& group, bool exiting) {
    gfx::Rect group_bounds;
    for (const base::Uuid& id : group) {
      if (exiting && !exiting_rows_.contains(id)) {
        continue;
      }
      if (auto* row = GetMaterializedRowForTesting(id)) {
        group_bounds.Union(row->bounds());
      }
    }
    for (const base::Uuid& id : group) {
      if (exiting && !exiting_rows_.contains(id)) {
        continue;
      }
      if (auto* row = GetMaterializedRowForTesting(id)) {
        row->SetSplitGroupClipBounds(group_bounds);
      }
    }
  };
  for (const auto& group : materialized_split_clip_groups_) {
    update_group(group, false);
  }
  for (const auto& group : exiting_split_clip_groups_) {
    update_group(group, true);
  }
}

}  // namespace ahoi::sidebar
