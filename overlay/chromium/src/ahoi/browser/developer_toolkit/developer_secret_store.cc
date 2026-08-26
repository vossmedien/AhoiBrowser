// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_secret_store.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"

namespace ahoi {
namespace {

bool RulesHaveSecretReferences(const std::vector<DeveloperHeaderRule>& rules) {
  for (const DeveloperHeaderRule& rule : rules) {
    if (!rule.secret_reference.empty()) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::optional<std::vector<DeveloperHeaderRule>> MaterializeDeveloperHeaderRules(
    const std::vector<DeveloperHeaderRule>& rules,
    const DeveloperSecretStore& secret_store) {
  std::vector<DeveloperHeaderRule> materialized;
  materialized.reserve(rules.size());
  for (const DeveloperHeaderRule& rule : rules) {
    DeveloperHeaderRule resolved = rule;
    if (!resolved.secret_reference.empty()) {
      if (!IsValidDeveloperSecretReference(resolved.secret_reference)) {
        return std::nullopt;
      }
      std::optional<std::string> value =
          secret_store.Resolve(resolved.secret_reference);
      if (!value || value->empty() || !IsValidDeveloperHeaderValue(*value)) {
        return std::nullopt;
      }
      resolved.value = std::move(*value);
      resolved.secret_reference.clear();
    }
    materialized.push_back(std::move(resolved));
  }
  return materialized;
}

bool DeveloperProfileHasActiveHeaderSecretReferences(
    const DeveloperProfile& profile) {
  return (profile.header_rules_enabled &&
          RulesHaveSecretReferences(profile.header_rules)) ||
         (profile.response_header_rules_enabled &&
          RulesHaveSecretReferences(profile.response_header_rules));
}

std::optional<DeveloperProfile> MaterializeDeveloperProfileHeaderSecrets(
    DeveloperProfile profile,
    const DeveloperSecretStore& secret_store) {
  std::optional<std::vector<DeveloperHeaderRule>> request_rules;
  std::optional<std::vector<DeveloperHeaderRule>> response_rules;
  if (profile.header_rules_enabled) {
    request_rules =
        MaterializeDeveloperHeaderRules(profile.header_rules, secret_store);
    if (!request_rules) {
      return std::nullopt;
    }
  }
  if (profile.response_header_rules_enabled) {
    response_rules = MaterializeDeveloperHeaderRules(
        profile.response_header_rules, secret_store);
    if (!response_rules) {
      return std::nullopt;
    }
  }
  if (request_rules) {
    profile.header_rules = std::move(*request_rules);
  }
  if (response_rules) {
    profile.response_header_rules = std::move(*response_rules);
  }
  return profile;
}

}  // namespace ahoi
