// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"
#include "ahoi/browser/ui/settings/link_routing_settings_model.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

// Setup-sync note: the stored rules hold logical Workspace UUIDs only, so the
// pref can later join the positively defined setup-sync catalog next to the
// other safe browser settings (ahoi/browser/sync, browser settings catalog);
// the Sync owner decides that. It is registered device-local in
// navigation_input_prefs.cc until then.

namespace ahoi::settings {
namespace {

bool HasCallbackId(const base::ListValue& args) {
  return !args.empty() && args.front().is_string() &&
         !args.front().GetString().empty();
}

bool CanWriteRouting(Profile* profile) {
  if (!profile) {
    return false;
  }
  PrefService* prefs = profile->GetPrefs();
  return prefs && prefs->FindPreference(navigation::kLinkRoutingPref) &&
         !prefs->IsManagedPreference(navigation::kLinkRoutingPref);
}

}  // namespace

Profile* AhoiSettingsHandler::LinkRoutingProfile() const {
  // External links are routed with the main Profile's settings, whichever
  // Profile's settings page edits them.
  return session::GetLoadedMainProfile();
}

std::vector<LinkRoutingWorkspace> AhoiSettingsHandler::LinkRoutingWorkspaces()
    const {
  std::vector<LinkRoutingWorkspace> workspaces;
  Profile* main_profile = LinkRoutingProfile();
  if (!main_profile) {
    return workspaces;
  }
  // Process-wide order (ADR 0011 step 2, handoff 054).
  std::vector<session::DirectoryWorkspace> main_keys;
  std::map<base::Uuid, LinkRoutingWorkspace> by_id;
  if (WorkspaceService* service =
          WorkspaceServiceFactory::GetForProfile(main_profile)) {
    for (const tab_tree::Workspace& workspace : service->ordered_workspaces()) {
      main_keys.push_back(
          {.workspace_id = workspace.id, .sort_key = workspace.sort_key});
      by_id.emplace(workspace.id,
                    LinkRoutingWorkspace{
                        .id = workspace.id,
                        .name = base::UTF16ToUTF8(workspace.name)});
    }
  }
  const std::vector<session::IsolatedProfileEntry> isolated =
      session::GetOpenableIsolatedWorkspaces();
  for (const session::IsolatedProfileEntry& entry : isolated) {
    by_id.emplace(entry.workspace_id,
                  LinkRoutingWorkspace{.id = entry.workspace_id,
                                       .name = base::UTF16ToUTF8(entry.name),
                                       .separated = true});
  }
  for (const session::DirectoryWorkspace& key :
       session::OrderDirectoryWorkspaces(main_keys, isolated)) {
    if (auto it = by_id.find(key.workspace_id); it != by_id.end()) {
      workspaces.push_back(it->second);
    }
  }
  return workspaces;
}

base::DictValue AhoiSettingsHandler::BuildLinkRoutingStatus(
    std::string_view action,
    std::string_view error) const {
  Profile* main_profile = LinkRoutingProfile();
  const navigation::RoutingSettings settings =
      main_profile ? navigation::ReadRoutingSettings(*main_profile->GetPrefs())
                   : navigation::RoutingSettings();
  base::DictValue status = BuildLinkRoutingState(
      settings, LinkRoutingWorkspaces(), CanWriteRouting(main_profile));
  status.Set("available", main_profile != nullptr);
  status.Set("action", std::string(action));
  std::string_view shown_error = error;
  if (shown_error.empty() && !main_profile) {
    shown_error = "unavailable";
  } else if (shown_error.empty() && !CanWriteRouting(main_profile)) {
    shown_error = "managed";
  }
  status.Set("error", std::string(shown_error));
  status.Set("errorLabel", LinkRoutingErrorLabel(shown_error));
  return status;
}

void AhoiSettingsHandler::PushLinkRoutingStatus() {
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  FireWebUIListener("ahoi-link-routing-changed",
                    base::Value(BuildLinkRoutingStatus({}, {})));
}

void AhoiSettingsHandler::HandleGetLinkRouting(const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args) ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  Profile* main_profile = LinkRoutingProfile();
  if (main_profile && main_profile == profile_.get() &&
      !link_routing_pref_registrar_.prefs()) {
    link_routing_pref_registrar_.Init(main_profile->GetPrefs());
    link_routing_pref_registrar_.Add(
        navigation::kLinkRoutingPref,
        base::BindRepeating(&AhoiSettingsHandler::PushLinkRoutingStatus,
                            base::Unretained(this)));
  }
  ResolveJavascriptCallback(args.front(),
                            base::Value(BuildLinkRoutingStatus({}, {})));
}

void AhoiSettingsHandler::HandleLinkRoutingAction(const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 3u || !args[1].is_string() || !args[2].is_dict()) {
    ResolveJavascriptCallback(args.front(), base::Value(BuildLinkRoutingStatus(
                                                "invalid", "invalidRequest")));
    return;
  }
  Profile* main_profile = LinkRoutingProfile();
  if (!main_profile) {
    ResolveJavascriptCallback(args.front(), base::Value(BuildLinkRoutingStatus(
                                                "blocked", "unavailable")));
    return;
  }
  if (!CanWriteRouting(main_profile)) {
    ResolveJavascriptCallback(args.front(), base::Value(BuildLinkRoutingStatus(
                                                "blocked", "managed")));
    return;
  }
  PrefService* prefs = main_profile->GetPrefs();
  navigation::RoutingSettings settings =
      navigation::ReadRoutingSettings(*prefs);
  const std::string error =
      ApplyLinkRoutingAction(settings, args[1].GetString(), args[2].GetDict(),
                             LinkRoutingWorkspaces());
  if (!error.empty()) {
    // Nothing is stored; the page shows the reason next to the editor.
    ResolveJavascriptCallback(
        args.front(), base::Value(BuildLinkRoutingStatus("invalid", error)));
    return;
  }
  const bool written = navigation::WriteRoutingSettings(prefs, settings);
  ResolveJavascriptCallback(args.front(),
                            base::Value(BuildLinkRoutingStatus(
                                written ? "saved" : "blocked",
                                written ? std::string_view() : "writeFailed")));
}

void AhoiSettingsHandler::HandleResolveLinkRoutingExample(
    const base::ListValue& args) {
  if (args.size() != 2u || !HasCallbackId(args) || !args[1].is_string() ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  Profile* main_profile = LinkRoutingProfile();
  const navigation::RoutingSettings settings =
      main_profile ? navigation::ReadRoutingSettings(*main_profile->GetPrefs())
                   : navigation::RoutingSettings();
  ResolveJavascriptCallback(
      args.front(),
      base::Value(DescribeLinkRoutingExample(settings, LinkRoutingWorkspaces(),
                                             args[1].GetString())));
}

}  // namespace ahoi::settings
