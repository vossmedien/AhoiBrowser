// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <memory>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace ahoi::sidebar {

namespace {

constexpr size_t kMaxSupplementalSearchResults = 4u;
constexpr size_t kMaxSupplementalRecentlyClosedResults = 4u;
constexpr int kSupplementalResultRowHeight = 46;
constexpr int kSupplementalResultSpacing = 2;
constexpr int kSearchHeaderHeight = 44;
constexpr int kSearchHeaderVerticalInset = 5;
constexpr int kSearchHeaderLeadingInset = 8;
constexpr int kSearchHeaderTrailingInset = 6;
constexpr int kSearchHeaderControlSpacing = 8;
constexpr int kSearchHeaderIconBoxSize = 20;
constexpr int kSearchHeaderIconSize = 17;
constexpr int kSearchHeaderCloseButtonSize = 32;
constexpr int kSearchFieldVerticalInset = 3;

class SidebarDiscoverySearchField final : public views::Textfield {
  METADATA_HEADER(SidebarDiscoverySearchField, views::Textfield)

 public:
  explicit SidebarDiscoverySearchField(views::View* focus_shell)
      : focus_shell_(focus_shell) {
    CHECK(focus_shell_);
  }

  void OnFocus() override {
    views::Textfield::OnFocus();
    views::FocusRing::Get(focus_shell_.get())->Refresh();
  }

  void OnBlur() override {
    views::Textfield::OnBlur();
    views::FocusRing::Get(focus_shell_.get())->Refresh();
  }

 private:
  const raw_ptr<views::View> focus_shell_;
};

BEGIN_METADATA(SidebarDiscoverySearchField)
END_METADATA

class SidebarDiscoveryCloseButton final : public views::Button {
  METADATA_HEADER(SidebarDiscoveryCloseButton, views::Button)

 public:
  explicit SidebarDiscoveryCloseButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(
        gfx::Size(kSearchHeaderCloseButtonSize, kSearchHeaderCloseButtonSize));
    const std::u16string close = l10n_util::GetStringUTF16(IDS_CLOSE);
    SetAccessibleName(close);
    SetTooltipText(close);
    icon_ = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kCloseIcon, visual_style::kMutedText, 16)));
    icon_->SetCanProcessEventsWithinSubtree(false);
    icon_->GetViewAccessibility().SetIsIgnored(true);
  }

  void Layout(PassKey) override { icon_->SetBoundsRect(GetLocalBounds()); }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const bool highlighted = GetState() == ButtonState::STATE_HOVERED ||
                             GetState() == ButtonState::STATE_PRESSED ||
                             HasFocus();
    SetBackground(highlighted ? views::CreateRoundedRectBackground(
                                    visual_style::kHoverSurface,
                                    visual_style::kRowCornerRadius)
                              : nullptr);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
};

BEGIN_METADATA(SidebarDiscoveryCloseButton)
END_METADATA

}  // namespace

