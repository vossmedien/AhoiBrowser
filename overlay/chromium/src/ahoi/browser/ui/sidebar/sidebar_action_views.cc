// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

class PageFallbackIconView final : public views::View {
  METADATA_HEADER(PageFallbackIconView, views::View)

 public:
  PageFallbackIconView(bool active, bool new_tab)
      : active_(active), new_tab_(new_tab) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setColor(GetColorProvider()->GetColor(
        active_ ? visual_style::kText : visual_style::kMutedText));
    stroke.setStrokeWidth(1.5f);
    stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    if (new_tab_) {
      const gfx::Point center = GetLocalBounds().CenterPoint();
      canvas->DrawLine(gfx::PointF(center.x() - 4.0f, center.y()),
                       gfx::PointF(center.x() + 4.0f, center.y()), stroke);
      canvas->DrawLine(gfx::PointF(center.x(), center.y() - 4.0f),
                       gfx::PointF(center.x(), center.y() + 4.0f), stroke);
      return;
    }

    gfx::RectF page_icon(GetLocalBounds());
    page_icon.Inset(gfx::InsetsF(2.0f));
    stroke.setStrokeCap(cc::PaintFlags::kButt_Cap);
    canvas->DrawRoundRect(page_icon, 2.0f, stroke);
    stroke.setStrokeWidth(1.0f);
    const float left = page_icon.x() + 3.0f;
    canvas->DrawLine(
        gfx::PointF(left, page_icon.y() + 4.0f),
        gfx::PointF(page_icon.right() - 3.0f, page_icon.y() + 4.0f), stroke);
  }

 private:
  const bool active_;
  const bool new_tab_;
};

class TabCloseIconView final : public views::View {
  METADATA_HEADER(TabCloseIconView, views::View)

 public:
  explicit TabCloseIconView(bool active) : active_(active) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const gfx::Point center = GetLocalBounds().CenterPoint();
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setColor(GetColorProvider()->GetColor(
        active_ ? visual_style::kText : visual_style::kMutedText));
    stroke.setStrokeWidth(1.5f);
    stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawLine(gfx::PointF(center.x() - 4.0f, center.y() - 4.0f),
                     gfx::PointF(center.x() + 4.0f, center.y() + 4.0f), stroke);
    canvas->DrawLine(gfx::PointF(center.x() + 4.0f, center.y() - 4.0f),
                     gfx::PointF(center.x() - 4.0f, center.y() + 4.0f), stroke);
  }

 private:
  const bool active_;
};

BEGIN_METADATA(PageFallbackIconView)
END_METADATA

BEGIN_METADATA(TabCloseIconView)
END_METADATA

class WorkspaceDotButton final : public views::Button {
  METADATA_HEADER(WorkspaceDotButton, views::Button)

 public:
  WorkspaceDotButton(PressedCallback callback,
                     base::RepeatingCallback<void(bool)> preview_callback,
                     SkColor accent,
                     const std::u16string& workspace_name)
      : views::Button(std::move(callback)),
        preview_callback_(std::move(preview_callback)),
        accent_(accent) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(14, 22));
    SetAccessibleName(workspace_name);
    SetTooltipText(workspace_name);
  }

  WorkspaceDotButton(const WorkspaceDotButton&) = delete;
  WorkspaceDotButton& operator=(const WorkspaceDotButton&) = delete;
  ~WorkspaceDotButton() override = default;

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdatePreviewState();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    UpdatePreviewState();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdatePreviewState();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF center{GetLocalBounds().CenterPoint()};
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(GetColorProvider()->GetColor(visual_style::kMutedText));
    canvas->DrawCircle(center, 3.0f, fill);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(1.0f);
    ring.setColor(accent_);
    canvas->DrawCircle(center, 4.0f, ring);
    views::Button::PaintButtonContents(canvas);
  }

 private:
  void UpdatePreviewState() {
    const bool should_preview = HasFocus() ||
                                GetState() == ButtonState::STATE_HOVERED ||
                                GetState() == ButtonState::STATE_PRESSED;
    if (should_preview == previewing_) {
      return;
    }
    previewing_ = should_preview;
    preview_callback_.Run(previewing_);
  }

  base::RepeatingCallback<void(bool)> preview_callback_;
  const SkColor accent_;
  bool previewing_ = false;
};

