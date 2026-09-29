// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-05, drag-and-drop: while a sidebar item is dragged in one
// Profile's window, the sidebar of every window of another Profile shows a
// calm drop card for its Workspace. Its rows cannot take the item (their
// tree lives in another Profile), so the card covers them for the drag and
// says what a drop means before the confirmation says it in full.

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace ahoi::sidebar {

namespace {

constexpr int kCardInset = 8;
constexpr float kCardRadius = 12.0f;

std::u16string Text(std::u16string_view german, std::u16string_view english) {
  return std::u16string(base::StartsWith(base::i18n::GetConfiguredLocale(),
                                         "de",
                                         base::CompareCase::INSENSITIVE_ASCII)
                            ? german
                            : english);
}

std::set<BrowserSidebarHostView*>& LiveHosts() {
  static base::NoDestructor<std::set<BrowserSidebarHostView*>> hosts;
  return *hosts;
}

// Covers the target sidebar for the length of one drag. Idle it shows a
// quiet outline; under the pointer the accent fill says "drop here".
class CrossLevelDropCard final : public views::View {
  METADATA_HEADER(CrossLevelDropCard, views::View)

 public:
  CrossLevelDropCard(const std::u16string& workspace_name,
                     base::RepeatingClosure accept)
      : accept_(std::move(accept)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(16, 20), 4));
    layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    const std::u16string title =
        base::StrCat({Text(u"Nach „", u"Move to “"), workspace_name,
                      Text(u"“ verschieben", u"”")});
    auto* heading = AddChildView(std::make_unique<views::Label>(title));
    heading->SetFontList(heading->font_list().DeriveWithWeight(
        gfx::Font::Weight::SEMIBOLD));
    heading->SetEnabledColor(visual_style::kText);
    heading->SetMultiLine(true);
    heading->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    heading->SetSubpixelRenderingEnabled(false);
    const std::u16string detail = Text(
        u"Seiten laden hier neu. Anmeldungen ziehen nicht mit.",
        u"Pages reload here. Sign-ins don't move along.");
    auto* hint = AddChildView(std::make_unique<views::Label>(detail));
    hint->SetEnabledColor(visual_style::kMutedText);
    hint->SetMultiLine(true);
    hint->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    hint->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(base::StrCat({title, u". ", detail}));
  }

  CrossLevelDropCard(const CrossLevelDropCard&) = delete;
  CrossLevelDropCard& operator=(const CrossLevelDropCard&) = delete;
  ~CrossLevelDropCard() override = default;

  // views::View:
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* types) override {
    *formats |= ui::OSExchangeData::PICKLED_DATA;
    types->insert(drag::SavedSidebarTabDragFormat());
    types->insert(drag::RuntimeSidebarTabDragFormat());
    return true;
  }
  bool AreDropTypesRequired() override { return true; }
  bool CanDrop(const ui::OSExchangeData& data) override {
    return drag::ReadSidebarTabDragPayload(data).has_value();
  }
  void OnDragEntered(const ui::DropTargetEvent&) override {
    SetHovered(true);
  }
  int OnDragUpdated(const ui::DropTargetEvent&) override {
    SetHovered(true);
    return ui::DragDropTypes::DRAG_MOVE;
  }
  void OnDragExited() override { SetHovered(false); }
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent&) override {
    return base::BindOnce(
        [](base::RepeatingClosure accept, const ui::DropTargetEvent&,
           ui::mojom::DragOperation& output_drag_op,
           std::unique_ptr<ui::LayerTreeOwner>) {
          output_drag_op = ui::mojom::DragOperation::kMove;
          accept.Run();
        },
        accept_);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor accent = GetColorProvider()->GetColor(visual_style::kAccent);
    const gfx::RectF card(GetLocalBounds());
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    // The surface stays readable in both appearances: a soft accent wash
    // over the sidebar's own material, never an opaque slab.
    fill.setColor(SkColorSetA(accent, hovered_ ? 0x38 : 0x14));
    canvas->DrawRoundRect(card, kCardRadius, fill);
    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    outline.setStrokeWidth(hovered_ ? 2.0f : 1.0f);
    outline.setColor(SkColorSetA(accent, hovered_ ? 0xFF : 0x80));
    gfx::RectF stroke = card;
    stroke.Inset(hovered_ ? 1.0f : 0.5f);
    canvas->DrawRoundRect(stroke, kCardRadius, outline);
  }

 private:
  void SetHovered(bool hovered) {
    if (hovered_ != hovered) {
      hovered_ = hovered;
      SchedulePaint();
    }
  }

  base::RepeatingClosure accept_;
  bool hovered_ = false;
};

