// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing_editor.h"

#include <utility>
#include <vector>

#include "base/types/expected_macros.h"

namespace ahoi::navigation {

namespace {

constexpr char kIdKey[] = "id";
constexpr char kEnabledKey[] = "enabled";
constexpr char kHostKey[] = "host";
constexpr char kIncludeSubdomainsKey[] = "includeSubdomains";
constexpr char kPathKey[] = "path";
constexpr char kTargetKey[] = "target";
constexpr char kModeKey[] = "mode";
constexpr char kLastActiveTarget[] = "last_active";
constexpr char kNormalTabName[] = "normal_tab";
constexpr char kQuickWindowName[] = "quick_window";

base::expected<base::Uuid, RoutingEditError> ParseTarget(
    const std::string& target,
    const std::set<base::Uuid>& allowed_targets) {
  const base::Uuid id = base::Uuid::ParseCaseInsensitive(target);
  if (!id.is_valid() || !allowed_targets.contains(id)) {
    return base::unexpected(RoutingEditError::kInvalidTarget);
  }
  return id;
}

bool IsExactHostRule(const RoutingRule& rule, std::string_view host) {
  return rule.host == host && !rule.include_subdomains &&
         rule.path_prefix.empty();
}

}  // namespace

std::string_view RoutingEditErrorName(RoutingEditError error) {
  switch (error) {
    case RoutingEditError::kNone:
      return "";
    case RoutingEditError::kInvalidRequest:
      return "invalidRequest";
    case RoutingEditError::kInvalidHost:
      return "invalidHost";
    case RoutingEditError::kInvalidPath:
      return "invalidPath";
    case RoutingEditError::kInvalidTarget:
      return "invalidTarget";
    case RoutingEditError::kInvalidMode:
      return "invalidMode";
    case RoutingEditError::kUnknownRule:
      return "unknownRule";
    case RoutingEditError::kCannotMove:
      return "cannotMove";
  }
  return "invalidRequest";
}

std::string_view LinkOpenModeName(LinkOpenMode mode) {
  return mode == LinkOpenMode::kQuickWindow ? kQuickWindowName : kNormalTabName;
}

std::optional<LinkOpenMode> ParseLinkOpenModeName(std::string_view name) {
  if (name == kNormalTabName) {
    return LinkOpenMode::kNormalTab;
  }
  if (name == kQuickWindowName) {
    return LinkOpenMode::kQuickWindow;
  }
  return std::nullopt;
}

base::expected<RoutingRule, RoutingEditError> RuleFromEditorValue(
    const base::DictValue& value,
    const base::Uuid& id,
    const std::set<base::Uuid>& allowed_targets) {
  const std::optional<bool> enabled = value.FindBool(kEnabledKey);
  const std::string* host = value.FindString(kHostKey);
  const std::optional<bool> include_subdomains =
      value.FindBool(kIncludeSubdomainsKey);
  const std::string* path = value.FindString(kPathKey);
  const std::string* target = value.FindString(kTargetKey);
  const std::string* mode = value.FindString(kModeKey);
  if (!id.is_valid() || !enabled || !host || !include_subdomains || !path ||
      !target || !mode) {
    return base::unexpected(RoutingEditError::kInvalidRequest);
  }
  std::optional<std::string> normalized_host = NormalizeRuleHost(*host);
  if (!normalized_host) {
    return base::unexpected(RoutingEditError::kInvalidHost);
  }
  std::optional<std::string> normalized_path = NormalizeRulePathPrefix(*path);
  if (!normalized_path) {
    return base::unexpected(RoutingEditError::kInvalidPath);
  }
  ASSIGN_OR_RETURN(base::Uuid target_id, ParseTarget(*target, allowed_targets));
  const std::optional<LinkOpenMode> open_mode = ParseLinkOpenModeName(*mode);
  if (!open_mode) {
    return base::unexpected(RoutingEditError::kInvalidMode);
  }
  return RoutingRule{
      .id = id,
      .enabled = *enabled,
      .host = std::move(*normalized_host),
      .include_subdomains = *include_subdomains,
      .path_prefix = std::move(*normalized_path),
      .target_workspace_id = std::move(target_id),
      .mode = *open_mode,
  };
}

base::DictValue RuleToEditorValue(const RoutingRule& rule) {
  base::DictValue value;
  value.Set(kIdKey, rule.id.AsLowercaseString());
  value.Set(kEnabledKey, rule.enabled);
  value.Set(kHostKey, rule.host);
  value.Set(kIncludeSubdomainsKey, rule.include_subdomains);
  value.Set(kPathKey, rule.path_prefix);
  value.Set(kTargetKey, rule.target_workspace_id.AsLowercaseString());
  value.Set(kModeKey, LinkOpenModeName(rule.mode));
  return value;
}

base::expected<DefaultRoute, RoutingEditError> DefaultRouteFromEditorValue(
    const base::DictValue& value,
    const std::set<base::Uuid>& allowed_targets) {
  const std::string* target = value.FindString(kTargetKey);
  const std::string* mode = value.FindString(kModeKey);
  if (!target || !mode) {
    return base::unexpected(RoutingEditError::kInvalidRequest);
  }
  DefaultRoute route;
  if (*target != kLastActiveTarget) {
    ASSIGN_OR_RETURN(route.target_workspace_id,
                     ParseTarget(*target, allowed_targets));
  }
  const std::optional<LinkOpenMode> open_mode = ParseLinkOpenModeName(*mode);
  if (!open_mode) {
    return base::unexpected(RoutingEditError::kInvalidMode);
  }
  route.mode = *open_mode;
  return route;
}

std::optional<size_t> FindRuleIndex(const RoutingSettings& settings,
                                    const base::Uuid& id) {
  for (size_t i = 0; i < settings.rules.size(); ++i) {
    if (settings.rules[i].id == id) {
      return i;
    }
  }
  return std::nullopt;
}

RoutingEditError AddRule(RoutingSettings& settings, RoutingRule rule) {
  if (!rule.id.is_valid() || FindRuleIndex(settings, rule.id)) {
    return RoutingEditError::kInvalidRequest;
  }
  settings.rules.push_back(std::move(rule));
  return RoutingEditError::kNone;
}

RoutingEditError UpdateRule(RoutingSettings& settings, RoutingRule rule) {
  const std::optional<size_t> index = FindRuleIndex(settings, rule.id);
  if (!index) {
    return RoutingEditError::kUnknownRule;
  }
  settings.rules[*index] = std::move(rule);
  return RoutingEditError::kNone;
}

RoutingEditError DeleteRule(RoutingSettings& settings, const base::Uuid& id) {
  const std::optional<size_t> index = FindRuleIndex(settings, id);
  if (!index) {
    return RoutingEditError::kUnknownRule;
  }
  settings.rules.erase(settings.rules.begin() + *index);
  return RoutingEditError::kNone;
}

RoutingEditError MoveRule(RoutingSettings& settings,
                          const base::Uuid& id,
                          int delta) {
  const std::optional<size_t> index = FindRuleIndex(settings, id);
  if (!index) {
    return RoutingEditError::kUnknownRule;
  }
  if (delta != -1 && delta != 1) {
    return RoutingEditError::kInvalidRequest;
  }
  if ((delta < 0 && *index == 0) ||
      (delta > 0 && *index + 1 >= settings.rules.size())) {
    return RoutingEditError::kCannotMove;
  }
  const size_t other = delta < 0 ? *index - 1 : *index + 1;
  std::swap(settings.rules[*index], settings.rules[other]);
  return RoutingEditError::kNone;
}

RoutingSettings DefaultRoutingSettings() {
  return RoutingSettings();
}

std::optional<size_t> RememberSiteChoice(RoutingSettings& settings,
                                         const GURL& url,
                                         const base::Uuid& target,
                                         LinkOpenMode mode,
                                         const base::Uuid& new_id) {
  if (!IsRoutableUrl(url) || !target.is_valid()) {
    return std::nullopt;
  }
  const std::optional<std::string> host = NormalizeRuleHost(url.host());
  if (!host) {
    return std::nullopt;
  }
  const RouteResult route = ResolveRoute(settings, url);
  if (route.rule_index &&
      IsExactHostRule(settings.rules[*route.rule_index], *host)) {
    RoutingRule& rule = settings.rules[*route.rule_index];
    rule.enabled = true;
    rule.target_workspace_id = target;
    rule.mode = mode;
    return route.rule_index;
  }
  if (!new_id.is_valid() || FindRuleIndex(settings, new_id)) {
    return std::nullopt;
  }
  // Any other exact-host rule for this host is disabled or shadowed by the
  // winner; the remembered choice replaces it.
  size_t insert_at = route.rule_index.value_or(settings.rules.size());
  std::vector<RoutingRule> kept;
  kept.reserve(settings.rules.size() + 1);
  for (size_t i = 0; i < settings.rules.size(); ++i) {
    if (IsExactHostRule(settings.rules[i], *host)) {
      if (i < insert_at) {
        --insert_at;
      }
      continue;
    }
    kept.push_back(std::move(settings.rules[i]));
  }
  kept.insert(kept.begin() + insert_at,
              RoutingRule{.id = new_id,
                          .enabled = true,
                          .host = *host,
                          .include_subdomains = false,
                          .target_workspace_id = target,
                          .mode = mode});
  settings.rules = std::move(kept);
  return insert_at;
}

}  // namespace ahoi::navigation
