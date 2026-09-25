// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing.h"

#include <set>
#include <utility>

#include "base/strings/strcat.h"
#include "components/prefs/pref_service.h"

namespace ahoi::navigation {

namespace {

bool HasChar(std::string_view text, char c) {
  return text.find(c) != std::string_view::npos;
}

constexpr char kVersionKey[] = "version";
constexpr char kEnabledKey[] = "enabled";
constexpr char kRulesKey[] = "rules";
constexpr char kDefaultKey[] = "default";
constexpr char kIdKey[] = "id";
constexpr char kHostKey[] = "host";
constexpr char kIncludeSubdomainsKey[] = "include_subdomains";
constexpr char kPathPrefixKey[] = "path_prefix";
constexpr char kTargetKey[] = "target_workspace_id";
constexpr char kModeKey[] = "mode";
constexpr char kDefaultTargetKey[] = "target";
constexpr char kLastActiveTarget[] = "last_active";
constexpr char kNormalTabMode[] = "normal_tab";
constexpr char kQuickWindowMode[] = "quick_window";

std::string_view StripTrailingDot(std::string_view host) {
  if (!host.empty() && host.back() == '.') {
    host.remove_suffix(1);
  }
  return host;
}

// Hosts that never take subdomains into account.
bool IsExactOnlyHost(std::string_view normalized_host) {
  if (normalized_host == "localhost") {
    return true;
  }
  const GURL probe(base::StrCat({"http://", normalized_host, "/"}));
  return probe.is_valid() && probe.HostIsIPAddress();
}

std::optional<LinkOpenMode> ParseMode(const std::string* mode) {
  if (!mode) {
    return std::nullopt;
  }
  if (*mode == kNormalTabMode) {
    return LinkOpenMode::kNormalTab;
  }
  if (*mode == kQuickWindowMode) {
    return LinkOpenMode::kQuickWindow;
  }
  return std::nullopt;
}

const char* ModeName(LinkOpenMode mode) {
  return mode == LinkOpenMode::kQuickWindow ? kQuickWindowMode : kNormalTabMode;
}

std::optional<RoutingRule> DecodeRule(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString(kIdKey);
  const std::optional<bool> enabled = dict->FindBool(kEnabledKey);
  const std::string* host = dict->FindString(kHostKey);
  const std::optional<bool> include_subdomains =
      dict->FindBool(kIncludeSubdomainsKey);
  const std::string* target = dict->FindString(kTargetKey);
  const std::optional<LinkOpenMode> mode =
      ParseMode(dict->FindString(kModeKey));
  if (!id || !enabled || !host || !include_subdomains || !target || !mode) {
    return std::nullopt;
  }
  // Stored values must already be canonical; nothing is repaired.
  const std::optional<std::string> normalized_host = NormalizeRuleHost(*host);
  if (!normalized_host || *normalized_host != *host) {
    return std::nullopt;
  }
  std::string path_prefix;
  if (const base::Value* path = dict->Find(kPathPrefixKey)) {
    const std::string* path_string = path->GetIfString();
    const std::optional<std::string> normalized_path =
        path_string ? NormalizeRulePathPrefix(*path_string) : std::nullopt;
    if (!normalized_path || *normalized_path != *path_string) {
      return std::nullopt;
    }
    path_prefix = *normalized_path;
  }
  RoutingRule rule{
      .id = base::Uuid::ParseLowercase(*id),
      .enabled = *enabled,
      .host = *normalized_host,
      .include_subdomains = *include_subdomains,
      .path_prefix = std::move(path_prefix),
      .target_workspace_id = base::Uuid::ParseLowercase(*target),
      .mode = *mode,
  };
  if (!rule.id.is_valid() || !rule.target_workspace_id.is_valid()) {
    return std::nullopt;
  }
  return rule;
}

std::optional<DefaultRoute> DecodeDefault(const base::DictValue& dict) {
  const std::string* target = dict.FindString(kDefaultTargetKey);
  const std::optional<LinkOpenMode> mode = ParseMode(dict.FindString(kModeKey));
  if (!target || !mode) {
    return std::nullopt;
  }
  DefaultRoute route{.mode = *mode};
  if (*target != kLastActiveTarget) {
    base::Uuid id = base::Uuid::ParseLowercase(*target);
    if (!id.is_valid()) {
      return std::nullopt;
    }
    route.target_workspace_id = std::move(id);
  }
  return route;
}

RouteResult FinishRoute(std::optional<size_t> rule_index,
                        const std::optional<base::Uuid>& target,
                        LinkOpenMode mode,
                        const std::set<base::Uuid>* available_workspaces) {
  RouteResult result{.disposition = RouteDisposition::kRouted,
                     .rule_index = rule_index,
                     .target_workspace_id = target,
                     .mode = mode};
  if (target && available_workspaces &&
      !available_workspaces->contains(*target)) {
    result.disposition = RouteDisposition::kNeedsTargetChoice;
    result.unavailable_target = target;
    result.target_workspace_id.reset();
  }
  return result;
}

RouteResult Resolve(const RoutingSettings& settings,
                    const GURL& url,
                    const std::set<base::Uuid>* available_workspaces) {
  if (!IsRoutableUrl(url)) {
    return {};
  }
  for (size_t i = 0; i < settings.rules.size(); ++i) {
    const RoutingRule& rule = settings.rules[i];
    if (rule.enabled && RuleMatchesUrl(rule, url)) {
      return FinishRoute(i, rule.target_workspace_id, rule.mode,
                         available_workspaces);
    }
  }
  return FinishRoute(std::nullopt, settings.default_route.target_workspace_id,
                     settings.default_route.mode, available_workspaces);
}

}  // namespace

bool IsRoutableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty();
}

