// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_view.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/grit/generated_resources.h"
#include "crypto/process_bound_string.h"
#include "crypto/secure_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace ahoi {
namespace {

constexpr size_t kMaximumKeychainLabelBytes = 96;
constexpr std::u16string_view kMaskedValue = u"••••••••";

void SecurelyClearString(std::string* value) {
  if (!value || value->empty()) {
    return;
  }
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*value));
  value->clear();
}

void SecurelyClearString(std::u16string* value) {
  if (!value || value->empty()) {
    return;
  }
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*value));
  value->clear();
}

class SecureDeveloperSecret {
 public:
  explicit SecureDeveloperSecret(std::string* source)
      : bytes_(source->begin(), source->end()) {
    SecurelyClearString(source);
  }
  SecureDeveloperSecret(const SecureDeveloperSecret&) = delete;
  SecureDeveloperSecret& operator=(const SecureDeveloperSecret&) = delete;
  SecureDeveloperSecret(SecureDeveloperSecret&&) noexcept = default;
  SecureDeveloperSecret& operator=(SecureDeveloperSecret&&) noexcept = default;
  ~SecureDeveloperSecret() { Clear(); }

  std::string_view value() const {
    return std::string_view(reinterpret_cast<const char*>(bytes_.data()),
                            bytes_.size());
  }

 private:
  void Clear() {
    if (bytes_.empty()) {
      return;
    }
    crypto::SecureZeroBuffer(base::as_writable_byte_span(bytes_));
    bytes_.clear();
  }

  std::vector<uint8_t, crypto::SecureAllocator<uint8_t>> bytes_;
};

void RemoveSecretOnWorker(DeveloperSecretStoreFactory factory,
                          std::string reference) {
  std::unique_ptr<DeveloperSecretStore> store = factory.Run();
  if (store) {
    store->Remove(reference);
  }
}

void PostRemoveSecret(DeveloperSecretStoreFactory factory,
                      std::string reference) {
  if (factory.is_null() || reference.empty()) {
    return;
  }
  DeveloperSecretStoreFactory fallback_factory = factory;
  std::string fallback_reference = reference;
  if (!base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce(&RemoveSecretOnWorker, std::move(factory),
                         std::move(reference)))) {
    // Shutdown may reject a newly posted cleanup task. This bounded fallback
    // is preferable to leaving an uncommitted Keychain item behind.
    RemoveSecretOnWorker(std::move(fallback_factory),
                         std::move(fallback_reference));
  }
}

std::unique_ptr<views::Label> MakeLabel(
    std::u16string text,
    int style = views::style::STYLE_BODY_4) {
  auto label = std::make_unique<views::Label>(
      std::move(text), views::style::CONTEXT_LABEL, style);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::MdTextButton> MakeSmallButton(
    views::Button::PressedCallback callback,
    int string_id) {
  auto button = std::make_unique<views::MdTextButton>(
      std::move(callback), l10n_util::GetStringUTF16(string_id));
  button->SetCustomPadding(gfx::Insets::VH(4, 8));
  button->SetCornerRadius(visual_style::kRowCornerRadius);
  button->SetTextSubpixelRenderingEnabled(false);
  return button;
}

std::string MakeKeychainLabel(std::string_view header_name) {
  std::string label = "Ahoi header: ";
  label.append(header_name);
  label.resize(std::min(label.size(), kMaximumKeychainLabelBytes));
  return label;
}

bool StoreSecretOnWorker(
    DeveloperSecretStoreFactory factory,
    std::string label,
    SecureDeveloperSecret secret,
    const std::shared_ptr<DeveloperSecretStoreLease>& lease);

void DeliverStoreResult(base::WeakPtr<DeveloperHeaderSecretEditorView> view,
                        uint64_t generation,
                        DeveloperHeaderSecretDirection direction,
                        std::string header_name,
                        bool was_rotation,
                        std::shared_ptr<DeveloperSecretStoreLease> lease,
                        bool stored);

}  // namespace

