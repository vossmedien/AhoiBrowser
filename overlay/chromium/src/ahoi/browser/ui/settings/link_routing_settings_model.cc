// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/link_routing_settings_model.h"

#include <optional>
#include <set>
#include <utility>

#include "ahoi/browser/navigation/link_routing_editor.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace ahoi::settings {

namespace {

using navigation::LinkOpenMode;
using navigation::RoutingEditError;
using navigation::RoutingRule;
using navigation::RoutingSettings;

constexpr char kLastActive[] = "last_active";

std::string Text(const char* de, const char* en) {
  return std::string(base::i18n::GetConfiguredLocale().starts_with("de") ? de
                                                                         : en);
}

base::DictValue Labels() {
  base::DictValue labels;
  labels.Set("title", Text("Workspace-Routing externer Links",
                           "Workspace routing for external links"));
  labels.Set("description",
             Text("Links aus anderen Apps öffnen im passenden Workspace. "
                  "Regeln werden von oben nach unten geprüft; die erste "
                  "aktive passende Regel gewinnt.",
                  "Links from other apps open in the matching Workspace. "
                  "Rules are checked from top to bottom; the first enabled "
                  "matching rule wins."));
  labels.Set("enabled", Text("Externe Links routen", "Route external links"));
  labels.Set("rules", Text("Regeln", "Rules"));
  labels.Set("noRules",
             Text("Noch keine Regeln – alle Links nutzen die Standardroute.",
                  "No rules yet – every link uses the default route."));
  labels.Set("ruleEnabled", Text("Regel aktiv", "Rule enabled"));
  labels.Set("host",
             Text("Host (z. B. example.com)", "Host (e.g. example.com)"));
  labels.Set("hostPortHint",
             Text("Ohne Port: gilt für alle Ports dieses Hosts.",
                  "Without a port: applies to every port of this host."));
  labels.Set("includeSubdomains",
             Text("Subdomains einschließen", "Include subdomains"));
  labels.Set("path", Text("Pfad (optional, z. B. /mail)",
                          "Path (optional, e.g. /mail)"));
  labels.Set("target", Text("Ziel-Workspace", "Target Workspace"));
  labels.Set("mode", Text("Öffnen als", "Open as"));
  labels.Set("normalTab", Text("Normaler Tab", "Normal tab"));
  labels.Set("quickWindow", Text("Quick Window", "Quick Window"));
  labels.Set("add", Text("Regel hinzufügen", "Add rule"));
  labels.Set("delete", Text("Löschen", "Delete"));
  labels.Set("moveUp", Text("Nach oben", "Move up"));
  labels.Set("moveDown", Text("Nach unten", "Move down"));
  labels.Set("reset", Text("Auf Standard zurücksetzen", "Reset to defaults"));
  labels.Set("resetHint",
             Text("Entfernt alle Regeln und setzt die Standardroute zurück.",
                  "Removes every rule and resets the default route."));
  labels.Set("resetConfirm", Text("Wirklich alle Regeln entfernen?",
                                  "Really remove every rule?"));
  labels.Set("defaultRoute", Text("Standardroute", "Default route"));
  labels.Set("defaultRouteHint", Text("Gilt, wenn keine Regel passt.",
                                      "Used when no rule matches."));
  labels.Set("lastActive",
             Text("Zuletzt aktiver Workspace", "Last active Workspace"));
  labels.Set("unavailableTarget", Text("Nicht mehr verfügbarer Workspace",
                                       "Workspace no longer available"));
  labels.Set("separated", Text("vollständig getrennt", "fully separated"));
  labels.Set("example", Text("Beispiel-Link", "Example link"));
  labels.Set("examplePlaceholder", "https://example.com/mail");
  labels.Set("rememberHint",
             Text("Ist ein gewähltes Ziel nicht mehr verfügbar, fragt Ahoi "
                  "beim Öffnen nach einem Workspace. „Für diese Website "
                  "merken“ speichert nur diese bewusste Wahl.",
                  "If a chosen target is no longer available, Ahoi asks for "
                  "a Workspace when the link opens. “Remember for this "
                  "website” stores only that deliberate choice."));
  labels.Set("saved", Text("Gespeichert", "Saved"));
  return labels;
}

const LinkRoutingWorkspace* FindWorkspace(
    const std::vector<LinkRoutingWorkspace>& workspaces,
    const base::Uuid& id) {
  for (const LinkRoutingWorkspace& workspace : workspaces) {
    if (workspace.id == id) {
      return &workspace;
    }
  }
  return nullptr;
}

std::string WorkspaceName(const std::vector<LinkRoutingWorkspace>& workspaces,
                          const std::optional<base::Uuid>& id) {
  if (!id) {
    return Text("Zuletzt aktiver Workspace", "Last active Workspace");
  }
  const LinkRoutingWorkspace* workspace = FindWorkspace(workspaces, *id);
  return workspace ? workspace->name
                   : Text("Nicht mehr verfügbarer Workspace",
                          "Workspace no longer available");
}

std::string ModeLabel(LinkOpenMode mode) {
  return mode == LinkOpenMode::kQuickWindow
             ? Text("Quick Window", "Quick Window")
             : Text("Normaler Tab", "Normal tab");
}

std::string RuleLabel(const RoutingRule& rule, size_t index) {
  return base::StrCat({Text("Regel ", "Rule "), base::NumberToString(index + 1),
                       " (", rule.include_subdomains ? "*." : "", rule.host,
                       rule.path_prefix, ")"});
}

std::set<base::Uuid> AllowedTargets(
    const std::vector<LinkRoutingWorkspace>& workspaces) {
  std::set<base::Uuid> ids;
  for (const LinkRoutingWorkspace& workspace : workspaces) {
    ids.insert(workspace.id);
  }
  return ids;
}

std::string ErrorName(RoutingEditError error) {
  return std::string(navigation::RoutingEditErrorName(error));
}

base::Uuid PayloadId(const base::DictValue& payload) {
  const std::string* id = payload.FindString("id");
  return id ? base::Uuid::ParseCaseInsensitive(*id) : base::Uuid();
}

GURL ParseExampleInput(std::string_view trimmed) {
  const GURL direct{std::string(trimmed)};
  const bool has_authority = trimmed.find("://") != std::string_view::npos;
  if (direct.is_valid() &&
      (direct.SchemeIsHTTPOrHTTPS() || has_authority ||
       (!direct.IsStandard() && direct.scheme() != "localhost" &&
        direct.scheme().find('.') == std::string::npos))) {
    return direct;
  }
  return GURL(base::StrCat({"https://", trimmed}));
}

}  // namespace

