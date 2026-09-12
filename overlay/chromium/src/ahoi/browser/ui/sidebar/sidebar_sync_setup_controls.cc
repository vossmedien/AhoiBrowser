// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_sync_setup_controls.h"

#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

using Disposition = sync::ExtensionRestoreDisposition;
std::u16string Text(std::u16string_view de, std::u16string_view en) {
  return std::u16string(
      base::StartsWith(base::i18n::GetConfiguredLocale(), "de") ? de : en);
}

std::u16string Status(Disposition value) {
  switch (value) {
    case Disposition::kApplied:
      return Text(u"Übernommen", u"Applied");
    case Disposition::kNeedsConfirmation:
      return Text(u"Bestätigung nötig", u"Confirmation required");
    case Disposition::kPending:
      return Text(u"Wird vorbereitet", u"Pending");
    case Disposition::kBlockedByPolicy:
      return Text(u"Durch Richtlinie gesperrt", u"Blocked by policy");
    case Disposition::kUnsupported:
      return Text(u"Separat einrichten", u"Set up separately");
    case Disposition::kFailed:
      return Text(u"Fehlgeschlagen", u"Failed");
    case Disposition::kCancelled:
      return Text(u"Abgebrochen", u"Cancelled");
  }
}

class SetupControls final : public views::View {
  METADATA_HEADER(SetupControls, views::View)
 public:
  SetupControls() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    enabled_ = AddChildView(std::make_unique<views::Checkbox>(
        Text(u"Erweiterungen synchronisieren", u"Sync extensions"),
        base::BindRepeating(&SetupControls::Toggle,
                            weak_factory_.GetWeakPtr())));
    settings_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
        Text(u"Unterstützte Erweiterungsoptionen",
             u"Supported extension settings"),
        base::BindRepeating(&SetupControls::ToggleSettings,
                            weak_factory_.GetWeakPtr())));
    settings_enabled_->SetTooltipText(
        Text(u"Vimium: weiches Scrollen, Linkhinweise, HUD und Updatehinweise. "
             u"Andere Daten bleiben lokal.",
             u"Vimium: smooth scrolling, link hints, HUD and update notices. "
             u"Other data stays local."));
    key_retry_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SetupControls::RetryKey,
                            weak_factory_.GetWeakPtr()),
        Text(u"Sync-Verbindung erneut prüfen",
             u"Check sync connection again")));
    rows_ = AddChildView(std::make_unique<views::View>());
    rows_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  }
  ~SetupControls() override = default;

  void Update(sync::ProfileSyncService* service) {
    service_ = service;
    SetEnabled(service != nullptr);
    if (!service) {
      return;
    }
    enabled_->SetChecked(service->extension_setup_sync_enabled());
    settings_enabled_->SetChecked(service->extension_settings_sync_enabled());
    key_retry_->SetVisible(
        !service->transport_status().key_setup_issue.empty());
    key_retry_->SetEnabled(service->sync_enabled());
    std::map<std::string, std::tuple<base::Uuid, std::string, Disposition>>
        current;
    for (const auto& [id, result] : service->extension_setup_results()) {
      current.emplace(id, std::make_tuple(result.operation_id, result.revision,
                                          result.disposition));
    }
    rows_->SetEnabled(service->sync_enabled() &&
                      service->extension_setup_sync_enabled());
    if (current == results_) {
      return;
    }
    results_ = std::move(current);
    rows_->RemoveAllChildViews();
    for (const auto& [id, result] : results_) {
      const auto disposition = std::get<2>(result);
      auto label = std::make_unique<views::Label>(
          base::UTF8ToUTF16(id.substr(0, 8)) + u" · " + Status(disposition));
      label->SetTooltipText(base::UTF8ToUTF16(id));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetEnabledColor(visual_style::kMutedText);
      rows_->AddChildView(std::move(label));
      if (disposition == Disposition::kNeedsConfirmation ||
          disposition == Disposition::kFailed ||
          disposition == Disposition::kCancelled) {
        rows_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SetupControls::RetryExtension,
                                weak_factory_.GetWeakPtr(), id),
            disposition == Disposition::kNeedsConfirmation
                ? Text(u"Erweiterung einrichten…", u"Set up extension…")
                : Text(u"Erneut versuchen", u"Retry")));
      }
    }
    PreferredSizeChanged();
  }

 private:
  void Toggle(const ui::Event&) {
    if (service_) {
      std::ignore =
          service_->SetExtensionSetupSyncEnabled(enabled_->GetChecked());
    }
  }
  void RetryKey(const ui::Event&) {
    if (service_) {
      service_->RetrySyncKeySetup();
    }
  }
  void ToggleSettings(const ui::Event&) {
    if (service_) {
      std::ignore = service_->SetExtensionSettingsSyncEnabled(
          settings_enabled_->GetChecked());
    }
  }
  void RetryExtension(std::string id, const ui::Event&) {
    if (service_) {
      std::ignore = service_->RetryExtensionSetup(std::move(id));
    }
  }
  raw_ptr<sync::ProfileSyncService> service_ = nullptr;
  raw_ptr<views::Checkbox> enabled_ = nullptr;
  raw_ptr<views::Checkbox> settings_enabled_ = nullptr;
  raw_ptr<views::LabelButton> key_retry_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
  std::map<std::string, std::tuple<base::Uuid, std::string, Disposition>>
      results_;
  base::WeakPtrFactory<SetupControls> weak_factory_{this};
};

BEGIN_METADATA(SetupControls)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateSidebarSyncSetupControls() {
  return std::make_unique<SetupControls>();
}
void UpdateSidebarSyncSetupControls(views::View* view,
                                    sync::ProfileSyncService* service) {
  views::AsViewClass<SetupControls>(view)->Update(service);
}

}  // namespace ahoi::sidebar