// Thread-safe ownership token for a newly created Keychain item. If the view
// disappears before the reply, Abort() and Publish() coordinate so the worker
// either removes the item inline or schedules its deletion. The only way to
// drop cleanup ownership is a successful complete-profile commit.
class DeveloperSecretStoreLease {
 public:
  explicit DeveloperSecretStoreLease(DeveloperSecretStoreFactory factory)
      : factory_(std::move(factory)) {}
  ~DeveloperSecretStoreLease() { Abort(); }

  bool Publish(std::string reference) {
    base::AutoLock lock(lock_);
    if (aborted_) {
      return false;
    }
    reference_ = std::move(reference);
    return true;
  }

  std::optional<std::string> reference() const {
    base::AutoLock lock(lock_);
    return reference_;
  }

  void Adopt() {
    base::AutoLock lock(lock_);
    adopted_ = true;
    reference_.reset();
  }

  void Abort() {
    std::optional<std::string> reference;
    {
      base::AutoLock lock(lock_);
      aborted_ = true;
      if (adopted_) {
        return;
      }
      reference = std::exchange(reference_, std::nullopt);
    }
    if (reference) {
      PostRemoveSecret(factory_, std::move(*reference));
    }
  }

 private:
  const DeveloperSecretStoreFactory factory_;
  mutable base::Lock lock_;
  std::optional<std::string> reference_ GUARDED_BY(lock_);
  bool aborted_ GUARDED_BY(lock_) = false;
  bool adopted_ GUARDED_BY(lock_) = false;
};

namespace {

bool StoreSecretOnWorker(
    DeveloperSecretStoreFactory factory,
    std::string label,
    SecureDeveloperSecret secret,
    const std::shared_ptr<DeveloperSecretStoreLease>& lease) {
  if (factory.is_null()) {
    return false;
  }
  std::unique_ptr<DeveloperSecretStore> store = factory.Run();
  if (!store || !lease) {
    return false;
  }
  std::optional<std::string> reference = store->Store(label, secret.value());
  if (!reference) {
    return false;
  }
  if (!lease->Publish(*reference)) {
    store->Remove(*reference);
    return false;
  }
  return true;
}

void DeliverStoreResult(base::WeakPtr<DeveloperHeaderSecretEditorView> view,
                        uint64_t generation,
                        DeveloperHeaderSecretDirection direction,
                        std::string header_name,
                        bool was_rotation,
                        std::shared_ptr<DeveloperSecretStoreLease> lease,
                        bool stored) {
  if (!view) {
    lease->Abort();
    return;
  }
  view->OnStoreFinished(generation, direction, std::move(header_name),
                        was_rotation, std::move(lease), stored);
}

}  // namespace

BEGIN_METADATA(DeveloperHeaderSecretEditorView)
END_METADATA