base::DictValue BuildLinkRoutingState(
    const RoutingSettings& settings,
    const std::vector<LinkRoutingWorkspace>& workspaces,
    bool can_change) {
  base::DictValue state;
  state.Set("labels", Labels());
  state.Set("canChange", can_change);
  state.Set("enabled", settings.enabled);
  base::ListValue rules;
  for (const RoutingRule& rule : settings.rules) {
    base::DictValue value = navigation::RuleToEditorValue(rule);
    value.Set("targetAvailable",
              FindWorkspace(workspaces, rule.target_workspace_id) != nullptr);
    value.Set("targetName",
              WorkspaceName(workspaces, rule.target_workspace_id));
    rules.Append(std::move(value));
  }
  state.Set("rules", std::move(rules));
  base::DictValue default_route;
  const std::optional<base::Uuid>& default_target =
      settings.default_route.target_workspace_id;
  default_route.Set("target", default_target
                                  ? default_target->AsLowercaseString()
                                  : std::string(kLastActive));
  default_route.Set("mode",
                    navigation::LinkOpenModeName(settings.default_route.mode));
  default_route.Set(
      "targetAvailable",
      !default_target || FindWorkspace(workspaces, *default_target) != nullptr);
  state.Set("defaultRoute", std::move(default_route));
  base::ListValue workspace_list;
  for (const LinkRoutingWorkspace& workspace : workspaces) {
    base::DictValue value;
    value.Set("id", workspace.id.AsLowercaseString());
    value.Set("name", workspace.name);
    value.Set("separated", workspace.separated);
    workspace_list.Append(std::move(value));
  }
  state.Set("workspaces", std::move(workspace_list));
  state.Set("action", "");
  state.Set("error", "");
  state.Set("errorLabel", "");
  return state;
}

