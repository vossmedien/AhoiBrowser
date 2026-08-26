// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_MODEL_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_MODEL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"

namespace ahoi {

enum class DeveloperHeaderSecretDirection {
  kRequest,
  kResponse,
};

// Deliberately contains no opaque reference. This is the only entry shape a
// native view may use for labels, accessibility data or test evidence.
struct DeveloperHeaderSecretDisplayEntry {
  DeveloperHeaderSecretDirection direction =
      DeveloperHeaderSecretDirection::kRequest;
  std::string header_name;
  bool delete_confirmation_armed = false;

  bool operator==(const DeveloperHeaderSecretDisplayEntry&) const = default;
};

enum class DeveloperHeaderSecretDeleteResult {
  kRejected,
  kConfirmationArmed,
  kDeleted,
};

// Transaction model for the Keychain-backed part of the native profile
// editor. Plaintext is never accepted or retained here. New references are
// committed only after the complete profile has been persisted; until then
// they are cleanup-owned by the editor.
class DeveloperHeaderSecretEditorModel {
 public:
  DeveloperHeaderSecretEditorModel(
      bool is_off_the_record,
      const std::vector<DeveloperHeaderRule>& request_rules,
      const std::vector<DeveloperHeaderRule>& response_rules);
  DeveloperHeaderSecretEditorModel(const DeveloperHeaderSecretEditorModel&) =
      delete;
  DeveloperHeaderSecretEditorModel& operator=(
      const DeveloperHeaderSecretEditorModel&) = delete;
  ~DeveloperHeaderSecretEditorModel();

  bool valid() const { return valid_; }
  bool is_off_the_record() const { return is_off_the_record_; }

  std::vector<DeveloperHeaderRule> PlainRulesForEditor(
      DeveloperHeaderSecretDirection direction) const;
  std::vector<DeveloperHeaderSecretDisplayEntry> DisplayEntries() const;

  // `reference` must be a newly created, independently cleanup-owned
  // Keychain item. A matching direction/name rotates the current entry.
  bool AddOrRotate(DeveloperHeaderSecretDirection direction,
                   std::string header_name,
                   std::string reference);
  DeveloperHeaderSecretDeleteResult RequestDelete(
      DeveloperHeaderSecretDirection direction,
      std::string_view header_name);
  void CancelDeleteConfirmation();

  // The input profile may contain only plain rules from the text editors.
  // Secret rules are appended atomically after duplicate/size validation.
  bool ApplyToProfile(DeveloperProfile* profile) const;

  // Closing/canceling owns deletion of new, not-yet-persisted items only.
  std::vector<std::string> TakeReferencesToRemoveOnCancel();
  // Successful profile persistence owns deletion of replaced, deleted and
  // superseded items, then adopts active new references as the new baseline.
  std::vector<std::string> TakeReferencesToRemoveAfterSave();

 private:
  struct SecretEntry {
    DeveloperHeaderSecretDirection direction =
        DeveloperHeaderSecretDirection::kRequest;
    std::string header_name;
    std::string reference;
  };

  struct EntryKey {
    DeveloperHeaderSecretDirection direction =
        DeveloperHeaderSecretDirection::kRequest;
    std::string normalized_header_name;

    bool operator==(const EntryKey&) const = default;
  };

  static std::string NormalizeHeaderName(std::string_view header_name);
  static bool Matches(const SecretEntry& entry,
                      DeveloperHeaderSecretDirection direction,
                      std::string_view header_name);
  void ImportRules(DeveloperHeaderSecretDirection direction,
                   const std::vector<DeveloperHeaderRule>& rules);

  const bool is_off_the_record_;
  bool valid_ = true;
  std::vector<DeveloperHeaderRule> plain_request_rules_;
  std::vector<DeveloperHeaderRule> plain_response_rules_;
  std::vector<SecretEntry> secret_entries_;
  std::vector<std::string> original_references_;
  std::vector<std::string> created_references_;
  std::optional<EntryKey> delete_confirmation_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_HEADER_SECRET_EDITOR_MODEL_H_