DeveloperHeaderSecretEditorView::DeveloperHeaderSecretEditorView(
    bool is_off_the_record,
    const std::vector<DeveloperHeaderRule>& request_rules,
    const std::vector<DeveloperHeaderRule>& response_rules,
    DeveloperSecretStoreFactory secret_store_factory,
    StatusCallback status_callback)
    : model_(is_off_the_record, request_rules, response_rules),
      secret_store_factory_(std::move(secret_store_factory)),
      status_callback_(std::move(status_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto privacy = MakeLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS));
  privacy->SetEnabledColor(visual_style::kMutedText);
  privacy->SetMultiLine(true);
  AddChildView(std::move(privacy));

  std::vector<ui::SimpleComboboxModel::Item> directions;
  directions.emplace_back(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_HEADERS));
  directions.emplace_back(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS));
  auto direction = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(directions)));
  direction->SetSelectedIndex(0);
  direction->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_SCOPE));
  direction->SetPreferredSize(
      gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  direction_ = AddChildView(std::move(direction));

  auto header_name = std::make_unique<views::Textfield>();
  header_name->SetPlaceholderText(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_PROFILE_HEADERS_PLACEHOLDER));
  header_name->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_HEADERS));
  header_name->SetBackgroundColor(visual_style::kRaisedSurface);
  header_name->SetTextColorId(visual_style::kText);
  header_name_field_ = AddChildView(std::move(header_name));

  auto secret = std::make_unique<views::Textfield>();
  secret->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  secret->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_PASSWORD_LABEL));
  secret->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_PASSWORD_LABEL));
  secret->SetBackgroundColor(visual_style::kRaisedSurface);
  secret->SetTextColorId(visual_style::kText);
  secret_field_ = AddChildView(std::move(secret));

  auto store = MakeSmallButton(
      base::BindRepeating(&DeveloperHeaderSecretEditorView::StoreDraft,
                          base::Unretained(this)),
      IDS_PASSWORD_MANAGER_SAVE_BUTTON);
  store_button_ = AddChildView(std::move(store));

  auto entries = std::make_unique<views::View>();
  auto* entries_layout =
      entries->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  entries_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  entries_container_ = AddChildView(std::move(entries));

  RebuildEntries();
  UpdateControls();
}

DeveloperHeaderSecretEditorView::~DeveloperHeaderSecretEditorView() {
  ClearTransientValue();
  ++operation_generation_;
  weak_ptr_factory_.InvalidateWeakPtrs();
  AbortUncommittedItems();
}

std::vector<DeveloperHeaderRule>
DeveloperHeaderSecretEditorView::PlainRulesForEditor(
    DeveloperHeaderSecretDirection direction) const {
  return model_.PlainRulesForEditor(direction);
}

bool DeveloperHeaderSecretEditorView::ApplyToProfile(
    DeveloperProfile* profile) const {
  return valid() && !operation_in_flight_ && model_.ApplyToProfile(profile);
}

bool DeveloperHeaderSecretEditorView::BeginProfileCommit() {
  if (!valid() || operation_in_flight_ || profile_commit_in_flight_) {
    return false;
  }
  ClearTransientValue();
  profile_commit_in_flight_ = true;
  return true;
}

void DeveloperHeaderSecretEditorView::CompleteProfileCommit(bool succeeded) {
  if (!profile_commit_in_flight_) {
    return;
  }
  profile_commit_in_flight_ = false;
  if (!succeeded) {
    return;
  }

  std::vector<std::string> removed = model_.TakeReferencesToRemoveAfterSave();
  for (const auto& lease : leases_) {
    const std::optional<std::string> reference = lease->reference();
    if (reference && std::ranges::find(removed, *reference) != removed.end()) {
      lease->Abort();
      std::erase(removed, *reference);
    } else {
      lease->Adopt();
    }
  }
  leases_.clear();
  RemoveReferences(removed);
}

void DeveloperHeaderSecretEditorView::OnPrimaryNavigationStarted() {
  ClearTransientValue();
  navigation_invalidated_ = true;
  ++operation_generation_;
  operation_in_flight_ = false;
  if (!profile_commit_in_flight_) {
    AbortUncommittedItems();
  }
  UpdateControls();
}

void DeveloperHeaderSecretEditorView::StoreDraft() {
  if (!valid() || operation_in_flight_ || profile_commit_in_flight_ ||
      secret_store_factory_.is_null()) {
    status_callback_.Run(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return;
  }

  DeveloperHeaderSecretDirection direction = SelectedDirection();
  std::string header_name = base::UTF16ToUTF8(header_name_field_->GetText());
  const bool was_rotation = editing_target_.has_value();
  if (editing_target_) {
    direction = editing_target_->direction;
    header_name = editing_target_->header_name;
  }

  std::u16string secret16(secret_field_->GetText());
  ClearTransientValue();
  if (!IsValidDeveloperHeaderName(header_name) || secret16.empty()) {
    SecurelyClearString(&secret16);
    status_callback_.Run(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return;
  }
  std::string secret8 = base::UTF16ToUTF8(secret16);
  SecurelyClearString(&secret16);
  SecureDeveloperSecret secret(&secret8);

  operation_in_flight_ = true;
  const uint64_t generation = ++operation_generation_;
  UpdateControls();
  auto lease =
      std::make_shared<DeveloperSecretStoreLease>(secret_store_factory_);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&StoreSecretOnWorker, secret_store_factory_,
                     MakeKeychainLabel(header_name), std::move(secret), lease),
      base::BindOnce(&DeliverStoreResult, weak_ptr_factory_.GetWeakPtr(),
                     generation, direction, std::move(header_name),
                     was_rotation, lease));
}

