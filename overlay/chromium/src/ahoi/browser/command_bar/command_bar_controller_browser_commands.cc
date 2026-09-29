// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// CommandBarController's browser-command index: the fixed command table, the
// shortcut catalog with the current keys, and the Workspace move and merge
// targets. Split from command_bar_controller.cc to keep both files small.

#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/command_bar/command_bar_controller.h"
#include "ahoi/browser/command_bar/command_bar_shortcut_items.h"
#include "ahoi/browser/command_bar/command_execution_adapter_internal.h"
#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"

namespace ahoi {

void CommandBarController::OnShortcutBindingsChanged() {
  // Posted like the Workspace republish in OnCommandIndexChanged: never
  // ReplaceItems from inside another notification's call stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandBarController::PublishBrowserCommands,
                                weak_ptr_factory_.GetWeakPtr()));
}

void CommandBarController::PublishBrowserCommands() {
  if (!command_service_) {
    return;
  }
  struct BrowserCommandDefinition {
    const char* stable_id;
    int title_id;
    std::vector<std::u16string> keywords;
    int priority;
  };
  std::vector<BrowserCommandDefinition> definitions = {
      {"browser.reload",
       IDS_RELOAD_MENU_NORMAL_RELOAD_ITEM,
       {u"reload", u"neu laden"},
       260},
      {"browser.reload-bypassing-cache",
       IDS_RELOAD_MENU_HARD_RELOAD_ITEM,
       {u"hard reload", u"cache", u"hart neu laden"},
       255},
      {"browser.downloads",
       IDS_DOWNLOAD_HISTORY_TITLE,
       {u"downloads", u"herunterladen"},
       230},
      {"browser.history", IDS_HISTORY_MENU, {u"history", u"verlauf"}, 225},
      {"browser.settings",
       IDS_SETTINGS,
       {u"settings", u"preferences", u"einstellungen"},
       220},
      {"browser.clear-browsing-data",
       IDS_CLEAR_BROWSING_DATA,
       {u"clear cache", u"cookies", u"browserdaten löschen"},
       215},
      {"browser.devtools",
       IDS_DEV_TOOLS,
       {u"developer tools", u"inspect", u"entwicklertools"},
       210},
      {"browser.view-source",
       IDS_VIEW_SOURCE,
       {u"view source", u"quelltext"},
       205},
      {"browser.print", IDS_CONTENT_CONTEXT_PRINT, {u"print", u"drucken"}, 200},
      {"page.copy-link",
       IDS_AHOI_COPY_ACTIVE_PAGE_LINK,
       {u"copy link", u"link kopieren", u"active pane", u"aktives pane"},
       265},
      {"page.copy-markdown-link",
       IDS_AHOI_COPY_ACTIVE_PAGE_LINK_AS_MARKDOWN,
       {u"markdown link", u"link als markdown", u"active pane",
        u"aktives pane"},
       264},
      {"page.reading-mode",
       IDS_AHOI_OPEN_ACTIVE_PAGE_IN_READING_MODE,
       {u"reader", u"reading mode", u"lesemodus", u"active pane",
        u"aktives pane"},
       263},
      {"browser.new-window",
       IDS_NEW_WINDOW,
       {u"new window", u"neues fenster"},
       195},
      {"browser.new-incognito-window",
       IDS_NEW_INCOGNITO_WINDOW,
       {u"incognito", u"private", u"inkognito"},
       190},
      {"browser.open-in-normal-window",
       IDS_AHOI_COMMAND_OPEN_IN_NORMAL_WINDOW,
       {u"open in normal window", u"quick window",
        u"in normales fenster übernehmen"},
       265},
      // The verified uBO Classic installer is otherwise only reachable
      // through the app menu's Extensions submenu; Chromium keeps the
      // command disabled when the build does not ship it.
      {"extensions.ubo-classic",
       IDS_AHOI_UBO_MENU,
       {u"ublock origin", u"ubo", u"adblock", u"werbeblocker"},
       185},
      {"privacy.open",
       IDS_AHOI_PRIVACY_OPEN_COMMAND,
       {u"privacy open", u"privacy", u"tracking", u"datenschutz",
        u"website-schutz"},
       255},
      {"http-auth.switch",
       IDS_AHOI_HTTP_AUTH_SWITCH_ACCOUNT_COMMAND,
       {u"http auth switch account", u"basic auth", u"htaccess",
        u"http-zugang wechseln", u"konto wechseln"},
       254},
      {"http-auth.forget",
       IDS_AHOI_HTTP_AUTH_FORGET_REALM_COMMAND,
       {u"http auth forget", u"basic auth", u"htaccess",
        u"http-zugang vergessen", u"schutzbereich vergessen"},
       253},
      {"http-auth.manage",
       IDS_AHOI_HTTP_AUTH_MANAGE_COMMAND,
       {u"http auth manage", u"basic auth", u"digest", u"htaccess",
        u"http-zugänge verwalten", u"passwörter und authentifizierung"},
       252},
      {"developer.clear-site-cache",
       IDS_AHOI_DEVELOPER_CLEAR_SITE_CACHE,
       {u"site cache", u"clear current cache", u"website cache leeren"},
       250},
      {"developer.toggle-css",
       IDS_AHOI_DEVELOPER_TOGGLE_CSS,
       {u"css", u"styles", u"stylesheets", u"css deaktivieren"},
       245},
      {"developer.reveal-passwords",
       IDS_AHOI_DEVELOPER_TOGGLE_PASSWORD_FIELDS,
       {u"password", u"passwort", u"show password", u"passwort anzeigen"},
       240},
      {"developer.toggle-javascript",
       IDS_AHOI_DEVELOPER_TOGGLE_JAVASCRIPT,
       {u"javascript", u"js", u"disable javascript"},
       235},
      {"developer.toggle-images",
       IDS_AHOI_DEVELOPER_TOGGLE_IMAGES,
       {u"images", u"bilder", u"disable images"},
       230},
      {"developer.reset-page",
       IDS_AHOI_DEVELOPER_RESET_DOCUMENT,
       {u"reset page", u"reset css", u"seite zurücksetzen"},
       225},
      {"developer.screenshot-visible",
       IDS_AHOI_DEVELOPER_SCREENSHOT_VISIBLE,
       {u"screenshot", u"viewport", u"sichtbarer bereich"},
       220},
      {"developer.screenshot-full-page",
       IDS_AHOI_DEVELOPER_SCREENSHOT_FULL_PAGE,
       {u"full page screenshot", u"ganze seite", u"vollseite"},
       215},
  };

  std::vector<CommandItem> commands;
  commands.reserve(definitions.size());
  for (BrowserCommandDefinition& definition : definitions) {
    commands.push_back({
        .type = CommandItemType::kBrowserCommand,
        .stable_id = definition.stable_id,
        .title = l10n_util::GetStringUTF16(definition.title_id),
        .keywords = std::move(definition.keywords),
        .priority = definition.priority,
    });
  }
  // The shared shortcut catalog: every rebindable command with its current
  // key, so the command bar and the keys run the same thing. Republished
  // when the bindings pref changes (OnShortcutBindingsChanged).
  const shortcuts::Overrides overrides =
      browser_ && browser_->GetProfile()
          ? shortcuts::ReadOverrides(*browser_->GetProfile()->GetPrefs())
          : shortcuts::Overrides();
  for (CommandItem& item : internal::BuildShortcutCommandItems(overrides)) {
    commands.push_back(std::move(item));
  }
  // ADR 0012 section 2: one "In Workspace verschieben" item per Workspace of
  // this Profile, the same targets as the sidebar's "Move to". The window's
  // own Workspace is refused at execution (CanMoveToWorkspace).
  if (browser_ && browser_->GetProfile() &&
      !browser_->GetProfile()->IsOffTheRecord()) {
    SessionBridge* bridge =
        SessionBridgeFactory::GetForProfile(browser_->GetProfile());
    std::vector<tab_tree::Workspace> workspaces;
    if (bridge && bridge->is_ready() &&
        bridge->tab_tree_store()->GetWorkspaces(&workspaces) ==
            tab_tree::TabTreeStore::Result::kOk) {
      std::vector<internal::MoveToWorkspaceTarget> targets;
      targets.reserve(workspaces.size());
      for (const tab_tree::Workspace& workspace : workspaces) {
        targets.push_back({.id = workspace.id, .name = workspace.name});
      }
      // WS-ISO-05: other Profiles' Workspaces reopen the item there.
      for (const auto& other :
           session::ListOtherProfileWorkspaces(browser_->GetProfile())) {
        targets.push_back({.id = other.workspace_id,
                           .name = other.name,
                           .separate_sign_ins = true});
      }
      const bool german = base::i18n::GetConfiguredLocale().starts_with("de");
      for (CommandItem& item :
           internal::BuildMoveToWorkspaceCommands(targets, german)) {
        commands.push_back(std::move(item));
      }
      // ADR 0012 section 1: "Zusammenführen mit …" for the same targets. The
      // source is the window's shown Workspace; the sidebar refuses it as its
      // own target (CanMergeWorkspaceInto) and opens the menu's dialog.
      for (CommandItem& item :
           internal::BuildMergeWorkspaceCommands(targets, german)) {
        commands.push_back(std::move(item));
      }
    }
  }
  CHECK(command_service_->ReplaceItems(CommandItemType::kBrowserCommand,
                                       std::move(commands)));
}

}  // namespace ahoi
