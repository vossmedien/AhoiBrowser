// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_model.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/strings/string_util.h"

namespace ahoi {
namespace {

bool ContainsReference(const std::vector<std::string>& references,
                       std::string_view reference) {
  return std::ranges::find(references, reference) != references.end();
}

bool ContainsHeaderName(const std::vector<DeveloperHeaderRule>& rules,
                        std::string_view header_name) {
  return std::ranges::any_of(rules, [header_name](const auto& rule) {
    return base::EqualsCaseInsensitiveASCII(rule.name, header_name);
  });
}

bool IsValidPlainRule(const DeveloperHeaderRule& rule) {
  if (!rule.secret_reference.empty() ||
      !IsValidDeveloperHeaderName(rule.name)) {
    return false;
  }
  if (rule.action == DeveloperHeaderAction::kRemove) {
    return rule.value.empty();
  }
  return rule.action == DeveloperHeaderAction::kSet && !rule.value.empty() &&
         IsValidDeveloperHeaderValue(rule.value);
}

void AppendUnique(std::vector<std::string>* destination,
                  const std::string& value) {
  if (!ContainsReference(*destination, value)) {
    destination->push_back(value);
  }
}

}  // namespace

DeveloperHeaderSecretEditorModel::DeveloperHeaderSecretEditorModel(
    bool is_off_the_record,
    const std::vector<DeveloperHeaderRule>& request_rules,
    const std::vector<DeveloperHeaderRule>& response_rules)
    : is_off_the_record_(is_off_the_record), valid_(!is_off_the_record) {
  if (!valid_) {
    return;
  }
  ImportRules(DeveloperHeaderSecretDirection::kRequest, request_rules);
  if (valid_) {
    ImportRules(DeveloperHeaderSecretDirection::kResponse, response_rules);
  }
}

DeveloperHeaderSecretEditorModel::~DeveloperHeaderSecretEditorModel() = default;

std::vector<DeveloperHeaderRule>
DeveloperHeaderSecretEditorModel::PlainRulesForEditor(
    DeveloperHeaderSecretDirection direction) const {
  if (!valid_) {
    return {};
  }
  return direction == DeveloperHeaderSecretDirection::kRequest
             ? plain_request_rules_
             : plain_response_rules_;
}

std::vector<DeveloperHeaderSecretDisplayEntry>
DeveloperHeaderSecretEditorModel::DisplayEntries() const {
  std::vector<DeveloperHeaderSecretDisplayEntry> display;
  if (!valid_) {
    return display;
  }
  display.reserve(secret_entries_.size());
  for (const SecretEntry& entry : secret_entries_) {
    const EntryKey key{entry.direction, NormalizeHeaderName(entry.header_name)};
    display.push_back({
        .direction = entry.direction,
        .header_name = entry.header_name,
        .delete_confirmation_armed = delete_confirmation_ == key,
    });
  }
  return display;
}

bool DeveloperHeaderSecretEditorModel::AddOrRotate(
    DeveloperHeaderSecretDirection direction,
    std::string header_name,
    std::string reference) {
  delete_confirmation_.reset();
  if (!valid_ || !IsValidDeveloperHeaderName(header_name) ||
      !IsValidDeveloperSecretReference(reference) ||
      ContainsReference(original_references_, reference) ||
      ContainsReference(created_references_, reference)) {
    return false;
  }

  auto existing = std::ranges::find_if(
      secret_entries_, [direction, &header_name](const SecretEntry& entry) {
        return Matches(entry, direction, header_name);
      });
  if (existing == secret_entries_.end() &&
      secret_entries_.size() >= kMaxDeveloperHeaderRules) {
    return false;
  }
  const std::string created_reference = reference;
  if (existing == secret_entries_.end()) {
    secret_entries_.push_back(
        {direction, std::move(header_name), std::move(reference)});
  } else {
    existing->header_name = std::move(header_name);
    existing->reference = std::move(reference);
  }
  AppendUnique(&created_references_, created_reference);
  return true;
}

DeveloperHeaderSecretDeleteResult
DeveloperHeaderSecretEditorModel::RequestDelete(
    DeveloperHeaderSecretDirection direction,
    std::string_view header_name) {
  if (!valid_) {
    return DeveloperHeaderSecretDeleteResult::kRejected;
  }
  const auto found = std::ranges::find_if(
      secret_entries_, [direction, header_name](const SecretEntry& entry) {
        return Matches(entry, direction, header_name);
      });
  if (found == secret_entries_.end()) {
    delete_confirmation_.reset();
    return DeveloperHeaderSecretDeleteResult::kRejected;
  }

  EntryKey key{direction, NormalizeHeaderName(header_name)};
  if (delete_confirmation_ != key) {
    delete_confirmation_ = std::move(key);
    return DeveloperHeaderSecretDeleteResult::kConfirmationArmed;
  }
  secret_entries_.erase(found);
  delete_confirmation_.reset();
  return DeveloperHeaderSecretDeleteResult::kDeleted;
}

void DeveloperHeaderSecretEditorModel::CancelDeleteConfirmation() {
  delete_confirmation_.reset();
}

bool DeveloperHeaderSecretEditorModel::ApplyToProfile(
    DeveloperProfile* profile) const {
  if (!profile || !valid_) {
    return false;
  }
  DeveloperProfile candidate = *profile;
  for (const DeveloperHeaderRule& rule : candidate.header_rules) {
    if (!IsValidPlainRule(rule)) {
      return false;
    }
  }
  for (const DeveloperHeaderRule& rule : candidate.response_header_rules) {
    if (!IsValidPlainRule(rule)) {
      return false;
    }
  }

  for (const SecretEntry& entry : secret_entries_) {
    std::vector<DeveloperHeaderRule>* rules =
        entry.direction == DeveloperHeaderSecretDirection::kRequest
            ? &candidate.header_rules
            : &candidate.response_header_rules;
    if (ContainsHeaderName(*rules, entry.header_name)) {
      return false;
    }
    rules->push_back({
        .name = entry.header_name,
        .secret_reference = entry.reference,
        .action = DeveloperHeaderAction::kSet,
    });
  }

  const size_t rule_count =
      candidate.header_rules.size() + candidate.response_header_rules.size();
  size_t byte_count = 0;
  for (const DeveloperHeaderRule& rule : candidate.header_rules) {
    byte_count +=
        rule.name.size() + rule.value.size() + rule.secret_reference.size();
  }
  for (const DeveloperHeaderRule& rule : candidate.response_header_rules) {
    byte_count +=
        rule.name.size() + rule.value.size() + rule.secret_reference.size();
  }
  if (rule_count > kMaxDeveloperHeaderRules ||
      byte_count > kMaxDeveloperHeaderBytes) {
    return false;
  }
  *profile = std::move(candidate);
  return true;
}

std::vector<std::string>
DeveloperHeaderSecretEditorModel::TakeReferencesToRemoveOnCancel() {
  delete_confirmation_.reset();
  return std::exchange(created_references_, {});
}

std::vector<std::string>
DeveloperHeaderSecretEditorModel::TakeReferencesToRemoveAfterSave() {
  std::vector<std::string> active;
  active.reserve(secret_entries_.size());
  for (const SecretEntry& entry : secret_entries_) {
    AppendUnique(&active, entry.reference);
  }

  std::vector<std::string> removed;
  for (const std::string& reference : original_references_) {
    if (!ContainsReference(active, reference)) {
      AppendUnique(&removed, reference);
    }
  }
  for (const std::string& reference : created_references_) {
    if (!ContainsReference(active, reference)) {
      AppendUnique(&removed, reference);
    }
  }
  original_references_ = std::move(active);
  created_references_.clear();
  delete_confirmation_.reset();
  return removed;
}

// static
std::string DeveloperHeaderSecretEditorModel::NormalizeHeaderName(
    std::string_view header_name) {
  return base::ToLowerASCII(header_name);
}

// static
bool DeveloperHeaderSecretEditorModel::Matches(
    const SecretEntry& entry,
    DeveloperHeaderSecretDirection direction,
    std::string_view header_name) {
  return entry.direction == direction &&
         base::EqualsCaseInsensitiveASCII(entry.header_name, header_name);
}

void DeveloperHeaderSecretEditorModel::ImportRules(
    DeveloperHeaderSecretDirection direction,
    const std::vector<DeveloperHeaderRule>& rules) {
  std::vector<DeveloperHeaderRule>* plain_rules =
      direction == DeveloperHeaderSecretDirection::kRequest
          ? &plain_request_rules_
          : &plain_response_rules_;
  for (const DeveloperHeaderRule& rule : rules) {
    if (rule.secret_reference.empty()) {
      plain_rules->push_back(rule);
      continue;
    }
    if (rule.action != DeveloperHeaderAction::kSet || !rule.value.empty() ||
        !IsValidDeveloperHeaderName(rule.name) ||
        !IsValidDeveloperSecretReference(rule.secret_reference) ||
        std::ranges::any_of(secret_entries_,
                            [direction, &rule](const SecretEntry& entry) {
                              return Matches(entry, direction, rule.name);
                            }) ||
        ContainsReference(original_references_, rule.secret_reference)) {
      valid_ = false;
      plain_request_rules_.clear();
      plain_response_rules_.clear();
      secret_entries_.clear();
      original_references_.clear();
      return;
    }
    secret_entries_.push_back({direction, rule.name, rule.secret_reference});
    original_references_.push_back(rule.secret_reference);
  }
}

}  // namespace ahoi