void DeveloperHeaderSecretEditorView::OnStoreFinished(
    uint64_t generation,
    DeveloperHeaderSecretDirection direction,
    std::string header_name,
    bool was_rotation,
    std::shared_ptr<DeveloperSecretStoreLease> lease,
    bool stored) {
  if (generation != operation_generation_ || navigation_invalidated_ ||
      !stored) {
    lease->Abort();
    if (generation == operation_generation_) {
      operation_in_flight_ = false;
      UpdateControls();
      status_callback_.Run(l10n_util::GetStringUTF16(
                               IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
                           true);
    }
    return;
  }
  operation_in_flight_ = false;
  std::optional<std::string> reference = lease->reference();
  if (!reference || !model_.AddOrRotate(direction, std::move(header_name),
                                        std::move(*reference))) {
    lease->Abort();
    UpdateControls();
    status_callback_.Run(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return;
  }
  leases_.push_back(std::move(lease));
  editing_target_.reset();
  header_name_field_->SetText(std::u16string_view());
  RebuildEntries();
  UpdateControls();
  status_callback_.Run(
      l10n_util::GetStringUTF16(was_rotation
                                    ? IDS_PASSWORD_MANAGER_CONFIRM_UPDATE_TITLE
                                    : IDS_PASSWORD_MANAGER_CONFIRM_SAVED_TITLE),
      false);
}

void DeveloperHeaderSecretEditorView::StartRotation(
    DeveloperHeaderSecretDirection direction,
    std::string header_name) {
  if (!valid() || operation_in_flight_ || profile_commit_in_flight_) {
    return;
  }
  model_.CancelDeleteConfirmation();
  editing_target_ = EditingTarget{direction, std::move(header_name)};
  direction_->SetSelectedIndex(
      direction == DeveloperHeaderSecretDirection::kRequest ? 0 : 1);
  header_name_field_->SetText(base::UTF8ToUTF16(editing_target_->header_name));
  ClearTransientValue();
  ScheduleRebuildEntries();
  UpdateControls();
  secret_field_->RequestFocus();
}

void DeveloperHeaderSecretEditorView::RequestDelete(
    DeveloperHeaderSecretDirection direction,
    std::string header_name) {
  if (!valid() || operation_in_flight_ || profile_commit_in_flight_) {
    return;
  }
  ClearTransientValue();
  editing_target_.reset();
  header_name_field_->SetText(std::u16string_view());
  // The product never resolves or reveals the stored value, so a separate
  // authentication prompt would not guard a disclosure. Security.framework
  // still enforces the app's Keychain access, while this second-click state
  // is the explicit confirmation for the destructive product action.
  model_.RequestDelete(direction, header_name);
  entries_container_->SetEnabled(false);
  ScheduleRebuildEntries();
}

void DeveloperHeaderSecretEditorView::RebuildEntries() {
  entries_container_->RemoveAllChildViews();
  for (const DeveloperHeaderSecretDisplayEntry& entry :
       model_.DisplayEntries()) {
    auto row = std::make_unique<views::View>();
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 6), 6));

    std::u16string label = base::UTF8ToUTF16(entry.header_name);
    label.append(u" · ");
    label.append(l10n_util::GetStringUTF16(
        entry.direction == DeveloperHeaderSecretDirection::kRequest
            ? IDS_AHOI_DEVELOPER_PROFILE_HEADERS
            : IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS));
    auto text = std::make_unique<views::View>();
    auto* text_layout =
        text->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    text->AddChildView(
        MakeLabel(std::move(label), views::style::STYLE_BODY_4_MEDIUM));
    auto masked = MakeLabel(std::u16string(kMaskedValue));
    masked->SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_PASSWORD_LABEL));
    masked->SetEnabledColor(visual_style::kMutedText);
    text->AddChildView(std::move(masked));
    views::View* text_ptr = row->AddChildView(std::move(text));
    layout->SetFlexForView(text_ptr, 1);

    row->AddChildView(MakeSmallButton(
        base::BindRepeating(&DeveloperHeaderSecretEditorView::StartRotation,
                            base::Unretained(this), entry.direction,
                            entry.header_name),
        IDS_PASSWORD_MANAGER_SHORT_UPDATE_BUTTON));
    row->AddChildView(MakeSmallButton(
        base::BindRepeating(&DeveloperHeaderSecretEditorView::RequestDelete,
                            base::Unretained(this), entry.direction,
                            entry.header_name),
        entry.delete_confirmation_armed ? IDS_CONFIRM : IDS_DELETE));
    row->SetBackground(views::CreateRoundedRectBackground(
        visual_style::kRaisedSurface, visual_style::kRowCornerRadius));
    entries_container_->AddChildView(std::move(row));
  }
  entries_container_->InvalidateLayout();
  PreferredSizeChanged();
  UpdateControls();
}

