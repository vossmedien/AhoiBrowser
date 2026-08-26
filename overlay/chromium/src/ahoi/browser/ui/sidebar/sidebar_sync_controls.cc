// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_sync_controls.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

constexpr int kControlHeight = 28;
constexpr int kStatusMaximumLines = 3;
constexpr int kDeviceIdPreviewLength = 8;
constexpr std::array kRetentionDays = {30, 90, 365, -1};

bool UsesGermanUi() {
  return base::StartsWith(base::i18n::GetConfiguredLocale(), "de",
                          base::CompareCase::INSENSITIVE_ASCII);
}

std::u16string Text(std::u16string_view german, std::u16string_view english) {
  return std::u16string(UsesGermanUi() ? german : english);
}

std::unique_ptr<views::Label> MakeMutedLabel(std::u16string text,
                                             bool multiline = false) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  label->SetMultiLine(multiline);
  if (multiline) {
    label->SetMaxLines(kStatusMaximumLines);
  }
  return label;
}

void StyleButton(views::LabelButton* button) {
  button->SetPreferredSize(gfx::Size(0, kControlHeight));
  button->SetTextSubpixelRenderingEnabled(false);
  button->SetTextColor(views::Button::STATE_NORMAL, visual_style::kText);
  button->SetTextColor(views::Button::STATE_HOVERED, visual_style::kText);
}

void StyleCombobox(views::Combobox* combobox, std::u16string accessible_name) {
  combobox->SetAccessibleName(std::move(accessible_name));
  combobox->SetBackgroundColorId(visual_style::kRaisedSurface);
  combobox->SetForegroundColorId(visual_style::kText);
  combobox->SetBorderColorId(visual_style::kDivider);
  combobox->SetPreferredSize(gfx::Size(0, kControlHeight));
}

std::u16string ShortDeviceId(const base::Uuid& id) {
  const std::string& serialized = id.AsLowercaseString();
  return base::UTF8ToUTF16(serialized.substr(
      0, std::min(serialized.size(),
                  static_cast<size_t>(kDeviceIdPreviewLength))));
}

class SidebarSyncControlsView final : public views::View {
  METADATA_HEADER(SidebarSyncControlsView, views::View)