std::optional<std::string> NormalizeRuleHost(std::string_view input) {
  if (input.empty()) {
    return std::nullopt;
  }
  std::string candidate(input);
  const bool bracketed = candidate.front() == '[' && candidate.back() == ']';
  if (!bracketed && HasChar(candidate, ':')) {
    // An unbracketed IPv6 literal; anything else with ':' carries a port.
    candidate = base::StrCat({"[", candidate, "]"});
  }
  for (const char c : std::string_view(candidate)) {
    if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '@' || c == ' ' ||
        c == '%') {
      return std::nullopt;
    }
  }
  const GURL probe(base::StrCat({"http://", candidate, "/"}));
  if (!probe.is_valid() || probe.has_port() || probe.host().empty()) {
    return std::nullopt;
  }
  if (HasChar(candidate, ':') && !probe.HostIsIPAddress()) {
    return std::nullopt;
  }
  const std::string_view host = StripTrailingDot(probe.host());
  if (host.empty() || host.back() == '.') {
    return std::nullopt;
  }
  return std::string(host);
}

std::optional<std::string> NormalizeRulePathPrefix(std::string_view input) {
  if (input.empty() || input == "/") {
    return std::string();
  }
  if (input.front() != '/' || HasChar(input, '?') || HasChar(input, '#')) {
    return std::nullopt;
  }
  const GURL probe(base::StrCat({"http://h", input}));
  if (!probe.is_valid()) {
    return std::nullopt;
  }
  std::string path(probe.path());
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  if (path == "/") {
    return std::string();
  }
  return path;
}

bool RuleMatchesUrl(const RoutingRule& rule, const GURL& url) {
  if (!IsRoutableUrl(url) || rule.host.empty()) {
    return false;
  }
  const std::string_view host = StripTrailingDot(url.host());
  bool host_matches = host == rule.host;
  if (!host_matches && rule.include_subdomains && !IsExactOnlyHost(rule.host) &&
      host.size() > rule.host.size() + 1) {
    // Label boundary: "a.example.com" matches, "notexample.com" and
    // "example.com.evil.net" never do.
    host_matches = host.ends_with(rule.host) &&
                   host[host.size() - rule.host.size() - 1] == '.';
  }
  if (!host_matches) {
    return false;
  }
  if (rule.path_prefix.empty()) {
    return true;
  }
  const std::string_view path = url.path();
  return path == rule.path_prefix || (path.starts_with(rule.path_prefix) &&
                                      path[rule.path_prefix.size()] == '/');
}