std::string ApplyLinkRoutingAction(
    RoutingSettings& settings,
    std::string_view action,
    const base::DictValue& payload,
    const std::vector<LinkRoutingWorkspace>& workspaces) {
  std::set<base::Uuid> allowed = AllowedTargets(workspaces);
  RoutingSettings edited = settings;
  RoutingEditError error = RoutingEditError::kInvalidRequest;
  if (action == "setEnabled") {
    if (const std::optional<bool> enabled = payload.FindBool("enabled")) {
      edited.enabled = *enabled;
      error = RoutingEditError::kNone;
    }
  } else if (action == "addRule") {
    auto rule = navigation::RuleFromEditorValue(
        payload, base::Uuid::GenerateRandomV4(), allowed);
    error = rule.has_value() ? navigation::AddRule(edited, std::move(*rule))
                             : rule.error();
  } else if (action == "updateRule") {
    const base::Uuid id = PayloadId(payload);
    const std::optional<size_t> index = navigation::FindRuleIndex(edited, id);
    if (!index) {
      error = RoutingEditError::kUnknownRule;
    } else {
      // Toggling or editing a rule must not require re-choosing a target
      // that disappeared; it may keep its current one.
      allowed.insert(edited.rules[*index].target_workspace_id);
      auto rule = navigation::RuleFromEditorValue(payload, id, allowed);
      error = rule.has_value()
                  ? navigation::UpdateRule(edited, std::move(*rule))
                  : rule.error();
    }
  } else if (action == "deleteRule") {
    error = navigation::DeleteRule(edited, PayloadId(payload));
  } else if (action == "moveRule") {
    const std::optional<int> delta = payload.FindInt("delta");
    error = delta ? navigation::MoveRule(edited, PayloadId(payload), *delta)
                  : RoutingEditError::kInvalidRequest;
  } else if (action == "setDefault") {
    if (edited.default_route.target_workspace_id) {
      allowed.insert(*edited.default_route.target_workspace_id);
    }
    auto route = navigation::DefaultRouteFromEditorValue(payload, allowed);
    if (route.has_value()) {
      edited.default_route = std::move(*route);
      error = RoutingEditError::kNone;
    } else {
      error = route.error();
    }
  } else if (action == "reset") {
    edited = navigation::DefaultRoutingSettings();
    error = RoutingEditError::kNone;
  }
  if (error != RoutingEditError::kNone) {
    return ErrorName(error);
  }
  settings = std::move(edited);
  return std::string();
}