 public:
  SidebarSyncControlsView(sync::ProfileSyncService* service,
                          std::vector<sync::DeviceRecord> filter_devices,
                          base::RepeatingClosure filter_changed_callback)
      : service_(service),
        filter_changed_callback_(std::move(filter_changed_callback)) {
    SetID(kSidebarSyncControlsViewId);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(2, 6, 6, 6)));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 5));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    SetAccessibleName(
        Text(u"Geräte-Tabs und Synchronisierung", u"Device tabs and sync"));

    auto filter_row = std::make_unique<views::View>();
    auto* filter_layout =
        filter_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    filter_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    filter_ = filter_row->AddChildView(std::make_unique<views::Combobox>());
    StyleCombobox(filter_, Text(u"Geräte-Tabs filtern", u"Filter device tabs"));
    filter_->SetCallback(
        base::BindRepeating(&SidebarSyncControlsView::OnFilterChanged,
                            weak_ptr_factory_.GetWeakPtr()));
    filter_layout->SetFlexForView(filter_, 1);
    settings_button_ =
        filter_row->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SidebarSyncControlsView::ToggleSettings,
                                weak_ptr_factory_.GetWeakPtr()),
            Text(u"Sync", u"Sync")));
    StyleButton(settings_button_);
    AddChildView(std::move(filter_row));

    settings_body_ = AddChildView(std::make_unique<views::View>());
    auto* settings_layout =
        settings_body_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical,
            gfx::Insets::TLBR(5, 0, 0, 0), 6));
    settings_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // Transport status belongs to the expanded settings disclosure. Keeping
    // it out of the always-visible filter row prevents an ordinary disabled
    // state from reading like a permanent warning between the user's tabs.
    status_label_ = settings_body_->AddChildView(MakeMutedLabel({}, true));

    sync_enabled_ =
        settings_body_->AddChildView(std::make_unique<views::Checkbox>(
            Text(u"CloudKit-Synchronisierung", u"CloudKit sync"),
            base::BindRepeating(&SidebarSyncControlsView::OnSyncToggled,
                                weak_ptr_factory_.GetWeakPtr())));
    remote_control_enabled_ =
        settings_body_->AddChildView(std::make_unique<views::Checkbox>(
            Text(u"Sichere Fernsteuerung empfangen",
                 u"Receive secure remote control"),
            base::BindRepeating(
                &SidebarSyncControlsView::OnRemoteControlToggled,
                weak_ptr_factory_.GetWeakPtr())));

    auto retention_row = std::make_unique<views::View>();
    auto* retention_layout =
        retention_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    retention_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* retention_label = retention_row->AddChildView(
        MakeMutedLabel(Text(u"Verlauf behalten", u"Keep history")));
    retention_layout->SetFlexForView(retention_label, 1);
    std::vector<ui::SimpleComboboxModel::Item> retention_items;
    for (int days : kRetentionDays) {
      std::u16string label;
      if (days < 0) {
        label = Text(u"Für immer", u"Forever");
      } else {
        label = base::NumberToString16(days);
        label += Text(u" Tage", u" days");
      }
      retention_items.emplace_back(std::move(label));
    }
    retention_ = retention_row->AddChildView(std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(std::move(retention_items))));
    StyleCombobox(retention_, Text(u"Aufbewahrungsdauer für den Verlauf",
                                   u"History retention period"));
    retention_->SetCallback(
        base::BindRepeating(&SidebarSyncControlsView::OnRetentionChanged,
                            weak_ptr_factory_.GetWeakPtr()));
    settings_body_->AddChildView(std::move(retention_row));

    auto action_row = std::make_unique<views::View>();
    auto* action_layout =
        action_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    sync_now_button_ =
        action_row->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SidebarSyncControlsView::SyncNow,
                                weak_ptr_factory_.GetWeakPtr()),
            Text(u"Jetzt synchronisieren", u"Sync now")));
    StyleButton(sync_now_button_);
    action_layout->SetFlexForView(sync_now_button_, 1);
    settings_body_->AddChildView(std::move(action_row));

    recovery_container_ =
        settings_body_->AddChildView(std::make_unique<views::View>());
    auto* recovery_layout = recovery_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    recovery_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    recovery_label_ = recovery_container_->AddChildView(MakeMutedLabel(
        Text(u"CloudKit-Wiederherstellung erfordert eine Bestätigung.",
             u"CloudKit recovery requires confirmation."),
        true));
    keep_local_button_ =
        recovery_container_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(
                &SidebarSyncControlsView::ConfirmAccountTransition,
                weak_ptr_factory_.GetWeakPtr(), true),
            Text(u"Lokale Daten weiter hochladen",
                 u"Continue uploading local data")));
    StyleButton(keep_local_button_);
    no_upload_button_ =
        recovery_container_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(
                &SidebarSyncControlsView::ConfirmAccountTransition,
                weak_ptr_factory_.GetWeakPtr(), false),
            Text(u"Ohne lokalen Upload fortfahren",
                 u"Continue without local upload")));
    StyleButton(no_upload_button_);
    zone_recovery_button_ =
        recovery_container_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SidebarSyncControlsView::ConfirmZoneRecovery,
                                weak_ptr_factory_.GetWeakPtr()),
            Text(u"Sync-Zone wiederherstellen", u"Recover sync zone")));
    StyleButton(zone_recovery_button_);

    settings_body_->AddChildView(std::make_unique<views::Separator>());
    settings_body_->AddChildView(MakeMutedLabel(
        Text(u"Companion-Gerät freigeben", u"Approve companion device")));
    approval_device_id_ =
        settings_body_->AddChildView(std::make_unique<views::Textfield>());
    approval_device_id_->SetPlaceholderText(
        Text(u"Geräte-UUID", u"Device UUID"));
    approval_device_id_->SetAccessibleName(
        Text(u"UUID des Companion-Geräts", u"Companion device UUID"));
    approval_public_key_ =
        settings_body_->AddChildView(std::make_unique<views::Textfield>());
    approval_public_key_->SetPlaceholderText(
        Text(u"Öffentlicher Ed25519-Schlüssel (Base64)",
             u"Ed25519 public key (Base64)"));
    approval_public_key_->SetAccessibleName(
        Text(u"Öffentlicher Companion-Schlüssel", u"Companion public key"));
    approve_button_ =
        settings_body_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SidebarSyncControlsView::ApproveDevice,
                                weak_ptr_factory_.GetWeakPtr()),
            Text(u"Gerät freigeben", u"Approve device")));
    StyleButton(approve_button_);
    approval_status_ = settings_body_->AddChildView(MakeMutedLabel({}, true));

    approved_devices_container_ =
        settings_body_->AddChildView(std::make_unique<views::View>());
    auto* approved_layout = approved_devices_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    approved_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    settings_body_->SetVisible(false);
    Update(service, std::move(filter_devices));
  }

  SidebarSyncControlsView(const SidebarSyncControlsView&) = delete;
  SidebarSyncControlsView& operator=(const SidebarSyncControlsView&) = delete;
  ~SidebarSyncControlsView() override = default;

  void Update(sync::ProfileSyncService* service,
              std::vector<sync::DeviceRecord> filter_devices) {
    service_ = service;
    UpdateFilterDevices(std::move(filter_devices));
    if (!service_) {
      SetEnabled(false);
      status_label_->SetText(
          Text(u"Sync-Anbieter nicht verfügbar", u"Sync provider unavailable"));
      return;
    }
    SetEnabled(true);
    updating_controls_ = true;
    sync_enabled_->SetChecked(service_->sync_enabled());
    remote_control_enabled_->SetChecked(service_->remote_control_enabled());
    const int retention_days = service_->history_retention_days();
    auto retention_it = std::ranges::find(kRetentionDays, retention_days);
    retention_->SetSelectedIndex(
        retention_it == std::end(kRetentionDays)
            ? std::optional<size_t>()
            : static_cast<size_t>(
                  std::distance(std::begin(kRetentionDays), retention_it)));
    updating_controls_ = false;

    const sync::SyncTransportStatus& status = service_->transport_status();
    status_label_->SetText(StatusText(status));
    recovery_container_->SetVisible(status.account_transition_pending ||
                                    status.zone_recovery_pending);
    recovery_label_->SetVisible(status.account_transition_pending ||
                                status.zone_recovery_pending);
    keep_local_button_->SetVisible(status.account_transition_pending);
    no_upload_button_->SetVisible(status.account_transition_pending);
    zone_recovery_button_->SetVisible(status.zone_recovery_pending);
    sync_now_button_->SetEnabled(service_->sync_enabled());

    std::vector<base::Uuid> approved =
        service_->approved_remote_control_devices();
    if (approved != approved_device_ids_) {
      approved_device_ids_ = std::move(approved);
      if (pending_revoke_ &&
          !std::ranges::contains(approved_device_ids_, *pending_revoke_)) {
        pending_revoke_.reset();
      }
      RebuildApprovedDevices();
    }
  }

  bool MatchesDevice(const base::Uuid& device_id) const {
    return !selected_device_id_.has_value() || selected_device_id_ == device_id;
  }

  void SetSettingsExpandedForTesting(bool expanded) {
    SetSettingsExpanded(expanded);
  }

  bool SettingsExpandedForTesting() const {
    return settings_body_->GetVisible();
  }

  bool StatusVisibleForTesting() const {
    return settings_body_->GetVisible() && status_label_->GetVisible();
  }

  std::u16string StatusTextForTesting() const {
    return std::u16string(status_label_->GetText());
  }

 private:
  std::u16string StatusText(const sync::SyncTransportStatus& status) const {
    if (!status.enabled) {
      return Text(u"Sync ist ausgeschaltet", u"Sync is off");
    }
    if (!status.provider_available) {
      return Text(u"Nur lokal · CloudKit nicht verfügbar",
                  u"Local only · CloudKit unavailable");
    }
    if (status.account_transition_pending) {
      return Text(u"iCloud-Accountwechsel wartet auf Bestätigung",
                  u"iCloud account change awaits confirmation");
    }
    if (status.zone_recovery_pending) {
      return Text(u"Sync-Zone wartet auf Wiederherstellung",
                  u"Sync zone awaits recovery");
    }
    if (status.retry.attempt > 0) {
      std::u16string value =
          Text(u"Neuer Sync-Versuch geplant", u"Sync retry scheduled");
      if (!status.retry.last_error.empty()) {
        value += u" · ";
        value += base::UTF8ToUTF16(status.retry.last_error);
      }
      return value;
    }
    if (status.pending_outbox > 0) {
      std::u16string value =
          Text(u"Ausstehende Änderungen: ", u"Pending changes: ");
      value += base::NumberToString16(status.pending_outbox);
      return value;
    }
    return Text(u"Synchronisiert und bereit", u"Synced and ready");
  }

  void UpdateFilterDevices(std::vector<sync::DeviceRecord> devices) {
    std::erase_if(devices, [](const sync::DeviceRecord& device) {
      return device.tombstone || device.retired || !device.id.is_valid();
    });
    std::ranges::sort(devices, [](const sync::DeviceRecord& left,
                                  const sync::DeviceRecord& right) {
      return std::tie(left.display_name, left.id) <
             std::tie(right.display_name, right.id);
    });
    devices.erase(std::unique(devices.begin(), devices.end(),
                              [](const sync::DeviceRecord& left,
                                 const sync::DeviceRecord& right) {
                                return left.id == right.id;
                              }),
                  devices.end());

    std::vector<base::Uuid> ids;
    std::vector<std::u16string> labels;
    ids.emplace_back();
    labels.push_back(Text(u"Alle Geräte", u"All devices"));
    for (const sync::DeviceRecord& device : devices) {
      ids.push_back(device.id);
      labels.push_back(device.display_name.empty()
                           ? ShortDeviceId(device.id)
                           : base::UTF8ToUTF16(device.display_name));
    }
    if (ids == filter_device_ids_ && labels == filter_device_labels_) {
      return;
    }
    filter_device_ids_ = std::move(ids);
    filter_device_labels_ = std::move(labels);
    filter_->SetVisible(filter_device_ids_.size() > 1u);
    if (selected_device_id_ &&
        !std::ranges::contains(filter_device_ids_, *selected_device_id_)) {
      selected_device_id_.reset();
    }
    std::vector<ui::SimpleComboboxModel::Item> items;
    items.reserve(filter_device_labels_.size());
    for (const std::u16string& label : filter_device_labels_) {
      items.emplace_back(label);
    }
    filter_->SetOwnedModel(
        std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
    size_t selected_index = 0;
    if (selected_device_id_) {
      const auto selected =
          std::ranges::find(filter_device_ids_, *selected_device_id_);
      if (selected != filter_device_ids_.end()) {
        selected_index = static_cast<size_t>(
            std::distance(filter_device_ids_.begin(), selected));
      }
    }
    updating_controls_ = true;
    filter_->SetSelectedIndex(selected_index);
    updating_controls_ = false;
  }

  void OnFilterChanged() {
    if (updating_controls_) {
      return;
    }
    const std::optional<size_t> selected = filter_->GetSelectedIndex();
    if (!selected || *selected == 0 || *selected >= filter_device_ids_.size()) {
      selected_device_id_.reset();
    } else {
      selected_device_id_ = filter_device_ids_[*selected];
    }
    filter_changed_callback_.Run();
  }

  void ToggleSettings(const ui::Event&) {
    SetSettingsExpanded(!settings_body_->GetVisible());
  }

  void SetSettingsExpanded(bool expanded) {
    settings_body_->SetVisible(expanded);
    settings_button_->SetText(expanded ? Text(u"Fertig", u"Done")
                                       : Text(u"Sync", u"Sync"));
    InvalidateLayout();
    PreferredSizeChanged();
  }

  void OnSyncToggled(const ui::Event&) {
    if (service_ && !updating_controls_) {
      service_->SetSyncEnabled(sync_enabled_->GetChecked());
      service_->Refresh();
    }
  }

  void OnRemoteControlToggled(const ui::Event&) {
    if (service_ && !updating_controls_) {
      service_->SetRemoteControlEnabled(remote_control_enabled_->GetChecked());
      service_->Refresh();
    }
  }

  void OnRetentionChanged() {
    if (!service_ || updating_controls_) {
      return;
    }
    const std::optional<size_t> selected = retention_->GetSelectedIndex();
    if (selected && *selected < kRetentionDays.size()) {
      std::ignore =
          service_->SetHistoryRetentionDays(kRetentionDays.at(*selected));
      service_->Refresh();
    }
  }

  void SyncNow(const ui::Event&) {
    if (service_) {
      service_->SyncNow();
    }
  }

  void ConfirmAccountTransition(bool allow_local_upload, const ui::Event&) {
    if (service_) {
      service_->ConfirmCloudKitAccountTransition(allow_local_upload);
    }
  }

  void ConfirmZoneRecovery(const ui::Event&) {
    if (service_) {
      service_->ConfirmCloudKitZoneRecovery();
    }
  }

  void ApproveDevice(const ui::Event&) {
    if (!service_) {
      return;
    }
    const base::Uuid device_id =
        base::Uuid::ParseCaseInsensitive(approval_device_id_->GetText());
    std::string public_key = base::UTF16ToUTF8(approval_public_key_->GetText());
    public_key =
        std::string(base::TrimWhitespaceASCII(public_key, base::TRIM_ALL));
    if (!device_id.is_valid() || public_key.empty() ||
        !service_->ApproveRemoteControlDevice(device_id, public_key)) {
      approval_status_->SetText(
          Text(u"UUID oder öffentlicher Schlüssel ist ungültig.",
               u"The UUID or public key is invalid."));
      return;
    }
    approval_device_id_->SetText({});
    approval_public_key_->SetText({});
    approval_status_->SetText(
        Text(u"Gerät sicher freigegeben.", u"Device approved securely."));
    service_->Refresh();
  }

  void RevokeDevice(base::Uuid device_id, const ui::Event&) {
    if (!service_) {
      return;
    }
    if (pending_revoke_ != device_id) {
      pending_revoke_ = device_id;
      approval_status_->SetText(Text(u"Zum Widerrufen erneut bestätigen.",
                                     u"Press again to confirm revocation."));
      RebuildApprovedDevices();
      return;
    }
    service_->RevokeRemoteControlDevice(device_id);
    pending_revoke_.reset();
    approval_status_->SetText(
        Text(u"Gerätefreigabe widerrufen.", u"Device approval revoked."));
    service_->Refresh();
  }

  void RebuildApprovedDevices() {
    approved_devices_container_->RemoveAllChildViews();
    if (approved_device_ids_.empty()) {
      approved_devices_container_->AddChildView(
          MakeMutedLabel(Text(u"Keine freigegebenen Companion-Geräte",
                              u"No approved companion devices")));
      return;
    }
    approved_devices_container_->AddChildView(
        MakeMutedLabel(Text(u"Freigegebene Geräte", u"Approved devices")));
    for (const base::Uuid& device_id : approved_device_ids_) {
      auto row = std::make_unique<views::View>();
      auto* row_layout =
          row->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
      row_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* id = row->AddChildView(MakeMutedLabel(ShortDeviceId(device_id)));
      id->SetTooltipText(base::UTF8ToUTF16(device_id.AsLowercaseString()));
      row_layout->SetFlexForView(id, 1);
      auto* revoke = row->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SidebarSyncControlsView::RevokeDevice,
                              weak_ptr_factory_.GetWeakPtr(), device_id),
          pending_revoke_ == device_id
              ? Text(u"Widerruf bestätigen", u"Confirm revoke")
              : Text(u"Widerrufen", u"Revoke")));
      StyleButton(revoke);
      approved_devices_container_->AddChildView(std::move(row));
    }
    approved_devices_container_->InvalidateLayout();
  }

  raw_ptr<sync::ProfileSyncService> service_ = nullptr;
  base::RepeatingClosure filter_changed_callback_;
  std::vector<base::Uuid> filter_device_ids_;
  std::vector<std::u16string> filter_device_labels_;
  std::optional<base::Uuid> selected_device_id_;
  std::vector<base::Uuid> approved_device_ids_;
  std::optional<base::Uuid> pending_revoke_;
  raw_ptr<views::Combobox> filter_ = nullptr;
  raw_ptr<views::LabelButton> settings_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::View> settings_body_ = nullptr;
  raw_ptr<views::Checkbox> sync_enabled_ = nullptr;
  raw_ptr<views::Checkbox> remote_control_enabled_ = nullptr;
  raw_ptr<views::Combobox> retention_ = nullptr;
  raw_ptr<views::LabelButton> sync_now_button_ = nullptr;
  raw_ptr<views::View> recovery_container_ = nullptr;
  raw_ptr<views::Label> recovery_label_ = nullptr;
  raw_ptr<views::LabelButton> keep_local_button_ = nullptr;
  raw_ptr<views::LabelButton> no_upload_button_ = nullptr;
  raw_ptr<views::LabelButton> zone_recovery_button_ = nullptr;
  raw_ptr<views::Textfield> approval_device_id_ = nullptr;
  raw_ptr<views::Textfield> approval_public_key_ = nullptr;
  raw_ptr<views::LabelButton> approve_button_ = nullptr;
  raw_ptr<views::Label> approval_status_ = nullptr;
  raw_ptr<views::View> approved_devices_container_ = nullptr;
  bool updating_controls_ = false;
  base::WeakPtrFactory<SidebarSyncControlsView> weak_ptr_factory_{this};
};

