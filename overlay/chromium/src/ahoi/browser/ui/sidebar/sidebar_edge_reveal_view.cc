// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_edge_reveal_view.h"

#include <utility>

#include "base/check.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

class SidebarEdgeRevealView final : public views::View {
 public:
  explicit SidebarEdgeRevealView(base::RepeatingClosure reveal_callback)
      : reveal_callback_(std::move(reveal_callback)) {
    CHECK(reveal_callback_);
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetRole(ax::mojom::Role::kNone);
    GetViewAccessibility().SetIsIgnored(true);
  }

  SidebarEdgeRevealView(const SidebarEdgeRevealView&) = delete;
  SidebarEdgeRevealView& operator=(const SidebarEdgeRevealView&) = delete;
  ~SidebarEdgeRevealView() override = default;

  void OnMouseEntered(const ui::MouseEvent& event) override {
    reveal_callback_.Run();
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    reveal_callback_.Run();
  }

 private:
  base::RepeatingClosure reveal_callback_;
};

}  // namespace

std::unique_ptr<views::View> CreateSidebarEdgeRevealView(
    base::RepeatingClosure reveal_callback) {
  return std::make_unique<SidebarEdgeRevealView>(
      std::move(reveal_callback));
}

}  // namespace ahoi::sidebar
