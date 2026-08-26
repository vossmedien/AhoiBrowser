// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/popup/popup_overlay_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/popup/popup_overlay_layout.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_shadow.h"
#include "url/url_constants.h"

namespace ahoi::popup_ui {

namespace {

void ConfigureActionButton(views::ImageButton* button,
                           const gfx::VectorIcon& icon,
                           std::u16string accessible_name) {
  button->SetPreferredSize(gfx::Size(visual_style::kPopupActionButtonSize,
                                     visual_style::kPopupActionButtonSize));
  button->SetMinimumImageSize(gfx::Size(visual_style::kPopupActionIconSize,
                                        visual_style::kPopupActionIconSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetAccessibleName(accessible_name);
  button->SetTooltipText(accessible_name);
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::SetImageFromVectorIconWithColor(
      button, icon,
      {visual_style::kText, visual_style::kDisabledIcon, visual_style::kAccent},
      visual_style::kPopupActionIconSize);
}

}  // namespace

class PopupOverlayView::PopupScrimView final : public views::View {
  METADATA_HEADER(PopupScrimView, views::View)

 public:
  explicit PopupScrimView(base::RepeatingClosure pressed)
      : pressed_(std::move(pressed)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    SetBackground(views::CreateSolidBackground(visual_style::kModalScrim));
    SetCanProcessEventsWithinSubtree(true);
    GetViewAccessibility().SetIsIgnored(true);
    GetViewAccessibility().SetIsInvisible(true);
  }

  PopupScrimView(const PopupScrimView&) = delete;
  PopupScrimView& operator=(const PopupScrimView&) = delete;
  ~PopupScrimView() override = default;

  bool OnMousePressed(const ui::MouseEvent&) override {
    pressed_.Run();
    return true;
  }

  bool OnMouseWheel(const ui::MouseWheelEvent&) override { return true; }

  void OnScrollEvent(ui::ScrollEvent* event) override { event->SetHandled(); }

  void OnGestureEvent(ui::GestureEvent* event) override {
    if (event->type() == ui::EventType::kGestureTapDown) {
      pressed_.Run();
    }
    event->SetHandled();
  }

 private:
  base::RepeatingClosure pressed_;
};

PopupOverlayView::PopupOverlayView(content::BrowserContext* browser_context,
                                   base::RepeatingClosure close_callback,
                                   base::RepeatingClosure promote_callback,
                                   base::RepeatingClosure split_callback,
                                   const appearance::GlassPolicy& policy)
    : close_callback_(std::move(close_callback)),
      promote_callback_(std::move(promote_callback)),
      split_callback_(std::move(split_callback)),
      reduced_motion_(policy.reduced_motion) {
  SetCanProcessEventsWithinSubtree(true);

  scrim_ = AddChildView(std::make_unique<PopupScrimView>(close_callback_));

  auto card = std::make_unique<views::View>();
  auto* card_layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  const appearance::SurfaceAppearance surface_appearance =
      appearance::AppearanceResolver::Resolve(appearance::SurfaceRole::kPopup,
                                              policy);
  appearance::ApplySurfaceAppearance(card.get(), surface_appearance);

  auto origin_row = std::make_unique<views::View>();
  origin_row->SetPreferredSize(
      gfx::Size(0, visual_style::kPopupOriginRowHeight));
  auto* origin_layout =
      origin_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 12),
          8));
  origin_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  origin_icon_ = origin_row->AddChildView(std::make_unique<views::ImageView>());
  origin_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kGlobeIcon, visual_style::kMutedText, 16));
  origin_icon_->GetViewAccessibility().SetIsIgnored(true);
  origin_icon_->GetViewAccessibility().SetIsInvisible(true);
  origin_label_ = origin_row->AddChildView(std::make_unique<views::Label>());
  origin_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  origin_label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  origin_label_->SetEnabledColor(visual_style::kMutedText);
  origin_label_->SetSubpixelRenderingEnabled(false);
  origin_layout->SetFlexForView(origin_label_, 1);
  card->AddChildView(std::move(origin_row));

  status_label_ = card->AddChildView(std::make_unique<views::Label>());
  status_label_->SetPreferredSize(
      gfx::Size(0, visual_style::kPopupStatusRowHeight));
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  status_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
  status_label_->SetVisible(false);

  web_view_ =
      card->AddChildView(std::make_unique<views::WebView>(browser_context));
  web_view_->SetFastResize(true);
  web_view_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AHOI_POPUP_WEB_CONTENT),
      ax::mojom::NameFrom::kAttribute);
  card_layout->SetFlexForView(web_view_, 1);

  card_ = AddChildView(std::move(card));
  card_shadow_ = std::make_unique<views::ViewShadow>(
      card_, visual_style::kContentCardShadowElevation);
  card_shadow_->SetRoundedCornerRadius(surface_appearance.corner_radius);

  auto action_rail = std::make_unique<views::View>();
  auto* action_layout =
      action_rail->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 4), 6));
  action_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  appearance::ApplySurfaceAppearance(action_rail.get(), surface_appearance);

  close_button_ =
      action_rail->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(
              [](base::RepeatingClosure callback) { callback.Run(); },
              close_callback_),
          kCloseIcon, visual_style::kPopupActionIconSize, visual_style::kText,
          visual_style::kDisabledIcon, visual_style::kAccent));
  ConfigureActionButton(close_button_, kCloseIcon,
                        l10n_util::GetStringUTF16(IDS_AHOI_POPUP_CLOSE));

  promote_button_ =
      action_rail->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(
              [](base::RepeatingClosure callback) { callback.Run(); },
              promote_callback_),
          kOpenInNewIcon, visual_style::kPopupActionIconSize,
          visual_style::kText, visual_style::kDisabledIcon,
          visual_style::kAccent));
  ConfigureActionButton(promote_button_, kOpenInNewIcon,
                        l10n_util::GetStringUTF16(IDS_AHOI_POPUP_OPEN_AS_TAB));

  split_button_ =
      action_rail->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(
              [](base::RepeatingClosure callback) { callback.Run(); },
              split_callback_),
          kSplitSceneIcon, visual_style::kPopupActionIconSize,
          visual_style::kText, visual_style::kDisabledIcon,
          visual_style::kAccent));
  ConfigureActionButton(
      split_button_, kSplitSceneIcon,
      l10n_util::GetStringUTF16(IDS_AHOI_POPUP_OPEN_IN_SPLIT));
  action_rail_ = AddChildView(std::move(action_rail));

  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetIsModal(true);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AHOI_POPUP_ACCESSIBLE_NAME),
      ax::mojom::NameFrom::kTitle);
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_COMMAND_DOWN));
  AddAccelerator(ui::Accelerator(ui::VKEY_RETURN,
                                 ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
}

PopupOverlayView::~PopupOverlayView() {
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  attached_contents_ = nullptr;
}

void PopupOverlayView::SetWebContents(content::WebContents* contents) {
  if (attached_contents_ != contents) {
    web_view_->SetWebContents(contents);
    attached_contents_ = contents;
  }
}

void PopupOverlayView::SetDisplayedOrigin(const url::Origin& origin) {
  const bool secure = !origin.opaque() && origin.scheme() == url::kHttpsScheme;
  origin_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      secure ? vector_icons::kLockIcon : vector_icons::kGlobeIcon,
      visual_style::kMutedText, 16));
  origin_label_->SetText(
      origin.opaque()
          ? l10n_util::GetStringUTF16(IDS_AHOI_POPUP_ORIGIN_UNAVAILABLE)
          : base::UTF8ToUTF16(origin.Serialize()));
}