BEGIN_METADATA(WorkspaceDotButton)
END_METADATA

class WorkspaceSelectorButton final : public views::Button {
  METADATA_HEADER(WorkspaceSelectorButton, views::Button)

 public:
  explicit WorkspaceSelectorButton(PressedCallback callback)
      : Button(std::move(callback)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(0, visual_style::kSidebarActionCellHeight));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    badge_ = AddChildView(std::make_unique<views::Label>(u"A"));
    badge_->SetSubpixelRenderingEnabled(false);
    badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    badge_->SetPreferredSize(gfx::Size(22, 22));
    badge_->SetEnabledColor(visual_style::kAccent);
    badge_->SetBackground(
        views::CreateRoundedRectBackground(visual_style::kChromeSurface, 5));
    badge_->SetBorder(
        views::CreateRoundedRectBorder(1, 5, visual_style::kAccent));
    badge_->GetViewAccessibility().SetIsIgnored(true);

    title_ = AddChildView(std::make_unique<views::Label>(u"Ahoi"));
    title_->SetSubpixelRenderingEnabled(false);
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetEnabledColor(visual_style::kText);
    title_->GetViewAccessibility().SetIsIgnored(true);
    layout->SetFlexForView(title_, 1);

    edit_icon_ = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            kEditIcon, visual_style::kMutedText, 14)));
    edit_icon_->SetCanProcessEventsWithinSubtree(false);
    edit_icon_->GetViewAccessibility().SetIsIgnored(true);
    edit_icon_->SetVisible(false);

    dots_ = AddChildView(std::make_unique<views::View>());
    auto* dots_layout =
        dots_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 1));
    dots_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    dots_->SetVisible(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    UpdateBackground();
  }

  WorkspaceSelectorButton(const WorkspaceSelectorButton&) = delete;
  WorkspaceSelectorButton& operator=(const WorkspaceSelectorButton&) = delete;
  ~WorkspaceSelectorButton() override = default;

  void SetWorkspace(const std::u16string& name,
                    const std::u16string& icon,
                    std::optional<uint32_t> accent_argb) {
    active_name_ = name;
    active_icon_ = icon;
    active_accent_argb_ = accent_argb;
    if (preview_index_.has_value()) {
      return;
    }
    ApplyWorkspacePresentation(name, icon, accent_argb);
  }

  void ApplyWorkspacePresentation(const std::u16string& name,
                                  const std::u16string& icon,
                                  std::optional<uint32_t> accent_argb) {
    title_->SetText(name);
    badge_->SetText(
        icon.empty() ? name.substr(0, std::min<size_t>(1, name.size())) : icon);
    const SkColor accent = accent_argb.value_or(visual_style::kDefaultAccent);
    badge_->SetEnabledColor(accent);
    badge_->SetBackground(
        views::CreateRoundedRectBackground(visual_style::kChromeSurface, 5));
    badge_->SetBorder(views::CreateRoundedRectBorder(1, 5, accent));
    SetAccessibleName(
        name + l10n_util::GetStringUTF16(IDS_AHOI_WORKSPACE_SWITCH_SUFFIX));
  }

  void SetIndicators(std::vector<WorkspaceSelectorIndicator> indicators,
                     base::RepeatingCallback<void(size_t)> activate_callback) {
    preview_index_.reset();
    indicators_ = std::move(indicators);
    dots_->RemoveAllChildViews();
    for (size_t index = 0; index < indicators_.size(); ++index) {
      const WorkspaceSelectorIndicator& indicator = indicators_[index];
      auto dot = std::make_unique<WorkspaceDotButton>(
          base::BindRepeating(
              [](base::RepeatingCallback<void(size_t)> callback, size_t index,
                 const ui::Event&) { callback.Run(index); },
              activate_callback, indicator.workspace_index),
          base::BindRepeating(&WorkspaceSelectorButton::SetPreviewVisible,
                              base::Unretained(this), index),
          indicator.accent_argb.value_or(visual_style::kDefaultAccent),
          indicator.name);
      dots_->AddChildView(std::move(dot));
    }
    dots_->SetVisible(!indicators_.empty());
    ApplyWorkspacePresentation(active_name_, active_icon_, active_accent_argb_);
    InvalidateLayout();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateBackground();
  }

 private:
  void SetPreviewVisible(size_t index, bool visible) {
    if (visible && index < indicators_.size()) {
      preview_index_ = index;
      const WorkspaceSelectorIndicator& indicator = indicators_[index];
      ApplyWorkspacePresentation(indicator.name, indicator.icon,
                                 indicator.accent_argb);
      return;
    }
    if (!visible && preview_index_ == index) {
      preview_index_.reset();
      ApplyWorkspacePresentation(active_name_, active_icon_,
                                 active_accent_argb_);
    }
  }

  void UpdateBackground() {
    const bool hovered = GetState() == ButtonState::STATE_HOVERED ||
                         GetState() == ButtonState::STATE_PRESSED;
    edit_icon_->SetVisible(hovered);
    SetBackground(views::CreateRoundedRectBackground(
        hovered ? visual_style::kHoverSurface : visual_style::kSelectedSurface,
        visual_style::kRowCornerRadius));
  }

  raw_ptr<views::Label> badge_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> edit_icon_ = nullptr;
  raw_ptr<views::View> dots_ = nullptr;
  std::u16string active_name_ = u"Ahoi";
  std::u16string active_icon_ = u"A";
  std::optional<uint32_t> active_accent_argb_;
  std::vector<WorkspaceSelectorIndicator> indicators_;
  std::optional<size_t> preview_index_;
};

