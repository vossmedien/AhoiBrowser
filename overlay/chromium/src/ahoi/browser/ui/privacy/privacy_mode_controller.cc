// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/privacy/privacy_mode_controller.h"

#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/ui/privacy/privacy_mode_bubble_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace ahoi {

namespace {

bool IsSupportedSite(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

std::optional<privacy::PrivacyMode> GetExplicitOriginMode(
    const PrefService& prefs,
    const GURL& url,
    bool is_off_the_record) {
  if (is_off_the_record || !IsSupportedSite(url)) {
    return std::nullopt;
  }
  const privacy::PrivacyPolicy policy =
      privacy::GetPolicySnapshot(prefs, false);
  const std::string origin_key = url::Origin::Create(url).Serialize();
  const auto found = policy.origin_modes.find(origin_key);
  return found == policy.origin_modes.end()
             ? std::nullopt
             : std::optional<privacy::PrivacyMode>(found->second);
}

}  // namespace

PrivacyModeController::PrivacyModeController(Browser* browser)
    : browser_(browser) {
  CHECK(browser_);
}

PrivacyModeController::~PrivacyModeController() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

bool PrivacyModeController::Show(views::View* anchor_view) {
  content::WebContents* const contents = GetActiveWebContents();
  Profile* const profile = browser_ ? browser_->profile() : nullptr;
  if (!anchor_view || !anchor_view->GetWidget() || !contents || !profile) {
    return false;
  }
  if (bubble_widget_) {
    bubble_widget_->Close();
    return true;
  }

  GURL url = contents->GetLastCommittedURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetVisibleURL();
  }
  const bool site_supported = IsSupportedSite(url);
  const bool is_off_the_record = profile->IsOffTheRecord();
  PrefService* const prefs = profile->GetPrefs();
  const std::u16string origin_label =
      site_supported ? base::UTF8ToUTF16(url::Origin::Create(url).Serialize())
                     : l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_BROWSER_PAGE);

  auto view = std::make_unique<PrivacyModeBubbleView>(
      origin_label, privacy::GetGlobalMode(*prefs),
      GetExplicitOriginMode(*prefs, url, is_off_the_record),
      privacy::IsGlobalModeManaged(*prefs), site_supported, is_off_the_record,
      base::BindRepeating(
          [](base::WeakPtr<PrivacyModeController> controller,
             privacy::PrivacyMode mode) {
            return controller && controller->SetGlobalMode(mode);
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<PrivacyModeController> controller,
             std::optional<privacy::PrivacyMode> mode) {
            return controller && controller->SetOriginMode(mode);
          },
          weak_ptr_factory_.GetWeakPtr()));

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_TITLE));
  delegate->set_fixed_width(visual_style::kDeveloperToolkitWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kDeveloperToolkitInset,
                                        visual_style::kDeveloperToolkitInset));
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  delegate->SetBackgroundColor(visual_style::kChromeSurface);
  delegate->set_close_on_deactivate(true);
  delegate->SetContentsView(std::move(view));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&PrivacyModeController::OnBubbleClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    return false;
  }
  bubble_delegate_ = std::move(delegate);
  bubble_widget_ = std::move(widget);
  bubble_widget_->Show();
  return true;
}

bool PrivacyModeController::CanShow() const {
  return browser_ && browser_->profile() && GetActiveWebContents();
}

content::WebContents* PrivacyModeController::GetActiveWebContents() const {
  return browser_ && browser_->tab_strip_model()
             ? browser_->tab_strip_model()->GetActiveWebContents()
             : nullptr;
}

bool PrivacyModeController::SetGlobalMode(privacy::PrivacyMode mode) {
  Profile* const profile = browser_ ? browser_->profile() : nullptr;
  if (!profile || profile->IsOffTheRecord() ||
      !privacy::SetGlobalMode(profile->GetPrefs(), mode)) {
    return false;
  }
  ReloadActivePage();
  return true;
}

bool PrivacyModeController::SetOriginMode(
    std::optional<privacy::PrivacyMode> mode) {
  Profile* const profile = browser_ ? browser_->profile() : nullptr;
  content::WebContents* const contents = GetActiveWebContents();
  if (!profile || profile->IsOffTheRecord() || !contents) {
    return false;
  }
  const GURL url = contents->GetLastCommittedURL();
  if (!privacy::SetOriginMode(profile->GetPrefs(), url, mode, false)) {
    return false;
  }
  ReloadActivePage();
  return true;
}

void PrivacyModeController::ReloadActivePage() {
  if (content::WebContents* const contents = GetActiveWebContents()) {
    contents->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/true);
  }
}

void PrivacyModeController::OnBubbleClosed() {
  std::unique_ptr<views::Widget> closed_widget = std::move(bubble_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(bubble_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       widget.reset();
                       delegate.reset();
                     },
                     std::move(closed_widget), std::move(closed_delegate)));
}

}  // namespace ahoi
