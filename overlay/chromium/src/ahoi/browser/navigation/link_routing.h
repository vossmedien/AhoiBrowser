// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_LINK_ROUTING_H_
#define AHOI_BROWSER_NAVIGATION_LINK_ROUTING_H_

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/uuid.h"
#include "base/values.h"
#include "url/gurl.h"

class PrefService;

namespace ahoi::navigation {

// Workspace routing of external links (master contract, "Workspace-Routing
// externer Links"; acceptance WS-ISO-06). Pure model and matcher: no Profile,
// window or WebContents state. Targets are logical Workspace UUIDs only, never
// Profile paths or partition ids, so the settings stay setup-sync safe.

// Profile pref (main Profile) holding the serialized RoutingSettings.
inline constexpr char kLinkRoutingPref[] = "ahoi.navigation.link_routing";
inline constexpr int kLinkRoutingSettingsVersion = 1;

enum class LinkOpenMode {
  kNormalTab = 0,
  kQuickWindow = 1,
};

struct RoutingRule {
  base::Uuid id;
  bool enabled = true;
  // Normalized: lowercase, canonical, no trailing dot. An IP literal (IPv6 in
  // brackets) or "localhost" only matches exactly.
  std::string host;
  bool include_subdomains = false;
  // Empty, or a canonical path starting with "/" without a trailing "/".
  // Matches at segment boundaries only: "/a" matches "/a" and "/a/b", never
  // "/ab".
  std::string path_prefix;
  base::Uuid target_workspace_id;
  LinkOpenMode mode = LinkOpenMode::kNormalTab;

  bool operator==(const RoutingRule&) const = default;
};

struct DefaultRoute {
  // nullopt: the last active Workspace; otherwise an explicit Workspace.
  std::optional<base::Uuid> target_workspace_id;
  LinkOpenMode mode = LinkOpenMode::kNormalTab;

  bool operator==(const DefaultRoute&) const = default;
};

struct RoutingSettings {
  bool enabled = true;
  // Evaluated in order; the first enabled matching rule wins.
  std::vector<RoutingRule> rules;
  DefaultRoute default_route;

  bool operator==(const RoutingSettings&) const = default;
};

enum class RouteDisposition {
  // Not an http(s) URL with a host: the caller keeps Chromium's behavior.
  kNotRoutable,
  kRouted,
  // The winning explicit target no longer exists. `target_workspace_id` is
  // then the fallback (last active Workspace) and `unavailable_target` names
  // the missing Workspace so a chooser can be offered.
  kNeedsTargetChoice,
};

struct RouteResult {
  RouteDisposition disposition = RouteDisposition::kNotRoutable;
  // Index into RoutingSettings::rules; nullopt for the default route.
  std::optional<size_t> rule_index;
  // nullopt: the last active Workspace.
  std::optional<base::Uuid> target_workspace_id;
  LinkOpenMode mode = LinkOpenMode::kNormalTab;
  std::optional<base::Uuid> unavailable_target;

  bool operator==(const RouteResult&) const = default;
};

// Only valid http/https URLs with a host are routed.
bool IsRoutableUrl(const GURL& url);

// Canonical rule host for `input` ("Example.COM." -> "example.com"), or
// nullopt when it is not a plain host (scheme, port, path, userinfo, empty).
std::optional<std::string> NormalizeRuleHost(std::string_view input);
// Canonical path prefix ("" and "/" -> "", "/a/" -> "/a"), or nullopt when it
// does not start with "/" or carries a query or fragment.
std::optional<std::string> NormalizeRulePathPrefix(std::string_view input);

// True when `rule` (ignoring `enabled`) matches `url`. Query and fragment
// never take part; scheme (http/https) and port do not restrict a rule.
bool RuleMatchesUrl(const RoutingRule& rule, const GURL& url);

// Resolves without an availability check: every explicit target is assumed
// to exist.
RouteResult ResolveRoute(const RoutingSettings& settings, const GURL& url);
// Resolves and reports kNeedsTargetChoice when the winning explicit target is
// not in `available_workspaces`.
RouteResult ResolveRoute(const RoutingSettings& settings,
                         const GURL& url,
                         const std::set<base::Uuid>& available_workspaces);

// Keeps the first occurrence of every routable URL, in input order.
std::vector<GURL> NormalizeAndDedupUrls(const std::vector<GURL>& urls);

// Strict version-1 codec. An unknown version or a malformed top level yields
// default settings; a malformed or duplicate rule is dropped, never repaired.
RoutingSettings ParseRoutingSettings(const base::DictValue& dict);
base::DictValue SerializeRoutingSettings(const RoutingSettings& settings);

RoutingSettings ReadRoutingSettings(const PrefService& prefs);
// Fails for an unregistered or managed pref, and for settings that would not
// survive the strict parse unchanged (non-canonical host or path, invalid or
// duplicate ids).
bool WriteRoutingSettings(PrefService* prefs, const RoutingSettings& settings);

}  // namespace ahoi::navigation

#endif  // AHOI_BROWSER_NAVIGATION_LINK_ROUTING_H_
