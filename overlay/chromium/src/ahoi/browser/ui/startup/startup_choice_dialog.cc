// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/startup/startup_choice_dialog.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/base_window.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/widget/widget.h"

namespace ahoi::startup {
namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kRememberStartupChoiceCheckbox);

raw_ptr<views::Widget> g_dialog_for_testing = nullptr;

void ClearDialogForTesting() {
  g_dialog_for_testing = nullptr;
}

class StartupChoiceDialogDelegate final : public ui::DialogModelDelegate {
 public:
  explicit StartupChoiceDialogDelegate(StartupChoiceCallback callback)
      : callback_(std::move(callback)) {}

  StartupChoiceDialogDelegate(const StartupChoiceDialogDelegate&) = delete;
  StartupChoiceDialogDelegate& operator=(const StartupChoiceDialogDelegate&) =
      delete;

  ~StartupChoiceDialogDelegate() override {
    // A native widget can disappear without invoking a button or close action.
    // Preserve the fail-closed one-run empty result in that case.
    Complete(StartupChoice::kEmpty, /*honor_remember=*/false);
  }

  void Continue() {
    Complete(StartupChoice::kContinue, /*honor_remember=*/true);
  }

  void Empty() { Complete(StartupChoice::kEmpty, /*honor_remember=*/true); }

  void Close() { Complete(StartupChoice::kEmpty, /*honor_remember=*/false); }

 private:
  void Complete(StartupChoice choice, bool honor_remember) {
    if (!callback_) {
      return;
    }
    StartupChoiceResult result{
        .choice = choice,
        .remember = honor_remember && dialog_model() &&
                    dialog_model()
                        ->GetCheckboxByUniqueId(kRememberStartupChoiceCheckbox)
                        ->is_checked(),
    };
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback_), result));
  }

  StartupChoiceCallback callback_;
};

}  // namespace

bool ShowStartupChoiceDialog(BrowserWindowInterface* browser,
                             StartupChoiceCallback callback) {
  if (!browser || browser->IsDeleteScheduled() || !browser->GetWindow() ||
      !callback) {
    return false;
  }
  auto delegate =
      std::make_unique<StartupChoiceDialogDelegate>(std::move(callback));
  auto* delegate_ptr = delegate.get();
  auto dialog =
      ui::DialogModel::Builder(std::move(delegate))
          .SetTitle(l10n_util::GetStringUTF16(IDS_AHOI_STARTUP_CHOICE_TITLE))
          .AddParagraph(ui::DialogModelLabel(
              l10n_util::GetStringUTF16(IDS_AHOI_STARTUP_CHOICE_BODY)))
          .AddCheckbox(kRememberStartupChoiceCheckbox,
                       ui::DialogModelLabel(l10n_util::GetStringUTF16(
                           IDS_AHOI_STARTUP_CHOICE_REMEMBER)))
          .AddOkButton(
              base::BindOnce(&StartupChoiceDialogDelegate::Continue,
                             base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params().SetLabel(
                  l10n_util::GetStringUTF16(IDS_AHOI_STARTUP_CHOICE_CONTINUE)))
          .AddCancelButton(
              base::BindOnce(&StartupChoiceDialogDelegate::Empty,
                             base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params().SetLabel(
                  l10n_util::GetStringUTF16(IDS_AHOI_STARTUP_CHOICE_EMPTY)))
          .SetCloseActionCallback(
              base::BindOnce(&StartupChoiceDialogDelegate::Close,
                             base::Unretained(delegate_ptr)))
          .SetDialogDestroyingCallback(base::BindOnce(&ClearDialogForTesting))
          .OverrideShowCloseButton(true)
          .Build();
  views::Widget* widget = constrained_window::ShowBrowserModal(
      std::move(dialog), browser->GetWindow()->GetNativeWindow());
  if (!widget) {
    return false;
  }
  g_dialog_for_testing = widget;
  return true;
}

views::Widget* GetStartupChoiceDialogForTesting() {
  return g_dialog_for_testing;
}

bool SetRememberStartupChoiceForTesting(bool remember) {
  if (!g_dialog_for_testing) {
    return false;
  }
  const ui::ElementContext context =
      views::ElementTrackerViews::GetContextForWidget(g_dialog_for_testing);
  views::Checkbox* checkbox = views::ElementTrackerViews::GetInstance()
                                  ->GetUniqueViewAs<views::Checkbox>(
                                      kRememberStartupChoiceCheckbox, context);
  if (!checkbox) {
    return false;
  }
  if (checkbox->GetChecked() != remember) {
    checkbox->button_controller()->NotifyClick();
  }
  return checkbox->GetChecked() == remember;
}

}  // namespace ahoi::startup
