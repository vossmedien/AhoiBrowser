// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <tuple>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

enum RemoteTabMenuCommand {
  kOpenHere = 1,
  kTakeOver,
  kFocusRemote,
};

bool UsesGermanUi() {
  return base::StartsWith(base::i18n::GetConfiguredLocale(), "de",
                          base::CompareCase::INSENSITIVE_ASCII);
}

std::u16string Text(std::u16string_view german, std::u16string_view english) {
  return std::u16string(UsesGermanUi() ? german : english);
}

const gfx::VectorIcon& DeviceIcon(sync::DeviceType type) {
  switch (type) {
    case sync::DeviceType::kMacDesktop:
      return vector_icons::kDesktopWindowsIcon;
    case sync::DeviceType::kIPhone:
      return kSmartphoneRefreshOldIcon;
    case sync::DeviceType::kIPad:
      return kTabletFilledIcon;
    case sync::DeviceType::kOther:
      return vector_icons::kDevicesIcon;
  }
}

class RemoteTabRowView final : public views::Button,
                               public views::ContextMenuController,
                               public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(RemoteTabRowView, views::Button)

 public:
  RemoteTabRowView(RemoteTabRowModel model, RemoteTabRowActions actions)
      : views::Button(base::BindRepeating(
            [](base::RepeatingCallback<void(sync::RemoteTabRecord)> callback,
               sync::RemoteTabRecord tab,
               const ui::Event&) {
              if (callback) {
                callback.Run(std::move(tab));
              }
            },
            actions.open_here,
            model.tab)),
        model_(std::move(model)),
        actions_(std::move(actions)) {
    SetPreferredSize(gfx::Size(0, SidebarTreeRowView::kRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetNotifyEnterExitOnChild(true);

    favicon_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_->SetImage(std::move(model_.favicon));
    favicon_->SetImageSize(gfx::Size(16, 16));
    favicon_->SetCanProcessEventsWithinSubtree(false);
    favicon_->GetViewAccessibility().SetIsIgnored(true);

    fallback_ = AddChildView(CreatePageFallbackIconView(false, false));
    fallback_->SetVisible(favicon_->GetImageModel().IsEmpty());
    fallback_->SetCanProcessEventsWithinSubtree(false);

    const std::u16string title = model_.tab.title.empty()
                                     ? base::UTF8ToUTF16(model_.tab.url)
                                     : base::UTF8ToUTF16(model_.tab.title);
    title_ = AddChildView(std::make_unique<views::Label>(title));
    title_->SetSubpixelRenderingEnabled(false);
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetEnabledColor(visual_style::kText);
    title_->SetCanProcessEventsWithinSubtree(false);
    title_->GetViewAccessibility().SetIsIgnored(true);

    device_ = AddChildView(std::make_unique<views::ImageView>());
    device_->SetImage(ui::ImageModel::FromVectorIcon(
        DeviceIcon(model_.device_type), visual_style::kMutedText, 15));
    device_->SetImageSize(gfx::Size(15, 15));
    device_->SetCanProcessEventsWithinSubtree(false);
    device_->GetViewAccessibility().SetIsIgnored(true);

    std::u16string details = model_.device_name;
    if (!model_.workspace_name.empty()) {
      if (!details.empty()) {
        details += u" · ";
      }
      details += model_.workspace_name;
    }
    if (!model_.relative_activity.empty()) {
      if (!details.empty()) {
        details += u" · ";
      }
      details += model_.relative_activity;
    }
    if (!model_.remote_status.empty()) {
      if (!details.empty()) {
        details += u" · ";
      }
      details += model_.remote_status;
    }
    SetTooltipText(details);
    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
    GetViewAccessibility().SetName(details.empty() ? title
                                                   : title + u" — " + details);
    set_context_menu_controller(this);
    UpdateBackground();
  }

  RemoteTabRowView(const RemoteTabRowView&) = delete;
  RemoteTabRowView& operator=(const RemoteTabRowView&) = delete;
  ~RemoteTabRowView() override = default;

  const sync::RemoteTabRecord& tab() const { return model_.tab; }

  void SetSearchSelected(bool selected) {
    if (search_selected_ == selected) {
      return;
    }
    search_selected_ = selected;
    GetViewAccessibility().SetIsSelected(search_selected_);
    UpdateBackground();
  }

  void Layout(PassKey) override {
    const gfx::Rect icon_bounds(8, std::max(0, (height() - 16) / 2), 16, 16);
    favicon_->SetBoundsRect(icon_bounds);
    fallback_->SetBoundsRect(icon_bounds);
    device_->SetBounds(width() - 25, std::max(0, (height() - 15) / 2), 15, 15);
    title_->SetBounds(30, 0, std::max(0, width() - 62), height());
  }

  void StateChanged(ButtonState) override { UpdateBackground(); }

  void OnFocus() override {
    views::Button::OnFocus();
    UpdateBackground();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdateBackground();
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& screen_point,
      ui::mojom::MenuSourceType source_type) override {
    if (!GetWidget() || source != this || menu_runner_) {
      return;
    }
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddItem(kOpenHere, Text(u"Hier öffnen", u"Open here"));
    if (model_.remote_actions_available && actions_.take_over) {
      menu_model_->AddItem(kTakeOver, Text(u"In diesen Workspace übernehmen",
                                           u"Take over in this workspace"));
    }
    if (model_.remote_actions_available && actions_.focus_remote) {
      menu_model_->AddItem(kFocusRemote,
                           Text(u"Auf dem anderen Gerät fokussieren",
                                u"Focus on the other device"));
    }
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(),
        views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
    menu_runner_->RunMenuAt(GetWidget(), nullptr,
                            gfx::Rect(screen_point, gfx::Size()),
                            views::MenuAnchorPosition::kTopLeft, source_type);
    menu_runner_.reset();
    menu_model_.reset();
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override {
    switch (command_id) {
      case kOpenHere:
        return !actions_.open_here.is_null();
      case kTakeOver:
        return model_.remote_actions_available && !actions_.take_over.is_null();
      case kFocusRemote:
        return model_.remote_actions_available &&
               !actions_.focus_remote.is_null();
      default:
        return false;
    }
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    std::ignore = event_flags;
    switch (command_id) {
      case kOpenHere:
        actions_.open_here.Run(model_.tab);
        return;
      case kTakeOver:
        actions_.take_over.Run(model_.tab);
        return;
      case kFocusRemote:
        actions_.focus_remote.Run(model_.tab);
        return;
      default:
        return;
    }
  }

 private:
  void UpdateBackground() {
    const bool selected = search_selected_ || HasFocus();
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(selected  ? views::CreateRoundedRectBackground(
                                  visual_style::kSelectedSurface,
                                  visual_style::kRowCornerRadius)
                  : hovered ? views::CreateRoundedRectBackground(
                                  visual_style::kHoverSurface,
                                  visual_style::kRowCornerRadius)
                            : nullptr);
    SchedulePaint();
  }

  RemoteTabRowModel model_;
  RemoteTabRowActions actions_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::View> fallback_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> device_ = nullptr;
  bool search_selected_ = false;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

BEGIN_METADATA(RemoteTabRowView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateRemoteTabRowView(
    RemoteTabRowModel model,
    RemoteTabRowActions actions) {
  return std::make_unique<RemoteTabRowView>(std::move(model),
                                            std::move(actions));
}

std::optional<sync::RemoteTabRecord> GetRemoteTabForView(views::View* view) {
  auto* row = views::AsViewClass<RemoteTabRowView>(view);
  return row ? std::make_optional(row->tab()) : std::nullopt;
}

void SetRemoteTabSearchSelected(views::View* view, bool selected) {
  if (auto* row = views::AsViewClass<RemoteTabRowView>(view)) {
    row->SetSearchSelected(selected);
  }
}

}  // namespace ahoi::sidebar
