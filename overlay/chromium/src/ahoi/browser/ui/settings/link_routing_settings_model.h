// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SETTINGS_LINK_ROUTING_SETTINGS_MODEL_H_
#define AHOI_BROWSER_UI_SETTINGS_LINK_ROUTING_SETTINGS_MODEL_H_

#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "base/uuid.h"
#include "base/values.h"

namespace ahoi::settings {

// Pure WebUI model of the Ahoi settings page's "Workspace-Routing externer
// Links" editor. The handler supplies the stored settings and the choosable
// Workspaces; everything sent to the page is built here, and every edit is
// validated here before the handler persists it with WriteRoutingSettings().

// A Workspace the editor may choose: a normal Workspace of the main Profile
// or a fully separated Workspace. Only the logical id is ever stored.
struct LinkRoutingWorkspace {
  base::Uuid id;
  std::string name;
  bool separated = false;
};

// Page state: {enabled, canChange, rules[], defaultRoute, workspaces[],
// labels, action, error, errorLabel}. Each rule carries the editor value
// (navigation::RuleToEditorValue) plus "targetName" and "targetAvailable".
base::DictValue BuildLinkRoutingState(
    const navigation::RoutingSettings& settings,
    const std::vector<LinkRoutingWorkspace>& workspaces,
    bool can_change);

// Applies one editor action to `settings`. `payload` is the action's dict:
//   setEnabled {enabled}, addRule {rule fields}, updateRule {id, rule fields},
//   deleteRule {id}, moveRule {id, delta}, setDefault {target, mode}, reset {}.
// Returns "" on success, otherwise an error name ("invalidHost",
// "invalidPath", "invalidTarget", "invalidMode", "unknownRule", "cannotMove",
// "invalidRequest") and leaves `settings` unchanged. An existing rule or the
// default route may keep its current, no longer available target; a new
// target must be one of `workspaces`.
std::string ApplyLinkRoutingAction(
    navigation::RoutingSettings& settings,
    std::string_view action,
    const base::DictValue& payload,
    const std::vector<LinkRoutingWorkspace>& workspaces);

// Localized text for an error name from ApplyLinkRoutingAction() or the
// handler ("writeFailed", "managed", "unavailable").
std::string LinkRoutingErrorLabel(std::string_view error);

// Live "Beispiel-Link" result: {status: "empty" | "invalidUrl" |
// "notRoutable" | "disabled" | "routed" | "needsChoice", ruleIndex (-1 for
// the default route), allPorts (a rule decided; rules match every port of
// their host), ruleId, targetId, text}. `input` without a scheme is
// read as https.
base::DictValue DescribeLinkRoutingExample(
    const navigation::RoutingSettings& settings,
    const std::vector<LinkRoutingWorkspace>& workspaces,
    std::string_view input);

}  // namespace ahoi::settings

#endif  // AHOI_BROWSER_UI_SETTINGS_LINK_ROUTING_SETTINGS_MODEL_H_