SidebarDiscoveryView::SidebarDiscoveryView(
    SidebarDiscoveryModel* model,
    std::unique_ptr<views::View> primary_surface,
    FilterCallback filter_callback,
    PrimaryResultCallback primary_result_callback,
    ActivateCommandCallback activate_command_callback,
    RestoreCallback restore_callback,
    CloseCallback close_callback)
    : model_(model),
      filter_callback_(std::move(filter_callback)),
      primary_result_callback_(std::move(primary_result_callback)),
      activate_command_callback_(std::move(activate_command_callback)),
      restore_callback_(std::move(restore_callback)),
      close_callback_(std::move(close_callback)) {
  CHECK(model_);
  CHECK(primary_surface);
  CHECK(filter_callback_);
  CHECK(primary_result_callback_);
  CHECK(activate_command_callback_);
  CHECK(restore_callback_);
  CHECK(close_callback_);
  model_->AddObserver(this);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  auto input_shell = std::make_unique<views::View>();
  input_shell->SetPreferredSize(gfx::Size(0, kSearchHeaderHeight));
  input_shell->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kRaisedSurface, visual_style::kControlCornerRadius));
  input_shell->SetBorder(views::CreateRoundedRectBorder(
      visual_style::kControlBorderThickness, visual_style::kControlCornerRadius,
      visual_style::kDivider));
  auto* input_layout =
      input_shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(
              kSearchHeaderVerticalInset, kSearchHeaderLeadingInset,
              kSearchHeaderVerticalInset, kSearchHeaderTrailingInset),
          kSearchHeaderControlSpacing));
  input_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* search_icon = input_shell->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, visual_style::kMutedText,
          kSearchHeaderIconSize)));
  search_icon_ = search_icon;
  search_icon->SetPreferredSize(
      gfx::Size(kSearchHeaderIconBoxSize, kSearchHeaderIconBoxSize));
  search_icon->SetImageSize(
      gfx::Size(kSearchHeaderIconSize, kSearchHeaderIconSize));
  search_icon->SetCanProcessEventsWithinSubtree(false);
  search_icon->GetViewAccessibility().SetIsIgnored(true);

  input_shell_ = input_shell.get();
  search_field_ = input_shell->AddChildView(
      std::make_unique<SidebarDiscoverySearchField>(input_shell_));
  const std::u16string placeholder =
      l10n_util::GetStringUTF16(IDS_AHOI_SIDEBAR_DISCOVERY_SEARCH);
  search_field_->SetController(this);
  search_field_->SetPlaceholderText(placeholder);
  search_field_->SetAccessibleName(placeholder);
  search_field_->SetFocusBehavior(FocusBehavior::ALWAYS);
  search_field_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kSearchFieldVerticalInset,
                      visual_style::kSidebarSearchTextHorizontalInset)));
  search_field_->SetBackgroundColor(visual_style::kRaisedSurface);
  search_field_->SetTextColorId(visual_style::kText);
  search_field_->SetPlaceholderTextColorId(visual_style::kMutedText);
  search_field_->RemoveHoverEffect();
  views::FocusRing::Install(input_shell_);
  views::FocusRing* const input_focus_ring =
      views::FocusRing::Get(input_shell_);
  input_focus_ring->SetOutsetFocusRingDisabled(true);
  // Keep the focus stroke inside the rounded header. This matters when the
  // sidebar is at its minimum width and the header sits directly against the
  // sidebar clip edge.
  input_focus_ring->SetHaloInset(0.0f);
  input_focus_ring->SetColorId(visual_style::kAccent);
  input_focus_ring->SetHasFocusPredicate(base::BindRepeating(
      [](const views::Textfield* search_field, const views::View*) {
        return search_field->HasFocus();
      },
      search_field_.get()));
  views::InstallRoundRectHighlightPathGenerator(
      input_shell_, gfx::Insets(), visual_style::kControlCornerRadius);
  input_layout->SetFlexForView(search_field_, 1);
  close_button_ = input_shell->AddChildView(
      std::make_unique<SidebarDiscoveryCloseButton>(base::BindRepeating(
          [](base::WeakPtr<SidebarDiscoveryView> view, const ui::Event&) {
            if (view) {
              view->CloseOrClear();
            }
          },
          weak_ptr_factory_.GetWeakPtr())));
  AddChildView(std::move(input_shell));
  input_shell_->SetVisible(false);

  no_results_label_ = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_NO_RESULTS_FOUND)));
  no_results_label_->SetSubpixelRenderingEnabled(false);
  no_results_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  no_results_label_->SetEnabledColor(visual_style::kMutedText);
  no_results_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(2, 9, 4, 9)));
  no_results_label_->SetVisible(false);

  primary_surface_ = AddChildView(std::move(primary_surface));
  layout->SetFlexForView(primary_surface_, 1, /*use_min_size=*/true);

  auto supplemental_section = std::make_unique<views::View>();
  auto* supplemental_section_layout =
      supplemental_section->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 3));
  supplemental_section_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  supplemental_section_label_ =
      supplemental_section->AddChildView(std::make_unique<views::Label>());
  supplemental_section_label_->SetSubpixelRenderingEnabled(false);
  supplemental_section_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  supplemental_section_label_->SetEnabledColor(visual_style::kMutedText);
  supplemental_section_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(2, 8, 0, 8)));
  auto supplemental_results_container = std::make_unique<views::View>();
  auto* supplemental_results_layout =
      supplemental_results_container->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(),
              kSupplementalResultSpacing));
  supplemental_results_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  supplemental_results_container->GetViewAccessibility().SetRole(
      ax::mojom::Role::kList);
  supplemental_results_container_ = supplemental_results_container.get();

  auto supplemental_scroll = std::make_unique<views::ScrollView>();
  supplemental_scroll->SetBackground(nullptr);
  supplemental_scroll->SetDrawOverflowIndicator(false);
  supplemental_scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  supplemental_scroll->SetUseContentsPreferredSize(true);
  supplemental_scroll->ClipHeightTo(
      0, static_cast<int>(kMaxSupplementalSearchResults) *
                 kSupplementalResultRowHeight +
             (static_cast<int>(kMaxSupplementalSearchResults) - 1) *
                 kSupplementalResultSpacing);
  supplemental_scroll->SetContents(std::move(supplemental_results_container));
  supplemental_results_scroll_view_ =
      supplemental_section->AddChildView(std::move(supplemental_scroll));
  supplemental_section_ = AddChildView(std::move(supplemental_section));
  supplemental_section_->SetVisible(false);

  auto recently_closed_section = std::make_unique<views::View>();
  auto* recently_closed_section_layout =
      recently_closed_section->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(), 3));
  recently_closed_section_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* recently_closed_label =
      recently_closed_section->AddChildView(std::make_unique<views::Label>(
          l10n_util::GetStringUTF16(IDS_TAB_SEARCH_RECENTLY_CLOSED)));
  recently_closed_label->SetSubpixelRenderingEnabled(false);
  recently_closed_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  recently_closed_label->SetEnabledColor(visual_style::kMutedText);
  recently_closed_label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(2, 8, 0, 8)));
  auto recently_closed_results_container = std::make_unique<views::View>();
  auto* recently_closed_results_layout =
      recently_closed_results_container->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(),
              kSupplementalResultSpacing));
  recently_closed_results_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  recently_closed_results_container->GetViewAccessibility().SetRole(
      ax::mojom::Role::kList);
  recently_closed_results_container->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_RECENTLY_CLOSED));
  recently_closed_results_container_ = recently_closed_results_container.get();

  auto recently_closed_scroll = std::make_unique<views::ScrollView>();
  recently_closed_scroll->SetBackground(nullptr);
  recently_closed_scroll->SetDrawOverflowIndicator(false);
  recently_closed_scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  recently_closed_scroll->SetUseContentsPreferredSize(true);
  recently_closed_scroll->ClipHeightTo(
      0, static_cast<int>(kMaxSupplementalRecentlyClosedResults) *
                 kSupplementalResultRowHeight +
             (static_cast<int>(kMaxSupplementalRecentlyClosedResults) - 1) *
                 kSupplementalResultSpacing);
  recently_closed_scroll->SetContents(
      std::move(recently_closed_results_container));
  recently_closed_results_scroll_view_ =
      recently_closed_section->AddChildView(std::move(recently_closed_scroll));
  recently_closed_section_ = AddChildView(std::move(recently_closed_section));
  recently_closed_section_->SetVisible(false);

  // Search filters these inline regions; it does not open a popup. Expose the
  // real control relationship without claiming combobox/listbox state.
  search_field_->GetViewAccessibility().SetControlIds(
      {primary_surface_->GetViewAccessibility().GetUniqueId(),
       supplemental_results_container_->GetViewAccessibility().GetUniqueId(),
       recently_closed_results_container_->GetViewAccessibility()
           .GetUniqueId()});
}

SidebarDiscoveryView::~SidebarDiscoveryView() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (search_field_) {
    search_field_->SetController(nullptr);
  }
  model_->RemoveObserver(this);
}

}  // namespace ahoi::sidebar