std::string LinkRoutingErrorLabel(std::string_view error) {
  if (error.empty()) {
    return std::string();
  }
  if (error == "invalidHost") {
    return Text(
        "Ungültiger Host: nur ein Hostname wie example.com, ohne "
        "Schema, Port, Pfad oder Zugangsdaten.",
        "Invalid host: use a host name like example.com, without "
        "scheme, port, path or credentials.");
  }
  if (error == "invalidPath") {
    return Text("Ungültiger Pfad: er beginnt mit / und enthält kein ? oder #.",
                "Invalid path: it starts with / and contains no ? or #.");
  }
  if (error == "invalidTarget") {
    return Text("Bitte einen vorhandenen Workspace als Ziel wählen.",
                "Choose an existing Workspace as the target.");
  }
  if (error == "invalidMode") {
    return Text("Unbekannte Öffnungsart.", "Unknown open mode.");
  }
  if (error == "unknownRule") {
    return Text("Die Regel existiert nicht mehr; die Liste wurde neu geladen.",
                "The rule no longer exists; the list was reloaded.");
  }
  if (error == "cannotMove") {
    return Text("Die Regel steht bereits am Rand der Liste.",
                "The rule is already at the edge of the list.");
  }
  if (error == "writeFailed") {
    return Text("Die Änderung konnte nicht gespeichert werden.",
                "The change could not be saved.");
  }
  if (error == "managed") {
    return Text("Diese Einstellung wird von deiner Organisation verwaltet.",
                "This setting is managed by your organization.");
  }
  if (error == "unavailable") {
    return Text("Das Hauptprofil ist gerade nicht geladen.",
                "The main profile is not loaded right now.");
  }
  return Text("Ungültige Anfrage; nichts wurde geändert.",
              "Invalid request; nothing was changed.");
}

base::DictValue DescribeLinkRoutingExample(
    const RoutingSettings& settings,
    const std::vector<LinkRoutingWorkspace>& workspaces,
    std::string_view input) {
  base::DictValue result;
  result.Set("ruleIndex", -1);
  result.Set("allPorts", false);
  result.Set("ruleId", "");
  result.Set("targetId", "");
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(input, base::TRIM_ALL);
  if (trimmed.empty()) {
    result.Set("status", "empty");
    result.Set("text", "");
    return result;
  }
  const GURL url = ParseExampleInput(trimmed);
  if (!url.is_valid()) {
    result.Set("status", "invalidUrl");
    result.Set("text", Text("Keine gültige Adresse.", "Not a valid address."));
    return result;
  }
  if (!navigation::IsRoutableUrl(url)) {
    result.Set("status", "notRoutable");
    result.Set("text", Text("Nur HTTP- und HTTPS-Links werden geroutet.",
                            "Only HTTP and HTTPS links are routed."));
    return result;
  }
  if (!settings.enabled) {
    result.Set("status", "disabled");
    result.Set("text", Text("Routing ist ausgeschaltet – der Link öffnet wie "
                            "gewohnt.",
                            "Routing is off – the link opens as usual."));
    return result;
  }
  const navigation::RouteResult route =
      navigation::ResolveRoute(settings, url, AllowedTargets(workspaces));
  std::string source = Text("Standardroute", "Default route");
  // Rules carry no port (crest review 020): a rule matches every port of its
  // host, and the example says so whenever a rule decides.
  result.Set("allPorts", route.rule_index.has_value());
  if (route.rule_index) {
    const RoutingRule& rule = settings.rules[*route.rule_index];
    result.Set("ruleIndex", static_cast<int>(*route.rule_index));
    result.Set("ruleId", rule.id.AsLowercaseString());
    source = base::StrCat({RuleLabel(rule, *route.rule_index), ", ",
                           Text("gilt für alle Ports dieses Hosts",
                                "applies to every port of this host")});
  }
  if (route.disposition == navigation::RouteDisposition::kNeedsTargetChoice) {
    result.Set("status", "needsChoice");
    result.Set("text",
               base::StrCat({source, " → ",
                             Text("Ziel nicht mehr verfügbar; beim Öffnen "
                                  "fragt Ahoi nach einem Workspace.",
                                  "target no longer available; Ahoi asks for "
                                  "a Workspace when the link opens.")}));
    return result;
  }
  result.Set("status", "routed");
  if (route.target_workspace_id) {
    result.Set("targetId", route.target_workspace_id->AsLowercaseString());
  }
  result.Set("text",
             base::StrCat({source, " → ",
                           WorkspaceName(workspaces, route.target_workspace_id),
                           " · ", ModeLabel(route.mode)}));
  return result;
}

}  // namespace ahoi::settings