void PopupOverlayView::SetAppearance(const appearance::GlassPolicy& policy) {
  const bool reduce_motion_just_enabled =
      policy.reduced_motion && !reduced_motion_;
  reduced_motion_ = policy.reduced_motion;
  const appearance::SurfaceAppearance surface_appearance =
      appearance::AppearanceResolver::Resolve(appearance::SurfaceRole::kPopup,
                                              policy);
  const float card_opacity = card_->layer()->opacity();
  const float rail_opacity = action_rail_->layer()->opacity();
  appearance::ApplySurfaceAppearance(card_, surface_appearance);
  appearance::ApplySurfaceAppearance(action_rail_, surface_appearance);
  card_->layer()->SetOpacity(card_opacity);
  action_rail_->layer()->SetOpacity(rail_opacity);
  card_shadow_->SetRoundedCornerRadius(surface_appearance.corner_radius);

  if (reduce_motion_just_enabled && show_animation_started_ && !dismissing_) {
    // A runtime accessibility change must not leave a half-revealed surface.
    scrim_->layer()->GetAnimator()->AbortAllAnimations();
    card_->layer()->GetAnimator()->AbortAllAnimations();
    action_rail_->layer()->GetAnimator()->AbortAllAnimations();
    scrim_->layer()->SetOpacity(visual_style::kPopupScrimOpacity);
    card_->layer()->SetOpacity(1.0f);
    action_rail_->layer()->SetOpacity(1.0f);
    card_->layer()->SetTransform(gfx::Transform());
    action_rail_->layer()->SetTransform(gfx::Transform());
  }
}