RouteResult ResolveRoute(const RoutingSettings& settings, const GURL& url) {
  return Resolve(settings, url, nullptr);
}

RouteResult ResolveRoute(const RoutingSettings& settings,
                         const GURL& url,
                         const std::set<base::Uuid>& available_workspaces) {
  return Resolve(settings, url, &available_workspaces);
}

std::vector<GURL> NormalizeAndDedupUrls(const std::vector<GURL>& urls) {
  std::vector<GURL> result;
  std::set<std::string> seen;
  for (const GURL& url : urls) {
    if (IsRoutableUrl(url) && seen.insert(url.spec()).second) {
      result.push_back(url);
    }
  }
  return result;
}

RoutingSettings ParseRoutingSettings(const base::DictValue& dict) {
  const std::optional<int> version = dict.FindInt(kVersionKey);
  const std::optional<bool> enabled = dict.FindBool(kEnabledKey);
  const base::ListValue* rules = dict.FindList(kRulesKey);
  const base::DictValue* default_dict = dict.FindDict(kDefaultKey);
  if (version != kLinkRoutingSettingsVersion || !enabled || !rules ||
      !default_dict) {
    return {};
  }
  std::optional<DefaultRoute> default_route = DecodeDefault(*default_dict);
  if (!default_route) {
    return {};
  }
  RoutingSettings settings{.enabled = *enabled,
                           .default_route = std::move(*default_route)};
  std::set<base::Uuid> ids;
  for (const base::Value& value : *rules) {
    std::optional<RoutingRule> rule = DecodeRule(value);
    if (rule && ids.insert(rule->id).second) {
      settings.rules.push_back(std::move(*rule));
    }
  }
  return settings;
}

base::DictValue SerializeRoutingSettings(const RoutingSettings& settings) {
  base::ListValue rules;
  for (const RoutingRule& rule : settings.rules) {
    base::DictValue dict;
    dict.Set(kIdKey, rule.id.AsLowercaseString());
    dict.Set(kEnabledKey, rule.enabled);
    dict.Set(kHostKey, rule.host);
    dict.Set(kIncludeSubdomainsKey, rule.include_subdomains);
    if (!rule.path_prefix.empty()) {
      dict.Set(kPathPrefixKey, rule.path_prefix);
    }
    dict.Set(kTargetKey, rule.target_workspace_id.AsLowercaseString());
    dict.Set(kModeKey, ModeName(rule.mode));
    rules.Append(std::move(dict));
  }
  base::DictValue default_dict;
  default_dict.Set(
      kDefaultTargetKey,
      settings.default_route.target_workspace_id
          ? settings.default_route.target_workspace_id->AsLowercaseString()
          : std::string(kLastActiveTarget));
  default_dict.Set(kModeKey, ModeName(settings.default_route.mode));

  base::DictValue dict;
  dict.Set(kVersionKey, kLinkRoutingSettingsVersion);
  dict.Set(kEnabledKey, settings.enabled);
  dict.Set(kRulesKey, std::move(rules));
  dict.Set(kDefaultKey, std::move(default_dict));
  return dict;
}

RoutingSettings ReadRoutingSettings(const PrefService& prefs) {
  if (!prefs.FindPreference(kLinkRoutingPref)) {
    return {};
  }
  return ParseRoutingSettings(prefs.GetDict(kLinkRoutingPref));
}

bool WriteRoutingSettings(PrefService* prefs, const RoutingSettings& settings) {
  if (!prefs || !prefs->FindPreference(kLinkRoutingPref) ||
      prefs->IsManagedPreference(kLinkRoutingPref)) {
    return false;
  }
  // Only canonical settings are stored, so a later strict parse keeps them.
  if (ParseRoutingSettings(SerializeRoutingSettings(settings)) != settings) {
    return false;
  }
  prefs->SetDict(kLinkRoutingPref, SerializeRoutingSettings(settings));
  return true;
}

}  // namespace ahoi::navigation