BEGIN_METADATA(WorkspaceSelectorButton)
END_METADATA

class SidebarActionButton final : public views::Button {
  METADATA_HEADER(SidebarActionButton, views::Button)

 public:
  SidebarActionButton(
      PressedCallback callback,
      const gfx::VectorIcon& icon,
      std::u16string accessible_name,
      int preferred_width = visual_style::kSidebarActionCellWidth,
      int icon_size = 19,
      bool draw_idle_shape = false,
      float hover_top_radius = visual_style::kControlCornerRadius,
      float hover_bottom_radius = visual_style::kControlCornerRadius)
      : views::Button(std::move(callback)),
        hover_top_radius_(hover_top_radius),
        hover_bottom_radius_(hover_bottom_radius) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetAnimateOnStateChange(true);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(
        gfx::Size(preferred_width, visual_style::kSidebarActionCellHeight));
    SetAccessibleName(accessible_name);
    SetTooltipText(accessible_name);
    icon_ = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            icon, visual_style::kMutedText, icon_size)));
    icon_->SetCanProcessEventsWithinSubtree(false);
    icon_->GetViewAccessibility().SetIsIgnored(true);
    if (draw_idle_shape) {
      SetBorder(views::CreateRoundedRectBorder(
          1, visual_style::kControlCornerRadius, visual_style::kDivider));
    }
  }

  SidebarActionButton(const SidebarActionButton&) = delete;
  SidebarActionButton& operator=(const SidebarActionButton&) = delete;
  ~SidebarActionButton() override = default;

  void Layout(PassKey) override { icon_->SetBoundsRect(GetLocalBounds()); }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SkColor idle =
        GetColorProvider()->GetColor(visual_style::kRaisedSurface);
    const SkColor hovered =
        GetColorProvider()->GetColor(visual_style::kHoverSurface);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(gfx::Tween::ColorValueBetween(
        hover_animation().GetCurrentValue(), idle, hovered));

    const SkVector radii[4] = {
        {hover_top_radius_, hover_top_radius_},
        {hover_top_radius_, hover_top_radius_},
        {hover_bottom_radius_, hover_bottom_radius_},
        {hover_bottom_radius_, hover_bottom_radius_},
    };
    canvas->sk_canvas()->drawRRect(
        SkRRect::MakeRectRadii(gfx::RectToSkRect(GetLocalBounds()), radii),
        fill);
    views::Button::PaintButtonContents(canvas);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  const float hover_top_radius_;
  const float hover_bottom_radius_;
};

BEGIN_METADATA(SidebarActionButton)
END_METADATA

// Two independently actionable rows occupying exactly one ordinary dock cell.
// A narrow transparent gap separates their borderless filled shapes; each half
// retains its own focus, tooltip, accessible name and native button semantics.
class SidebarSplitActionCell final : public views::View {
  METADATA_HEADER(SidebarSplitActionCell, views::View)

