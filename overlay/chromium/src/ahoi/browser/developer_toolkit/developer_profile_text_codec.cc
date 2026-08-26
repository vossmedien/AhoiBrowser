// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_text_codec.h"

#include <string>
#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace ahoi {
namespace {

constexpr std::string_view kKeychainPrefix = "@keychain(";

DeveloperHeaderTextParseResult Error(DeveloperHeaderTextError error,
                                     size_t line) {
  return {.error = error, .error_line = line};
}

std::string Trim(std::string_view value) {
  std::string result(value);
  base::TrimWhitespaceASCII(result, base::TRIM_ALL, &result);
  return result;
}

bool HasHeaderName(const std::vector<DeveloperHeaderRule>& rules,
                   std::string_view name) {
  for (const DeveloperHeaderRule& rule : rules) {
    if (base::EqualsCaseInsensitiveASCII(rule.name, name)) {
      return true;
    }
  }
  return false;
}

}  // namespace

DeveloperHeaderTextParseResult ParseDeveloperHeaderRules(
    std::string_view text) {
  if (text.size() > kMaxDeveloperHeaderBytes) {
    return Error(DeveloperHeaderTextError::kTooLarge, 0);
  }

  DeveloperHeaderTextParseResult result;
  const std::vector<std::string_view> lines = base::SplitStringPiece(
      text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  for (size_t index = 0; index < lines.size(); ++index) {
    std::string line = Trim(lines[index]);
    const size_t line_number = index + 1;
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (result.rules.size() >= kMaxDeveloperHeaderRules) {
      return Error(DeveloperHeaderTextError::kTooManyRules, line_number);
    }

    DeveloperHeaderRule rule;
    if (line.front() == '-') {
      rule.action = DeveloperHeaderAction::kRemove;
      rule.name = Trim(std::string_view(line).substr(1));
    } else {
      const size_t separator = line.find(':');
      if (separator == std::string::npos) {
        return Error(DeveloperHeaderTextError::kMissingSeparator, line_number);
      }
      rule.name = Trim(std::string_view(line).substr(0, separator));
      rule.value = Trim(std::string_view(line).substr(separator + 1));
      if (base::StartsWith(rule.value, kKeychainPrefix,
                           base::CompareCase::SENSITIVE) &&
          rule.value.back() == ')') {
        rule.secret_reference =
            rule.value.substr(kKeychainPrefix.size(),
                              rule.value.size() - kKeychainPrefix.size() - 1);
        rule.value.clear();
      }
    }

    if (!IsValidDeveloperHeaderName(rule.name)) {
      return Error(DeveloperHeaderTextError::kInvalidName, line_number);
    }
    if (rule.action == DeveloperHeaderAction::kSet &&
        ((rule.value.empty() && rule.secret_reference.empty()) ||
         (!rule.value.empty() && !IsValidDeveloperHeaderValue(rule.value)) ||
         (!rule.secret_reference.empty() &&
          !IsValidDeveloperSecretReference(rule.secret_reference)))) {
      return Error(DeveloperHeaderTextError::kInvalidValue, line_number);
    }
    if (HasHeaderName(result.rules, rule.name)) {
      return Error(DeveloperHeaderTextError::kDuplicateName, line_number);
    }
    result.rules.push_back(std::move(rule));
  }
  return result;
}

std::string FormatDeveloperHeaderRules(
    const std::vector<DeveloperHeaderRule>& rules) {
  std::string result;
  for (const DeveloperHeaderRule& rule : rules) {
    if (!result.empty()) {
      result.push_back('\n');
    }
    if (rule.action == DeveloperHeaderAction::kRemove) {
      result.push_back('-');
      result.append(rule.name);
      continue;
    }
    result.append(rule.name);
    result.append(": ");
    if (!rule.secret_reference.empty()) {
      result.append(kKeychainPrefix);
      result.append(rule.secret_reference);
      result.push_back(')');
    } else {
      result.append(rule.value);
    }
  }
  return result;
}

}  // namespace ahoi