BEGIN_METADATA(SidebarSyncControlsView)
END_METADATA

SidebarSyncControlsView* AsSyncControls(views::View* view) {
  return views::AsViewClass<SidebarSyncControlsView>(view);
}

const SidebarSyncControlsView* AsSyncControls(const views::View* view) {
  return views::AsViewClass<SidebarSyncControlsView>(view);
}

}  // namespace

std::unique_ptr<views::View> CreateSidebarSyncControlsView(
    sync::ProfileSyncService* service,
    std::vector<sync::DeviceRecord> filter_devices,
    base::RepeatingClosure filter_changed_callback) {
  return std::make_unique<SidebarSyncControlsView>(
      service, std::move(filter_devices), std::move(filter_changed_callback));
}

void UpdateSidebarSyncControlsView(
    views::View* view,
    sync::ProfileSyncService* service,
    std::vector<sync::DeviceRecord> filter_devices) {
  if (SidebarSyncControlsView* controls = AsSyncControls(view)) {
    controls->Update(service, std::move(filter_devices));
  }
}

bool SidebarSyncControlsMatchesDevice(const views::View* view,
                                      const base::Uuid& device_id) {
  const SidebarSyncControlsView* controls = AsSyncControls(view);
  return !controls || controls->MatchesDevice(device_id);
}

void SetSidebarSyncSettingsExpandedForTesting(views::View* view,
                                              bool expanded) {
  if (SidebarSyncControlsView* controls = AsSyncControls(view)) {
    controls->SetSettingsExpandedForTesting(expanded);
  }
}

bool SidebarSyncSettingsExpandedForTesting(const views::View* view) {
  const SidebarSyncControlsView* controls = AsSyncControls(view);
  return controls && controls->SettingsExpandedForTesting();
}

bool SidebarSyncStatusVisibleForTesting(const views::View* view) {
  const SidebarSyncControlsView* controls = AsSyncControls(view);
  return controls && controls->StatusVisibleForTesting();
}

std::u16string SidebarSyncStatusTextForTesting(const views::View* view) {
  const SidebarSyncControlsView* controls = AsSyncControls(view);
  return controls ? controls->StatusTextForTesting() : std::u16string();
}

}  // namespace ahoi::sidebar