 public:
  SidebarSplitActionCell(views::Button::PressedCallback top_callback,
                         const gfx::VectorIcon& top_icon,
                         std::u16string top_name,
                         views::Button::PressedCallback bottom_callback,
                         const gfx::VectorIcon& bottom_icon,
                         std::u16string bottom_name) {
    SetPreferredSize(gfx::Size(visual_style::kSidebarActionCellWidth,
                               visual_style::kSidebarActionCellHeight));
    SetBackground(nullptr);
    SetBorder(nullptr);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    top_ = AddChildView(std::make_unique<SidebarActionButton>(
        std::move(top_callback), top_icon, std::move(top_name),
        visual_style::kSidebarActionCellWidth, 14,
        /*draw_idle_shape=*/false, visual_style::kControlCornerRadius,
        visual_style::kControlCornerRadius));
    bottom_ = AddChildView(std::make_unique<SidebarActionButton>(
        std::move(bottom_callback), bottom_icon, std::move(bottom_name),
        visual_style::kSidebarActionCellWidth, 13,
        /*draw_idle_shape=*/false, visual_style::kControlCornerRadius,
        visual_style::kControlCornerRadius));
  }

  SidebarSplitActionCell(const SidebarSplitActionCell&) = delete;
  SidebarSplitActionCell& operator=(const SidebarSplitActionCell&) = delete;
  ~SidebarSplitActionCell() override = default;

  void Layout(PassKey) override {
    const gfx::Rect contents = GetContentsBounds();
    const int available_height =
        std::max(0, contents.height() - visual_style::kSidebarSplitActionGap);
    const int top_height = (available_height + 1) / 2;
    top_->SetBounds(contents.x(), contents.y(), contents.width(), top_height);
    bottom_->SetBounds(
        contents.x(),
        contents.y() + top_height + visual_style::kSidebarSplitActionGap,
        contents.width(), available_height - top_height);
  }

 private:
  raw_ptr<SidebarActionButton> top_ = nullptr;
  raw_ptr<SidebarActionButton> bottom_ = nullptr;
};

BEGIN_METADATA(SidebarSplitActionCell)
END_METADATA

// A compact, self-painted swatch avoids the double-shape produced by putting a
// tiny colored glyph inside a conventional rounded LabelButton. The complete
// visual surface is the color choice and the selected ring remains legible in
// both light and dark themes.
class GroupColorSwatchButton final : public views::Button {
  METADATA_HEADER(GroupColorSwatchButton, views::Button)

 public:
  GroupColorSwatchButton(PressedCallback callback,
                         std::optional<uint32_t> color,
                         std::u16string accessible_name)
      : views::Button(std::move(callback)), color_(color) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(
        gfx::Size(visual_style::kTreeRowHeight, visual_style::kTreeRowHeight));
    SetAccessibleName(accessible_name);
    SetTooltipText(accessible_name);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    UpdateAccessibleCheckedState();
  }

  GroupColorSwatchButton(const GroupColorSwatchButton&) = delete;
  GroupColorSwatchButton& operator=(const GroupColorSwatchButton&) = delete;
  ~GroupColorSwatchButton() override = default;

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateAccessibleCheckedState();
    SchedulePaint();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void UpdateAccessibleCheckedState() override {
    GetViewAccessibility().SetCheckedState(
        selected_ ? ax::mojom::CheckedState::kTrue
                  : ax::mojom::CheckedState::kFalse);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    gfx::RectF swatch(GetLocalBounds());
    swatch.Inset(gfx::InsetsF(3.0f));
    const float radius = 7.0f;
    const SkColor swatch_color =
        color_.has_value()
            ? static_cast<SkColor>(*color_)
            : GetColorProvider()->GetColor(visual_style::kRaisedSurface);

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(swatch_color);
    canvas->DrawRoundRect(swatch, radius, fill);

    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    outline.setStrokeWidth(selected_ ? 2.0f : 1.0f);
    outline.setColor(GetColorProvider()->GetColor(
        selected_ ? visual_style::kAccent : visual_style::kDivider));
    gfx::RectF outline_bounds = swatch;
    outline_bounds.Inset(selected_ ? -1.0f : 0.5f);
    canvas->DrawRoundRect(outline_bounds, radius + (selected_ ? 1.0f : 0.0f),
                          outline);

    if (!color_.has_value()) {
      cc::PaintFlags slash = outline;
      slash.setStrokeWidth(1.5f);
      slash.setStrokeCap(cc::PaintFlags::kRound_Cap);
      slash.setColor(GetColorProvider()->GetColor(visual_style::kMutedText));
      canvas->DrawLine(gfx::PointF(swatch.x() + 7.0f, swatch.bottom() - 7.0f),
                       gfx::PointF(swatch.right() - 7.0f, swatch.y() + 7.0f),
                       slash);
    }

    if (GetState() == ButtonState::STATE_HOVERED ||
        GetState() == ButtonState::STATE_PRESSED) {
      cc::PaintFlags hover = outline;
      hover.setStrokeWidth(1.0f);
      hover.setColor(GetColorProvider()->GetColor(visual_style::kFocusRing));
      gfx::RectF hover_bounds(GetLocalBounds());
      hover_bounds.Inset(gfx::InsetsF(1.0f));
      canvas->DrawRoundRect(hover_bounds, radius + 2.0f, hover);
    }
  }

 private:
  const std::optional<uint32_t> color_;
  bool selected_ = false;
};

