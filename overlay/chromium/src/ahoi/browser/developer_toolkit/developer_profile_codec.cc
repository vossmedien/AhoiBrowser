// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_codec.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_asset_codec.h"

namespace ahoi {
namespace {

constexpr char kName[] = "name";
constexpr char kAssets[] = "assets";
constexpr char kCss[] = "css";
constexpr char kJavaScript[] = "javascript";
constexpr char kUserAgent[] = "user_agent";
constexpr char kHeaders[] = "headers";
constexpr char kResponseHeaders[] = "response_headers";
constexpr char kCacheDisabled[] = "cache_disabled";
constexpr char kEnabled[] = "enabled";
constexpr char kSyncEnabled[] = "sync_enabled";
constexpr char kSource[] = "source";
constexpr char kValue[] = "value";
constexpr char kSecretReference[] = "secret_ref";
constexpr char kRules[] = "rules";
constexpr char kHeaderName[] = "name";
constexpr char kAction[] = "action";
constexpr char kSet[] = "set";
constexpr char kRemove[] = "remove";

std::optional<bool> ReadRequiredBool(const base::DictValue& dict,
                                     std::string_view key) {
  return dict.FindBool(key);
}

std::optional<std::string> ReadRequiredString(const base::DictValue& dict,
                                              std::string_view key) {
  const std::string* value = dict.FindString(key);
  if (!value) {
    return std::nullopt;
  }
  return *value;
}

std::optional<base::DictValue> ReadRequiredDict(const base::DictValue& dict,
                                                std::string_view key) {
  const base::DictValue* value = dict.FindDict(key);
  if (!value) {
    return std::nullopt;
  }
  return value->Clone();
}

base::DictValue SerializeHeaderRules(
    bool enabled,
    bool sync_enabled,
    const std::vector<DeveloperHeaderRule>& header_rules) {
  base::ListValue rules;
  for (const DeveloperHeaderRule& rule : header_rules) {
    base::DictValue encoded;
    encoded.Set(kHeaderName, rule.name);
    encoded.Set(kAction,
                rule.action == DeveloperHeaderAction::kSet ? kSet : kRemove);
    if (!rule.secret_reference.empty()) {
      encoded.Set(kSecretReference, rule.secret_reference);
    } else {
      encoded.Set(kValue, rule.value);
    }
    rules.Append(std::move(encoded));
  }
  return base::DictValue()
      .Set(kEnabled, enabled)
      .Set(kSyncEnabled, sync_enabled)
      .Set(kRules, std::move(rules));
}

bool DeserializeHeaderRules(const base::DictValue& encoded,
                            bool* enabled,
                            bool* sync_enabled,
                            std::vector<DeveloperHeaderRule>* parsed_rules) {
  const std::optional<bool> encoded_enabled =
      ReadRequiredBool(encoded, kEnabled);
  const base::ListValue* rules = encoded.FindList(kRules);
  if (!encoded_enabled || !rules || rules->size() > kMaxDeveloperHeaderRules) {
    return false;
  }

  std::vector<DeveloperHeaderRule> result;
  result.reserve(rules->size());
  for (const base::Value& entry : *rules) {
    const base::DictValue* rule = entry.GetIfDict();
    if (!rule) {
      return false;
    }
    auto header_name = ReadRequiredString(*rule, kHeaderName);
    auto action = ReadRequiredString(*rule, kAction);
    const std::string* header_value = rule->FindString(kValue);
    const std::string* secret_reference = rule->FindString(kSecretReference);
    if (!header_name || !action || (header_value && secret_reference)) {
      return false;
    }
    DeveloperHeaderRule parsed;
    parsed.name = std::move(*header_name);
    if (*action == kSet) {
      parsed.action = DeveloperHeaderAction::kSet;
      if (secret_reference) {
        parsed.secret_reference = *secret_reference;
      } else if (header_value) {
        parsed.value = *header_value;
      } else {
        return false;
      }
    } else if (*action == kRemove) {
      parsed.action = DeveloperHeaderAction::kRemove;
      if (secret_reference || (header_value && !header_value->empty())) {
        return false;
      }
    } else {
      return false;
    }
    result.push_back(std::move(parsed));
  }
  *enabled = *encoded_enabled;
  *sync_enabled = encoded.FindBool(kSyncEnabled).value_or(false);
  *parsed_rules = std::move(result);
  return true;
}

}  // namespace

std::optional<base::DictValue> SerializeDeveloperProfile(
    const DeveloperProfile& profile) {
  base::DictValue result;
  result.Set(kName, profile.name);
  result.Set(kAssets, SerializeDeveloperAssets(profile.assets));
  result.Set(kUserAgent, base::DictValue()
                             .Set(kEnabled, profile.user_agent_enabled)
                             .Set(kValue, profile.user_agent));

  result.Set(kHeaders, SerializeHeaderRules(profile.header_rules_enabled,
                                            profile.header_rules_sync_enabled,
                                            profile.header_rules));
  result.Set(kResponseHeaders,
             SerializeHeaderRules(profile.response_header_rules_enabled,
                                  profile.response_header_rules_sync_enabled,
                                  profile.response_header_rules));
  result.Set(kCacheDisabled, profile.cache_disabled);
  return result;
}

std::optional<DeveloperProfile> DeserializeDeveloperProfile(
    const base::DictValue& value,
    const url::Origin* owner_origin) {
  DeveloperProfile profile;
  auto name = ReadRequiredString(value, kName);
  auto user_agent = ReadRequiredDict(value, kUserAgent);
  auto headers = ReadRequiredDict(value, kHeaders);
  if (!name || !user_agent || !headers) {
    return std::nullopt;
  }
  if (name->size() > kMaxDeveloperProfileNameBytes) {
    return std::nullopt;
  }
  profile.name = std::move(*name);

  auto user_agent_enabled = ReadRequiredBool(*user_agent, kEnabled);
  auto user_agent_value = ReadRequiredString(*user_agent, kValue);
  if (!user_agent_enabled || !user_agent_value || !headers) {
    return std::nullopt;
  }
  if (user_agent_value->size() > kMaxDeveloperUserAgentBytes) {
    return std::nullopt;
  }
  if (const base::ListValue* assets = value.FindList(kAssets)) {
    auto decoded_assets = DeserializeDeveloperAssets(*assets);
    if (!decoded_assets) {
      return std::nullopt;
    }
    profile.assets = std::move(*decoded_assets);
  } else {
    const base::DictValue* css = value.FindDict(kCss);
    const base::DictValue* javascript = value.FindDict(kJavaScript);
    const std::optional<bool> css_enabled =
        css ? css->FindBool(kEnabled) : std::nullopt;
    const std::string* css_source = css ? css->FindString(kSource) : nullptr;
    const std::optional<bool> javascript_enabled =
        javascript ? javascript->FindBool(kEnabled) : std::nullopt;
    const std::string* javascript_source =
        javascript ? javascript->FindString(kSource) : nullptr;
    if (!owner_origin || !css_enabled || !css_source || !javascript_enabled ||
        !javascript_source || css_source->size() > kMaxDeveloperCssBytes ||
        javascript_source->size() > kMaxDeveloperJavaScriptBytes) {
      return std::nullopt;
    }
    profile.assets =
        MigrateLegacyDeveloperAssets(*owner_origin, *css_enabled, *css_source,
                                     *javascript_enabled, *javascript_source);
  }
  profile.user_agent_enabled = *user_agent_enabled;
  profile.user_agent = std::move(*user_agent_value);
  if (!DeserializeHeaderRules(*headers, &profile.header_rules_enabled,
                              &profile.header_rules_sync_enabled,
                              &profile.header_rules)) {
    return std::nullopt;
  }

  // `response_headers` was added to the v1 dictionary shape after request
  // rules shipped. Absence therefore means disabled, preserving old profiles
  // without a schema reset or data loss.
  if (const base::Value* response_headers_value =
          value.Find(kResponseHeaders)) {
    const base::DictValue* response_headers =
        response_headers_value->GetIfDict();
    if (!response_headers ||
        !DeserializeHeaderRules(*response_headers,
                                &profile.response_header_rules_enabled,
                                &profile.response_header_rules_sync_enabled,
                                &profile.response_header_rules)) {
      return std::nullopt;
    }
  }
  profile.cache_disabled = value.FindBool(kCacheDisabled).value_or(false);
  return profile;
}

std::optional<base::DictValue> SerializeDeveloperProfileForSync(
    const DeveloperProfile& profile) {
  DeveloperProfile sync_profile = profile;
  std::erase_if(sync_profile.assets, [](const DeveloperAsset& asset) {
    return !asset.sync_enabled;
  });
  sync_profile.user_agent_enabled = false;
  sync_profile.user_agent.clear();
  sync_profile.cache_disabled = false;

  const auto remove_local_secret = [](const DeveloperHeaderRule& rule) {
    return !rule.secret_reference.empty();
  };
  if (!sync_profile.header_rules_sync_enabled) {
    sync_profile.header_rules_enabled = false;
    sync_profile.header_rules.clear();
  } else {
    std::erase_if(sync_profile.header_rules, remove_local_secret);
  }
  if (!sync_profile.response_header_rules_sync_enabled) {
    sync_profile.response_header_rules_enabled = false;
    sync_profile.response_header_rules.clear();
  } else {
    std::erase_if(sync_profile.response_header_rules, remove_local_secret);
  }
  return SerializeDeveloperProfile(sync_profile);
}

}  // namespace ahoi
