// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

class RecentGroupLinkRowView final : public views::Button {
  METADATA_HEADER(RecentGroupLinkRowView, views::Button)

 public:
  RecentGroupLinkRowView(PressedCallback callback,
                         const RecentGroupLink& link,
                         std::u16string relative_time)
      : views::Button(std::move(callback)), url_(link.url) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(304, 50));
    SetAccessibleName(link.title + u", " + relative_time);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 8), 9));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    if (link.favicon.IsEmpty()) {
      auto* fallback = AddChildView(CreatePageFallbackIconView(false, false));
      fallback->SetPreferredSize(gfx::Size(18, 18));
      fallback->SetCanProcessEventsWithinSubtree(false);
    } else {
      auto* favicon = AddChildView(std::make_unique<views::ImageView>());
      favicon->SetImage(link.favicon);
      favicon->SetImageSize(gfx::Size(18, 18));
      favicon->SetCanProcessEventsWithinSubtree(false);
      favicon->GetViewAccessibility().SetIsIgnored(true);
    }

    auto text_stack = std::make_unique<views::View>();
    auto* text_layout =
        text_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* title = text_stack->AddChildView(std::make_unique<views::Label>(
        link.title.empty() ? base::UTF8ToUTF16(link.url.host()) : link.title));
    title->SetSubpixelRenderingEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    title->SetEnabledColor(visual_style::kText);
    title->GetViewAccessibility().SetIsIgnored(true);
    auto* time = text_stack->AddChildView(
        std::make_unique<views::Label>(std::move(relative_time)));
    time->SetSubpixelRenderingEnabled(false);
    time->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    time->SetElideBehavior(gfx::ELIDE_TAIL);
    time->SetEnabledColor(visual_style::kMutedText);
    time->GetViewAccessibility().SetIsIgnored(true);
    auto* raw_text_stack = AddChildView(std::move(text_stack));
    raw_text_stack->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(raw_text_stack, 1);
    UpdateBackground();
  }

  const GURL& url() const { return url_; }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateBackground();
  }

 private:
  void UpdateBackground() {
    const bool highlighted = GetState() == ButtonState::STATE_HOVERED ||
                             GetState() == ButtonState::STATE_PRESSED;
    SetBackground(highlighted ? views::CreateRoundedRectBackground(
                                    visual_style::kHoverSurface,
                                    visual_style::kControlCornerRadius)
                              : nullptr);
  }

  const GURL url_;
};

BEGIN_METADATA(RecentGroupLinkRowView)
END_METADATA