BEGIN_METADATA(GroupColorSwatchButton)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreatePageFallbackIconView(bool active,
                                                        bool new_tab) {
  return std::make_unique<PageFallbackIconView>(active, new_tab);
}

std::unique_ptr<views::View> CreateTabCloseIconView(bool active) {
  return std::make_unique<TabCloseIconView>(active);
}

std::unique_ptr<views::Button> CreateWorkspaceSelectorButton(
    views::Button::PressedCallback callback) {
  return std::make_unique<WorkspaceSelectorButton>(std::move(callback));
}

void SetWorkspaceSelectorPresentation(views::Button* button,
                                      const std::u16string& name,
                                      const std::u16string& icon,
                                      std::optional<uint32_t> accent_argb) {
  if (auto* selector = views::AsViewClass<WorkspaceSelectorButton>(button)) {
    selector->SetWorkspace(name, icon, accent_argb);
  }
}

void SetWorkspaceSelectorIndicators(
    views::Button* button,
    std::vector<WorkspaceSelectorIndicator> indicators,
    base::RepeatingCallback<void(size_t)> activate_callback) {
  if (auto* selector = views::AsViewClass<WorkspaceSelectorButton>(button)) {
    selector->SetIndicators(std::move(indicators),
                            std::move(activate_callback));
  }
}

std::unique_ptr<views::View> CreateSidebarActionButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name) {
  return std::make_unique<SidebarActionButton>(std::move(callback), icon,
                                               std::move(accessible_name));
}

std::unique_ptr<views::View> CreateSidebarHeaderActionButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name) {
  return std::make_unique<SidebarActionButton>(
      std::move(callback), icon, std::move(accessible_name),
      /*preferred_width=*/30, /*icon_size=*/15,
      /*draw_idle_shape=*/false);
}

std::unique_ptr<views::View> CreateSidebarSplitActionCell(
    views::Button::PressedCallback top_callback,
    const gfx::VectorIcon& top_icon,
    std::u16string top_name,
    views::Button::PressedCallback bottom_callback,
    const gfx::VectorIcon& bottom_icon,
    std::u16string bottom_name) {
  return std::make_unique<SidebarSplitActionCell>(
      std::move(top_callback), top_icon, std::move(top_name),
      std::move(bottom_callback), bottom_icon, std::move(bottom_name));
}

std::unique_ptr<views::Button> CreateGroupColorSwatchButton(
    views::Button::PressedCallback callback,
    std::optional<uint32_t> color,
    std::u16string accessible_name) {
  return std::make_unique<GroupColorSwatchButton>(std::move(callback), color,
                                                  std::move(accessible_name));
}

void SetGroupColorSwatchSelected(views::Button* button, bool selected) {
  if (auto* swatch = views::AsViewClass<GroupColorSwatchButton>(button)) {
    swatch->SetSelected(selected);
  }
}

}  // namespace ahoi::sidebar