void PopupOverlayView::SetPreferredCardSize(const gfx::Size& preferred_size) {
  preferred_card_size_.SetSize(std::max(0, preferred_size.width()),
                               std::max(0, preferred_size.height()));
  InvalidateLayout();
}

void PopupOverlayView::SetSplitAvailability(
    popup::PopupSplitAvailability availability) {
  // Keep the action keyboard-reachable even when preflight will reject it.
  // Invoking it then presents a visible and VoiceOver-announced explanation
  // instead of making a full four-pane split look like a broken control.
  split_button_->SetEnabled(true);
  if (availability == popup::PopupSplitAvailability::kSplitFull) {
    split_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_AHOI_POPUP_SPLIT_FULL));
  } else if (availability != popup::PopupSplitAvailability::kAvailable) {
    split_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_AHOI_POPUP_SPLIT_UNAVAILABLE));
  } else {
    split_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_AHOI_POPUP_OPEN_IN_SPLIT));
  }
}

void PopupOverlayView::ShowStatus(std::u16string message, bool is_error) {
  status_label_->SetText(message);
  status_label_->SetEnabledColor(is_error ? ui::kColorAlertHighSeverity
                                          : visual_style::kMutedText);
  status_label_->SetVisible(true);
  status_label_->GetViewAccessibility().AnnounceAlert(message);
  InvalidateLayout();
}

void PopupOverlayView::ClearStatus() {
  status_label_->SetVisible(false);
  status_label_->SetText(std::u16string());
  InvalidateLayout();
}

void PopupOverlayView::FocusWebContents() {
  web_view_->RequestFocus();
}

