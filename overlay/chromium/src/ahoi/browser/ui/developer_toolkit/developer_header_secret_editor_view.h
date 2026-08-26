// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_VIEW_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Combobox;
class MdTextButton;
class Textfield;
}  // namespace views

namespace ahoi {

class DeveloperSecretStoreLease;

// Native, write-only Keychain editor for secret header values. Existing
// values and opaque references are never rendered or exposed through its
// accessibility tree. New plaintext is masked and moved immediately to a
// bounded worker-sequence buffer.
class DeveloperHeaderSecretEditorView final : public views::View {
  METADATA_HEADER(DeveloperHeaderSecretEditorView, views::View)

 public:
  using StatusCallback =
      base::RepeatingCallback<void(std::u16string text, bool is_error)>;

  DeveloperHeaderSecretEditorView(
      bool is_off_the_record,
      const std::vector<DeveloperHeaderRule>& request_rules,
      const std::vector<DeveloperHeaderRule>& response_rules,
      DeveloperSecretStoreFactory secret_store_factory,
      StatusCallback status_callback);
  DeveloperHeaderSecretEditorView(const DeveloperHeaderSecretEditorView&) =
      delete;
  DeveloperHeaderSecretEditorView& operator=(
      const DeveloperHeaderSecretEditorView&) = delete;
  ~DeveloperHeaderSecretEditorView() override;

  std::vector<DeveloperHeaderRule> PlainRulesForEditor(
      DeveloperHeaderSecretDirection direction) const;
  bool ApplyToProfile(DeveloperProfile* profile) const;
  bool operation_in_flight() const { return operation_in_flight_; }
  bool valid() const { return model_.valid() && !navigation_invalidated_; }

  // Brackets the complete profile-store transaction. A successful commit
  // adopts active new items and only then removes superseded items.
  bool BeginProfileCommit();
  void CompleteProfileCommit(bool succeeded);

  // Called on any primary-frame navigation. Clears masked plaintext
  // immediately, invalidates in-flight results and cleans up uncommitted
  // Keychain items unless the navigation was caused by a successful commit.
  void OnPrimaryNavigationStarted();

  // Public only as the target of a free reply trampoline that must run even
  // after this view's WeakPtr becomes null, so an orphaned Keychain item can
  // still be removed. Product callers do not invoke it directly.
  void OnStoreFinished(uint64_t generation,
                       DeveloperHeaderSecretDirection direction,
                       std::string header_name,
                       bool was_rotation,
                       std::shared_ptr<DeveloperSecretStoreLease> lease,
                       bool stored);

  views::Textfield* secret_field_for_testing() const { return secret_field_; }
  views::Textfield* header_name_field_for_testing() const {
    return header_name_field_;
  }
  views::MdTextButton* store_button_for_testing() const {
    return store_button_;
  }
  views::View* entries_container_for_testing() const {
    return entries_container_;
  }
  const DeveloperHeaderSecretEditorModel& model_for_testing() const {
    return model_;
  }

 private:
  struct EditingTarget {
    DeveloperHeaderSecretDirection direction =
        DeveloperHeaderSecretDirection::kRequest;
    std::string header_name;
  };

  void StoreDraft();
  void StartRotation(DeveloperHeaderSecretDirection direction,
                     std::string header_name);
  void RequestDelete(DeveloperHeaderSecretDirection direction,
                     std::string header_name);
  void RebuildEntries();
  void ScheduleRebuildEntries();
  void UpdateControls();
  void ClearTransientValue();
  void AbortUncommittedItems();
  void RemoveReferences(std::vector<std::string> references);
  DeveloperHeaderSecretDirection SelectedDirection() const;

  DeveloperHeaderSecretEditorModel model_;
  const DeveloperSecretStoreFactory secret_store_factory_;
  const StatusCallback status_callback_;
  raw_ptr<views::Combobox> direction_ = nullptr;
  raw_ptr<views::Textfield> header_name_field_ = nullptr;
  raw_ptr<views::Textfield> secret_field_ = nullptr;
  raw_ptr<views::MdTextButton> store_button_ = nullptr;
  raw_ptr<views::View> entries_container_ = nullptr;
  std::optional<EditingTarget> editing_target_;
  std::vector<std::shared_ptr<DeveloperSecretStoreLease>> leases_;
  uint64_t operation_generation_ = 0;
  bool operation_in_flight_ = false;
  bool profile_commit_in_flight_ = false;
  bool navigation_invalidated_ = false;
  base::WeakPtrFactory<DeveloperHeaderSecretEditorView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_VIEW_H_