class GroupRecentLinksView final : public views::View,
                                   public views::TextfieldController {
  METADATA_HEADER(GroupRecentLinksView, views::View)

 public:
  using ActivateCallback = base::RepeatingCallback<void(const base::Uuid&)>;
  using HoverCallback = base::RepeatingCallback<void(bool)>;

  GroupRecentLinksView(std::vector<RecentGroupLink> links,
                       ActivateCallback activate_callback,
                       HoverCallback hover_callback)
      : links_(std::move(links)),
        activate_callback_(std::move(activate_callback)),
        hover_callback_(std::move(hover_callback)) {
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto search_surface = std::make_unique<views::View>();
    search_surface->SetBackground(views::CreateRoundedRectBackground(
        visual_style::kRaisedSurface, visual_style::kControlCornerRadius));
    search_surface->SetBorder(views::CreateRoundedRectBorder(
        1, visual_style::kControlCornerRadius, visual_style::kDivider));
    auto* search_layout =
        search_surface->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(7, 9),
            7));
    search_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* search_icon = search_surface->AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kSearchIcon, visual_style::kMutedText, 17)));
    search_icon->SetCanProcessEventsWithinSubtree(false);
    search_icon->GetViewAccessibility().SetIsIgnored(true);
    search_field_ =
        search_surface->AddChildView(std::make_unique<views::Textfield>());
    const std::u16string placeholder =
        l10n_util::GetStringUTF16(IDS_AHOI_GROUP_RECENT_SEARCH);
    search_field_->SetPlaceholderText(placeholder);
    search_field_->SetAccessibleName(placeholder);
    search_field_->SetController(this);
    search_field_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    search_field_->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, visual_style::kSidebarSearchTextHorizontalInset)));
    search_field_->SetBackgroundColor(visual_style::kRaisedSurface);
    search_field_->RemoveHoverEffect();
    views::FocusRing::Install(search_field_);
    views::FocusRing::Get(search_field_)->SetOutsetFocusRingDisabled(true);
    search_layout->SetFlexForView(search_field_, 1);
    AddChildView(std::move(search_surface));

    results_container_ = AddChildView(std::make_unique<views::View>());
    auto* results_layout =
        results_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    results_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    RebuildResults();
  }

  GroupRecentLinksView(const GroupRecentLinksView&) = delete;
  GroupRecentLinksView& operator=(const GroupRecentLinksView&) = delete;
  ~GroupRecentLinksView() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size preferred = views::View::CalculatePreferredSize(available_size);
    preferred.set_width(320);
    // The search surface plus an empty row is already roughly 100 DIP. Keep a
    // useful lower bound and cap the six-result list so the bubble never grows
    // beyond an ordinary browser window while still sizing to its real
    // contents instead of clipping them at a fixed 90 DIP.
    preferred.set_height(std::clamp(preferred.height(), 100, 370));
    return preferred;
  }

  void UpdateFavicon(const GURL& url, ui::ImageModel favicon) {
    bool changed = false;
    for (RecentGroupLink& link : links_) {
      if (link.url == url) {
        link.favicon = favicon;
        changed = true;
      }
    }
    if (changed) {
      RebuildResults();
    }
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::View::OnMouseEntered(event);
    hover_callback_.Run(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    views::View::OnMouseExited(event);
    hover_callback_.Run(false);
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string&) override {
    if (sender == search_field_) {
      RebuildResults();
    }
  }

 private:
  void RebuildResults() {
    results_container_->RemoveAllChildViews();
    const std::u16string filter =
        base::i18n::FoldCase(search_field_->GetText());
    size_t result_count = 0;
    constexpr size_t kMaxVisibleResults = 6;
    for (const RecentGroupLink& link : links_) {
      const std::u16string searchable = base::i18n::FoldCase(
          link.title + u" " + base::UTF8ToUTF16(link.url.spec()));
      if (!filter.empty() && searchable.find(filter) == std::u16string::npos) {
        continue;
      }
      const base::TimeDelta elapsed =
          std::max(base::TimeDelta(), base::Time::Now() - link.last_visit);
      const std::u16string relative_time =
          ui::TimeFormat::SimpleWithMonthAndYear(ui::TimeFormat::FORMAT_ELAPSED,
                                                 ui::TimeFormat::LENGTH_LONG,
                                                 elapsed, true);
      results_container_->AddChildView(std::make_unique<RecentGroupLinkRowView>(
          base::BindRepeating([](ActivateCallback callback, base::Uuid node_id,
                                 const ui::Event&) { callback.Run(node_id); },
                              activate_callback_, link.node_id),
          link, relative_time));
      if (++result_count == kMaxVisibleResults) {
        break;
      }
    }
    if (result_count == 0) {
      auto* empty =
          results_container_->AddChildView(std::make_unique<views::Label>(
              l10n_util::GetStringUTF16(IDS_AHOI_GROUP_RECENT_EMPTY)));
      empty->SetSubpixelRenderingEnabled(false);
      empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      empty->SetEnabledColor(visual_style::kMutedText);
      empty->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(14, 9)));
    }
    results_container_->InvalidateLayout();
    InvalidateLayout();
    PreferredSizeChanged();
  }

  std::vector<RecentGroupLink> links_;
  const ActivateCallback activate_callback_;
  const HoverCallback hover_callback_;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;
};

BEGIN_METADATA(GroupRecentLinksView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateGroupRecentLinksView(
    std::vector<RecentGroupLink> links,
    ActivateRecentGroupLinkCallback activate_callback,
    RecentGroupLinksHoverCallback hover_callback) {
  return std::make_unique<GroupRecentLinksView>(std::move(links),
                                                std::move(activate_callback),
                                                std::move(hover_callback));
}

void UpdateGroupRecentLinkFavicon(views::View* view,
                                  const GURL& url,
                                  ui::ImageModel favicon) {
  if (auto* recent_links = views::AsViewClass<GroupRecentLinksView>(view)) {
    recent_links->UpdateFavicon(url, std::move(favicon));
  }
}

}  // namespace ahoi::sidebar