BEGIN_METADATA(CrossLevelDropCard)
END_METADATA

}  // namespace

void TrackBrowserSidebarHostForCrossLevelDrop(BrowserSidebarHostView* host,
                                              bool live) {
  if (live) {
    LiveHosts().insert(host);
    return;
  }
  LiveHosts().erase(host);
  host->SetCrossLevelDropSource(nullptr);
}

void UpdateBrowserSidebarCrossLevelDropTargets(
    BrowserSidebarHostView* source) {
  const std::set<BrowserSidebarHostView*> hosts = LiveHosts();
  for (BrowserSidebarHostView* host : hosts) {
    if (LiveHosts().contains(host)) {
      host->SetCrossLevelDropSource(source);
    }
  }
}

void BrowserSidebarHostView::SetCrossLevelDropSource(
    BrowserSidebarHostView* source) {
  Profile* const own = browser_->GetProfile();
  std::optional<SwitcherWorkspace> target;
  // Only an item drag, never a split divider resize, offers the card.
  if (source && source != this && source->browser_->GetProfile() != own &&
      !own->IsOffTheRecord() &&
      (source->dragged_node_id_ || source->dragged_runtime_tab_handle_)) {
    const std::optional<base::Uuid> active =
        session_bridge_->GetActiveWorkspaceForWindow(browser_);
    for (const SwitcherWorkspace& workspace : source->CrossLevelTargets()) {
      if (active && workspace.key.workspace_id == *active) {
        target = workspace;
      }
    }
  }
  if (!target) {
    cross_level_drop_source_.reset();
    if (views::View* card = cross_level_drop_overlay_.get()) {
      cross_level_drop_overlay_ = nullptr;
      // The drop already ran; the source ends its drag only afterwards.
      RemoveChildViewT(card);
    }
    return;
  }
  cross_level_drop_source_ = source->weak_ptr_factory_.GetWeakPtr();
  if (cross_level_drop_overlay_) {
    return;
  }
  cross_level_drop_overlay_ =
      AddChildView(std::make_unique<CrossLevelDropCard>(
          target->name,
          base::BindRepeating(&BrowserSidebarHostView::AcceptCrossLevelDrop,
                              weak_ptr_factory_.GetWeakPtr())));
  gfx::Rect bounds = GetLocalBounds();
  bounds.Inset(kCardInset);
  cross_level_drop_overlay_->SetBoundsRect(bounds);
}

void BrowserSidebarHostView::AcceptCrossLevelDrop() {
  BrowserSidebarHostView* source = cross_level_drop_source_.get();
  const std::optional<base::Uuid> active =
      session_bridge_->GetActiveWorkspaceForWindow(browser_);
  if (!source || !active) {
    return;
  }
  // Read the dragged item now: the source clears its drag state as soon as
  // the native drag ends.
  std::vector<base::Uuid> roots;
  if (source->dragged_node_id_) {
    roots = source->GetMoveGroupNodeIds(*source->dragged_node_id_);
  } else if (source->dragged_runtime_tab_handle_) {
    roots = source->CrossLevelRootsForTab(
        source->FindRuntimeTab(*source->dragged_runtime_tab_handle_));
  }
  std::optional<SwitcherWorkspace> target;
  for (const SwitcherWorkspace& workspace : source->CrossLevelTargets()) {
    if (workspace.key.workspace_id == *active) {
      target = workspace;
    }
  }
  if (roots.empty() || !target) {
    return;
  }
  // The user is looking at this window, so it asks; the source moves.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> source,
             base::WeakPtr<BrowserSidebarHostView> presenter,
             std::vector<base::Uuid> roots, SwitcherWorkspace target) {
            if (source && presenter) {
              source->RequestCrossLevelMove(std::move(roots), target,
                                            /*follow=*/false, presenter.get());
            }
          },
          source->weak_ptr_factory_.GetWeakPtr(),
          weak_ptr_factory_.GetWeakPtr(), std::move(roots), *target));
}

}  // namespace ahoi::sidebar