void PopupOverlayView::AnimateIn() {
  if (show_animation_started_ || dismissing_) {
    return;
  }
  show_animation_started_ = true;

  ui::Layer* const scrim_layer = scrim_->layer();
  ui::Layer* const card_layer = card_->layer();
  ui::Layer* const rail_layer = action_rail_->layer();
  scrim_layer->GetAnimator()->AbortAllAnimations();
  card_layer->GetAnimator()->AbortAllAnimations();
  rail_layer->GetAnimator()->AbortAllAnimations();

  if (reduced_motion_) {
    scrim_layer->SetOpacity(visual_style::kPopupScrimOpacity);
    card_layer->SetOpacity(1.0f);
    rail_layer->SetOpacity(1.0f);
    card_layer->SetTransform(gfx::Transform());
    rail_layer->SetTransform(gfx::Transform());
    return;
  }

  gfx::Transform reveal_transform;
  reveal_transform.Translate(0, visual_style::kPopupRevealOffset);
  scrim_layer->SetOpacity(0.0f);
  card_layer->SetOpacity(0.0f);
  rail_layer->SetOpacity(0.0f);
  card_layer->SetTransform(reveal_transform);
  rail_layer->SetTransform(reveal_transform);

  views::AnimationBuilder builder;
  auto& sequence = builder
                       .SetPreemptionStrategy(
                           ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
                       .Once()
                       .SetDuration(gfx::Animation::RichAnimationDuration(
                           visual_style::kPopupRevealDuration));
  sequence
      .SetOpacity(scrim_layer, visual_style::kPopupScrimOpacity,
                  gfx::Tween::EASE_OUT)
      .SetOpacity(card_layer, 1.0f, gfx::Tween::EASE_OUT)
      .SetTransform(card_layer, gfx::Transform(), gfx::Tween::EASE_OUT)
      .SetOpacity(rail_layer, 1.0f, gfx::Tween::EASE_OUT)
      .SetTransform(rail_layer, gfx::Transform(), gfx::Tween::EASE_OUT);
}

void PopupOverlayView::AnimateOut(base::OnceClosure completed) {
  if (dismissing_ || completed.is_null()) {
    return;
  }
  dismissing_ = true;

  ui::Layer* const scrim_layer = scrim_->layer();
  ui::Layer* const card_layer = card_->layer();
  ui::Layer* const rail_layer = action_rail_->layer();
  if (reduced_motion_) {
    // Defer removal so the controller never deletes this View from inside its
    // own member function when reduced motion maps the duration to zero.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(completed));
    return;
  }

  gfx::Transform dismiss_transform;
  dismiss_transform.Translate(0, visual_style::kPopupRevealOffset);
  auto completion_pair = base::SplitOnceCallback(std::move(completed));
  views::AnimationBuilder builder;
  auto& sequence = builder
                       .SetPreemptionStrategy(
                           ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
                       .OnEnded(std::move(completion_pair.first))
                       .OnAborted(std::move(completion_pair.second))
                       .Once()
                       .SetDuration(gfx::Animation::RichAnimationDuration(
                           visual_style::kPopupDismissDuration));
  sequence.SetOpacity(scrim_layer, 0.0f, gfx::Tween::EASE_IN)
      .SetOpacity(card_layer, 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(card_layer, dismiss_transform, gfx::Tween::EASE_IN)
      .SetOpacity(rail_layer, 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(rail_layer, dismiss_transform, gfx::Tween::EASE_IN);
}

void PopupOverlayView::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  scrim_->SetBoundsRect(bounds);
  if (bounds.IsEmpty()) {
    card_->SetBoundsRect(gfx::Rect());
    action_rail_->SetBoundsRect(gfx::Rect());
    return;
  }

  const PopupOverlayLayout layout =
      CalculatePopupOverlayLayout(bounds, preferred_card_size_);
  card_->SetBoundsRect(layout.card_bounds);
  action_rail_->SetBoundsRect(layout.action_rail_bounds);
}

bool PopupOverlayView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE &&
      accelerator.modifiers() == ui::EF_NONE) {
    close_callback_.Run();
    return true;
  }
  if (accelerator.key_code() == ui::VKEY_RETURN && accelerator.IsShiftDown() &&
      accelerator.IsCmdDown()) {
    split_callback_.Run();
    return true;
  }
  if (accelerator.key_code() == ui::VKEY_RETURN && accelerator.IsCmdDown()) {
    promote_callback_.Run();
    return true;
  }
  return false;
}

BEGIN_METADATA(PopupOverlayView, PopupScrimView)
END_METADATA

BEGIN_METADATA(PopupOverlayView)
END_METADATA

}  // namespace ahoi::popup_ui
