// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-18: the audio of windows a hand-over hid, for the shared
// switcher in the Workspace menu.

#include "ahoi/browser/ui/sidebar/other_profile_media_chromium.h"

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/other_profile_media.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"

namespace ahoi::sidebar {

namespace {

Profile* LoadedProfileOfEntry(const std::string& profile_dir) {
  if (profile_dir.empty()) {
    return session::GetLoadedMainProfile();
  }
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  return manager ? session::FindLoadedProfile(
                       manager->user_data_dir().AppendASCII(profile_dir))
                 : nullptr;
}

struct Page {
  raw_ptr<content::WebContents> contents;
  PageAudioState state;
};

// Every page of `profile`'s normal windows, hidden ones included.
std::vector<Page> CollectPages(Profile* profile) {
  std::vector<Page> pages;
  ProfileBrowserCollection* browsers =
      profile ? ProfileBrowserCollection::GetForProfile(profile) : nullptr;
  if (!browsers) {
    return pages;
  }
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile);
  browsers->ForEach(
      [&pages, bridge](BrowserWindowInterface* browser) {
        TabStripModel* model = browser->GetTabStripModel();
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            browser->IsDeleteScheduled() || !model) {
          return true;
        }
        for (int index = 0; index < model->count(); ++index) {
          tabs::TabInterface* tab = model->GetTabAtIndex(index);
          content::WebContents* contents =
              tab ? tab->GetContents() : nullptr;
          if (!contents) {
            continue;
          }
          pages.push_back(
              {.contents = contents,
               .state = {.workspace_id =
                             bridge ? bridge->GetWorkspaceForTab(tab)
                                    : std::nullopt,
                         .audible = contents->IsCurrentlyAudible()}});
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return pages;
}

}  // namespace

bool OtherProfileWorkspacePlaysAudio(const std::string& profile_dir,
                                     const base::Uuid& workspace_id) {
  std::vector<PageAudioState> states;
  for (const Page& page : CollectPages(LoadedProfileOfEntry(profile_dir))) {
    states.push_back(page.state);
  }
  return SwitcherEntryPlaysAudio(states, !profile_dir.empty(), workspace_id);
}

size_t PauseOtherProfileWorkspaceMedia(const std::string& profile_dir,
                                       const base::Uuid& workspace_id) {
  size_t reached = 0;
  for (const Page& page : CollectPages(LoadedProfileOfEntry(profile_dir))) {
    if (!page.state.audible ||
        !PageBelongsToSwitcherEntry(page.state, !profile_dir.empty(),
                                    workspace_id)) {
      continue;
    }
    content::MediaSession::Get(page.contents)
        ->Suspend(content::MediaSession::SuspendType::kUI);
    // Sound without a media session (Web Audio, for example) cannot be
    // suspended; muting stops it without touching the page.
    if (page.contents->IsCurrentlyAudible()) {
      SetTabAudioMuted(page.contents, true, TabMutedReason::kAudioIndicator,
                       /*extension_id=*/std::string());
    }
    ++reached;
  }
  return reached;
}

bool BrowserSidebarHostView::RunOtherProfileMediaCommand(int command_id) {
  if (command_id < kPauseOtherProfileMediaCommandBase ||
      command_id >= kMergeWorkspaceCommandBase) {
    return false;
  }
  const size_t index =
      static_cast<size_t>(command_id - kPauseOtherProfileMediaCommandBase);
  if (context_.scope == ContextMenuScope::kWorkspace &&
      index < context_.media_pause_targets.size()) {
    const session::DirectoryWorkspace& target =
        context_.media_pause_targets[index];
    PauseOtherProfileWorkspaceMedia(target.profile_dir, target.workspace_id);
  }
  return true;
}

}  // namespace ahoi::sidebar
