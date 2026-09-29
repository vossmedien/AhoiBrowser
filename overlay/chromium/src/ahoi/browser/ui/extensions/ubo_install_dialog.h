// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_DIALOG_H_
#define AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_DIALOG_H_

#include "ahoi/browser/extensions/ubo_service.h"
#include "ahoi/browser/ui/extensions/ubo_install_presenter.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class LabelButton;
class ProgressBar;
class ScrollView;
class Widget;
}  // namespace views

namespace ahoi::extensions {

class UboInstallDialog final : public views::DialogDelegate,
                               public UboService::Observer {
 public:
  ~UboInstallDialog() override;

  static views::Widget* CreateWidget(
      Browser* browser,
      UboService* service,
      UboInstallDialog** dialog_for_testing = nullptr);

  UboInstallDialog(const UboInstallDialog&) = delete;
  UboInstallDialog& operator=(const UboInstallDialog&) = delete;

  std::u16string GetWindowTitle() const override;
  bool Accept() override;
  bool Cancel() override;

  void OnUboServiceStatusChanged(const UboServiceStatus& status) override;

 private:
  UboInstallDialog(Browser* browser, UboService* service);
  void HandleDialogClosed();
  void ToggleDetails();
  void Update(const UboServiceStatus& status);

  raw_ptr<Browser> browser_;
  raw_ptr<UboService> service_;
  bool prompt_handoff_pending_ = false;
  UboDialogAction action_ = UboDialogAction::kNone;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::ProgressBar> progress_ = nullptr;
  raw_ptr<views::View> summary_ = nullptr;
  raw_ptr<views::Label> version_ = nullptr;
  raw_ptr<views::Label> source_ = nullptr;
  raw_ptr<views::LabelButton> details_button_ = nullptr;
  raw_ptr<views::ScrollView> details_ = nullptr;
  raw_ptr<views::View> metadata_ = nullptr;
  raw_ptr<views::Label> extension_id_ = nullptr;
  raw_ptr<views::Label> upstream_ = nullptr;
  raw_ptr<views::Label> hash_ = nullptr;
  raw_ptr<views::Label> license_ = nullptr;
  raw_ptr<views::Label> pinned_classic_inventory_ = nullptr;
  raw_ptr<views::Label> former_classic_inventory_ = nullptr;
  raw_ptr<views::Label> lite_inventory_ = nullptr;

  base::WeakPtrFactory<UboInstallDialog> weak_ptr_factory_{this};
};

void ShowUboInstallDialog(Browser* browser);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_DIALOG_H_