void DeveloperHeaderSecretEditorView::ScheduleRebuildEntries() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DeveloperHeaderSecretEditorView::RebuildEntries,
                     weak_ptr_factory_.GetWeakPtr()));
}

void DeveloperHeaderSecretEditorView::UpdateControls() {
  const bool enabled = valid() && !operation_in_flight_ &&
                       !profile_commit_in_flight_ &&
                       !secret_store_factory_.is_null();
  direction_->SetEnabled(enabled && !editing_target_);
  header_name_field_->SetEnabled(enabled);
  header_name_field_->SetReadOnly(editing_target_.has_value());
  secret_field_->SetEnabled(enabled);
  store_button_->SetEnabled(enabled);
  store_button_->SetText(l10n_util::GetStringUTF16(
      editing_target_ ? IDS_PASSWORD_MANAGER_SHORT_UPDATE_BUTTON
                      : IDS_PASSWORD_MANAGER_SAVE_BUTTON));
  entries_container_->SetEnabled(enabled);
}

void DeveloperHeaderSecretEditorView::ClearTransientValue() {
  if (!secret_field_) {
    return;
  }
  std::u16string secret(secret_field_->GetText());
  if (!secret.empty()) {
    std::u16string zeroes(secret.size(), u'\0');
    secret_field_->SetText(zeroes);
    SecurelyClearString(&zeroes);
  }
  secret_field_->SetText(std::u16string_view());
  SecurelyClearString(&secret);
}

void DeveloperHeaderSecretEditorView::AbortUncommittedItems() {
  for (const auto& lease : leases_) {
    lease->Abort();
  }
  leases_.clear();
  model_.TakeReferencesToRemoveOnCancel();
}

void DeveloperHeaderSecretEditorView::RemoveReferences(
    std::vector<std::string> references) {
  for (std::string& reference : references) {
    PostRemoveSecret(secret_store_factory_, std::move(reference));
  }
}

DeveloperHeaderSecretDirection
DeveloperHeaderSecretEditorView::SelectedDirection() const {
  return direction_->GetSelectedIndex().value_or(0) == 0
             ? DeveloperHeaderSecretDirection::kRequest
             : DeveloperHeaderSecretDirection::kResponse;
}

}  // namespace ahoi
