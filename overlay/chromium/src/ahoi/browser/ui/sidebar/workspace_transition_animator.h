// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_WORKSPACE_TRANSITION_ANIMATOR_H_
#define AHOI_BROWSER_UI_SIDEBAR_WORKSPACE_TRANSITION_ANIMATOR_H_

#include "base/memory/weak_ptr.h"

namespace ui {
class Layer;
}

namespace ahoi::sidebar {

enum class WorkspaceTransitionDirection {
  kPrevious = -1,
  kNext = 1,
};

struct WorkspaceTransitionVisualState {
  float opacity = 1.0f;
  int horizontal_offset = 0;

  bool operator==(const WorkspaceTransitionVisualState&) const = default;
};

WorkspaceTransitionVisualState CalculateWorkspaceTransitionInitialState(
    WorkspaceTransitionDirection direction);

// Slides the already-committed sidebar in the workspace direction. The page
// surface remains spatially stable and only fades when the activation actually
// selected a different WebContents. Domain state never lives here: identity,
// dots, tree selection, runtime tab and WebContents are switched atomically by
// the existing observer path before these layers receive their first frame.
class WorkspaceTransitionAnimator final {
 public:
  WorkspaceTransitionAnimator();
  WorkspaceTransitionAnimator(const WorkspaceTransitionAnimator&) = delete;
  WorkspaceTransitionAnimator& operator=(const WorkspaceTransitionAnimator&) =
      delete;
  ~WorkspaceTransitionAnimator();

  void Start(ui::Layer* sidebar_layer,
             ui::Layer* contents_layer,
             WorkspaceTransitionDirection direction,
             bool fade_contents,
             bool reduced_motion);

  // Immediately finishes any in-flight transition at the stable committed
  // state. Safe for a new gesture, a canceled gesture and teardown.
  void Cancel();

  bool is_animating() const;

 private:
  void ResetLayer(ui::Layer* layer);
  void AnimateSidebarLayer(
      ui::Layer* layer,
      const WorkspaceTransitionVisualState& initial_state);
  void AnimateContentsLayer(
      ui::Layer* layer,
      const WorkspaceTransitionVisualState& initial_state);

  base::WeakPtr<ui::Layer> sidebar_layer_;
  base::WeakPtr<ui::Layer> contents_layer_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_WORKSPACE_TRANSITION_ANIMATOR_H_
