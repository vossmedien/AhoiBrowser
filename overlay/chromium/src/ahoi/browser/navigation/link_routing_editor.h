// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_LINK_ROUTING_EDITOR_H_
#define AHOI_BROWSER_NAVIGATION_LINK_ROUTING_EDITOR_H_

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "ahoi/browser/navigation/link_routing.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "base/values.h"
#include "url/gurl.h"

namespace ahoi::navigation {

// Pure edit operations behind the settings rule editor and the "Für diese
// Website merken" chooser. Every operation validates first and leaves
// `settings` unchanged on failure; nothing is repaired or silently dropped.
// Targets stay logical Workspace UUIDs, never Profile or partition ids.

enum class RoutingEditError {
  kNone,
  // Malformed editor request (missing or mistyped field).
  kInvalidRequest,
  kInvalidHost,
  kInvalidPath,
  // Not a UUID, or not a Workspace the editor may choose.
  kInvalidTarget,
  kInvalidMode,
  kUnknownRule,
  // Moving past the first or last position.
  kCannotMove,
};

// Stable wire name for the WebUI ("invalidHost", ...); "" for kNone.
std::string_view RoutingEditErrorName(RoutingEditError error);

// Editor wire names of LinkOpenMode: "normal_tab", "quick_window".
std::string_view LinkOpenModeName(LinkOpenMode mode);
std::optional<LinkOpenMode> ParseLinkOpenModeName(std::string_view name);

// A rule as the editor sends it: {enabled: bool, host: string,
// includeSubdomains: bool, path: string, target: string, mode: string}.
// `host` and `path` are normalized here (NormalizeRuleHost,
// NormalizeRulePathPrefix); `target` must be in `allowed_targets`.
base::expected<RoutingRule, RoutingEditError> RuleFromEditorValue(
    const base::DictValue& value,
    const base::Uuid& id,
    const std::set<base::Uuid>& allowed_targets);

// Editor value of a stored rule (same keys plus "id").
base::DictValue RuleToEditorValue(const RoutingRule& rule);

// The default route as the editor sends it: {target: "last_active" | uuid,
// mode: string}.
base::expected<DefaultRoute, RoutingEditError> DefaultRouteFromEditorValue(
    const base::DictValue& value,
    const std::set<base::Uuid>& allowed_targets);

// Appends `rule` (lowest priority). Fails for an id already present.
RoutingEditError AddRule(RoutingSettings& settings, RoutingRule rule);
// Replaces the rule with `rule.id`, keeping its position.
RoutingEditError UpdateRule(RoutingSettings& settings, RoutingRule rule);
RoutingEditError DeleteRule(RoutingSettings& settings, const base::Uuid& id);
// Moves the rule one position earlier (`delta` -1) or later (+1).
RoutingEditError MoveRule(RoutingSettings& settings,
                          const base::Uuid& id,
                          int delta);
// Built-in defaults: routing on, no rules, last active Workspace, normal tab.
RoutingSettings DefaultRoutingSettings();

// Index of the rule with `id`, or nullopt.
std::optional<size_t> FindRuleIndex(const RoutingSettings& settings,
                                    const base::Uuid& id);

// "Für diese Website merken": a deliberate choice to open `url`'s exact host
// in `target` with `mode`. Writes an exact-host rule (no subdomains, no path)
// that wins for `url` without changing what any earlier rule decides:
//  - the winning rule already is that exact-host rule: it is rewritten in
//    place (enabled, new target and mode);
//  - otherwise the rule is inserted directly before the winning rule, or
//    appended when the default route won; an older identical-host rule
//    further down is removed so the host keeps one remembered choice.
// Returns the index of the written rule, or nullopt for an unroutable URL or
// an invalid target (settings unchanged). `new_id` is used for an inserted
// rule.
std::optional<size_t> RememberSiteChoice(RoutingSettings& settings,
                                         const GURL& url,
                                         const base::Uuid& target,
                                         LinkOpenMode mode,
                                         const base::Uuid& new_id);

}  // namespace ahoi::navigation

#endif  // AHOI_BROWSER_NAVIGATION_LINK_ROUTING_EDITOR_H_
