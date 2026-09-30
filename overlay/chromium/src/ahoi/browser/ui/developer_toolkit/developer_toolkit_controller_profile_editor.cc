// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The per-origin developer profile editor surface of
// DeveloperToolkitController (split from developer_toolkit_controller.cc,
// source line budget).

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "ahoi/browser/ui/developer_toolkit/developer_profile_editor_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_controller.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace ahoi {

void DeveloperToolkitController::OpenProfileEditor(views::View* anchor_view) {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
  ShowProfileEditor(anchor_view);
}

bool DeveloperToolkitController::ShowProfileEditor(views::View* anchor_view) {
  content::WebContents* const contents = GetActiveWebContents();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!anchor_view || !anchor_view->GetWidget() || !contents || !profile ||
      profile->IsOffTheRecord() || !IsSupportedDeveloperTarget(contents)) {
    return false;
  }
  if (profile_editor_widget_) {
    profile_editor_widget_->Activate();
    return true;
  }

  const url::Origin origin =
      url::Origin::Create(contents->GetLastCommittedURL());
  DeveloperProfileTabHelper* const tab_helper =
      DeveloperProfileTabHelper::FromWebContents(contents);
  PrefDeveloperProfileStore fallback_store(profile->GetPrefs(), false);
  std::optional<DeveloperProfile> existing =
      tab_helper ? tab_helper->GetProfile(origin) : fallback_store.Get(origin);
  DeveloperProfile initial;
  if (existing) {
    initial = *existing;
  } else {
    initial.name = std::string(contents->GetLastCommittedURL().host());
  }

  auto editor = std::make_unique<DeveloperProfileEditorView>(
      base::UTF8ToUTF16(origin.Serialize()), std::move(initial),
      existing.has_value(), contents,
      base::BindRepeating(&CreatePlatformDeveloperSecretStore),
      base::BindRepeating(
          [](base::WeakPtr<DeveloperToolkitController> controller,
             base::WeakPtr<content::WebContents> source_contents,
             url::Origin source_origin,
             const DeveloperProfile& developer_profile) {
            return controller && source_contents &&
                   controller->GetActiveWebContents() ==
                       source_contents.get() &&
                   url::Origin::Create(
                       source_contents->GetLastCommittedURL()) ==
                       source_origin &&
                   controller->SaveProfile(developer_profile);
          },
          weak_ptr_factory_.GetWeakPtr(), contents->GetWeakPtr(), origin),
      base::BindRepeating(
          [](base::WeakPtr<DeveloperToolkitController> controller,
             base::WeakPtr<content::WebContents> source_contents,
             url::Origin source_origin) {
            return controller && source_contents &&
                   controller->GetActiveWebContents() ==
                       source_contents.get() &&
                   url::Origin::Create(
                       source_contents->GetLastCommittedURL()) ==
                       source_origin &&
                   controller->RemoveProfile();
          },
          weak_ptr_factory_.GetWeakPtr(), contents->GetWeakPtr(), origin),
      base::BindRepeating(&DeveloperToolkitController::CloseProfileEditor,
                          weak_ptr_factory_.GetWeakPtr()),
      profile->GetPrefs());
  profile_editor_view_ = editor.get();

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetTitle(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_TITLE));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                       static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetButtonLabel(
      ui::mojom::DialogButton::kOk,
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SAVE));
  delegate->SetButtonLabel(
      ui::mojom::DialogButton::kCancel,
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_CANCEL));
  delegate->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<DeveloperToolkitController> controller) {
        return !controller || !controller->profile_editor_view_ ||
               controller->profile_editor_view_->Save();
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(false);
  delegate->set_fixed_width(visual_style::kDeveloperProfileEditorWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kDeveloperToolkitInset,
                                        visual_style::kDeveloperToolkitInset));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  delegate->SetInitiallyFocusedView(editor->initially_focused_view());
  delegate->SetContentsView(std::move(editor));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&DeveloperToolkitController::OnProfileEditorClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    profile_editor_view_ = nullptr;
    return false;
  }
  delegate->GetBubbleFrameView()->default_title()->SetSubpixelRenderingEnabled(
      false);
  delegate->GetOkButton()->SetTextSubpixelRenderingEnabled(false);
  delegate->GetCancelButton()->SetTextSubpixelRenderingEnabled(false);
  profile_editor_delegate_ = std::move(delegate);
  profile_editor_widget_ = std::move(widget);
  profile_editor_view_->ReapplyAppearance();
  profile_editor_widget_->Show();
  return true;
}

bool DeveloperToolkitController::SaveProfile(
    const DeveloperProfile& developer_profile) {
  content::WebContents* const contents = GetActiveWebContents();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!contents || !profile || profile->IsOffTheRecord() ||
      !IsSupportedDeveloperTarget(contents)) {
    return false;
  }
  const url::Origin origin =
      url::Origin::Create(contents->GetLastCommittedURL());
  DeveloperProfileTabHelper* const tab_helper =
      DeveloperProfileTabHelper::FromWebContents(contents);
  PrefDeveloperProfileStore fallback_store(profile->GetPrefs(), false);
  if (!(tab_helper ? tab_helper->SaveProfile(origin, developer_profile)
                   : fallback_store.Set(origin, developer_profile))) {
    return false;
  }
  ApplyAhoiUserAgentOverride(*contents, &developer_profile);
  if (content::NavigationEntry* entry =
          contents->GetController().GetLastCommittedEntry()) {
    entry->SetIsOverridingUserAgent(developer_profile.user_agent_enabled);
  }
  contents->GetController().Reload(content::ReloadType::NORMAL, true);
  return true;
}

bool DeveloperToolkitController::RemoveProfile() {
  content::WebContents* const contents = GetActiveWebContents();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!contents || !profile || profile->IsOffTheRecord() ||
      !IsSupportedDeveloperTarget(contents)) {
    return false;
  }
  const url::Origin origin =
      url::Origin::Create(contents->GetLastCommittedURL());
  DeveloperProfileTabHelper* const tab_helper =
      DeveloperProfileTabHelper::FromWebContents(contents);
  PrefDeveloperProfileStore fallback_store(profile->GetPrefs(), false);
  if (!(tab_helper ? tab_helper->RemoveProfile(origin)
                   : fallback_store.Remove(origin))) {
    return false;
  }
  ApplyAhoiUserAgentOverride(*contents, nullptr);
  if (content::NavigationEntry* entry =
          contents->GetController().GetLastCommittedEntry()) {
    entry->SetIsOverridingUserAgent(false);
  }
  contents->GetController().Reload(content::ReloadType::NORMAL, true);
  return true;
}

void DeveloperToolkitController::CloseProfileEditor() {
  if (profile_editor_widget_) {
    profile_editor_widget_->Close();
  }
}
}  // namespace ahoi
