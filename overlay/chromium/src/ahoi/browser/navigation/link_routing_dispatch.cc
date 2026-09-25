// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing_dispatch.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/command_bar/quick_window.h"
#include "ahoi/browser/navigation/link_routing_editor.h"
#include "ahoi/browser/navigation/link_routing_target_chooser.h"
#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"

namespace ahoi::navigation {

namespace {

std::string DirName(const Profile* profile) {
  return profile->GetPath().BaseName().AsUTF8Unsafe();
}

bool IsRoutingProfile(Profile* profile, Profile* main_profile) {
  return profile && profile->IsRegularProfile() && !profile->IsOffTheRecord() &&
         (profile == main_profile ||
          session::IsIsolatedWorkspaceProfile(profile));
}

// The most recently activated visible normal window of the main Profile or
// of a fully separated Workspace; windows hidden by a hand-over are skipped.
BrowserWindowInterface* FindLastActiveWindow(Profile* main_profile) {
  BrowserWindowInterface* match = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&match, main_profile](BrowserWindowInterface* browser) {
        ui::BaseWindow* window = browser->GetWindow();
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            browser->IsDeleteScheduled() || !window || !window->IsVisible() ||
            !IsRoutingProfile(browser->GetProfile(), main_profile)) {
          return true;
        }
        match = browser;
        return false;
      },
      BrowserCollection::Order::kActivation);
  return match;
}

gfx::Rect AnchorBounds(BrowserWindowInterface* source) {
  if (source && source->GetWindow()) {
    return source->GetWindow()->GetBounds();
  }
  display::Screen* screen = display::Screen::Get();
  return screen ? screen->GetPrimaryDisplay().work_area() : gfx::Rect();
}

void OpenInWindow(GURL url,
                  std::optional<base::Uuid> workspace_id,
                  ExternalUrlFallback fallback,
                  BrowserWindowInterface* window) {
  if (!window || window->IsDeleteScheduled() ||
      window->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      window->GetProfile()->IsOffTheRecord()) {
    LOG(WARNING) << "Link routing found no target window; default open.";
    fallback.Run({std::move(url)});
    return;
  }
  if (workspace_id) {
    // The Workspace is selected before the tab exists, so the new
    // WebContents is created with that Workspace's website session.
    SessionBridge* bridge =
        SessionBridgeFactory::GetForProfile(window->GetProfile());
    if (!bridge ||
        !bridge->SetActiveWorkspaceForWindow(
            window, *workspace_id, WorkspaceActivationSource::kRouting)) {
      LOG(WARNING) << "Link routing could not activate its target Workspace; "
                      "opening in the window's active Workspace.";
    }
  }
  window->OpenGURL(url, WindowOpenDisposition::NEW_FOREGROUND_TAB);
  if (ui::BaseWindow* base_window = window->GetWindow()) {
    base_window->Show();
    base_window->Activate();
  }
}

// Same window contract as quick_window::CreateAndShowQuickWindow(), but the
// popup's only tab is created directly with `url` instead of about:blank and
// the Command Bar, so the first navigation already is the routed one.
void OpenQuickWindow(GURL url,
                     gfx::Rect anchor_bounds,
                     ExternalUrlFallback fallback,
                     Profile* profile) {
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord()) {
    fallback.Run({std::move(url)});
    return;
  }
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, profile,
                                   /*from_user_gesture=*/true);
  params.is_trusted_source = true;
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  params.initial_bounds =
      quick_window::CalculateQuickWindowBounds(anchor_bounds);
  params.initial_origin_specified =
      BrowserWindowCreateParams::ValueSpecified::kSpecified;
  params.user_title = l10n_util::GetStringUTF8(IDS_AHOI_QUICK_WINDOW_TITLE);
  BrowserWindowInterface* const quick_window =
      CreateBrowserWindow(std::move(params));
  if (!quick_window) {
    fallback.Run({std::move(url)});
    return;
  }
  // An empty popup accepts the new tab itself (browser_navigator's
  // WindowCanOpenTabs), so nothing is redirected to a normal window.
  quick_window->OpenGURL(url, WindowOpenDisposition::NEW_FOREGROUND_TAB);
  if (ui::BaseWindow* window = quick_window->GetWindow()) {
    window->Show();
    window->Activate();
  }
}

bool HasOwnWebsiteSessions(Profile* profile,
                           BrowserWindowInterface* window,
                           const std::optional<base::Uuid>& workspace_id) {
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile);
  if (!bridge) {
    return false;
  }
  std::optional<base::Uuid> id = workspace_id;
  if (!id && window) {
    id = bridge->GetActiveWorkspaceForWindow(window);
  }
  return id && bridge->HasOwnWebsiteSessions(*id);
}

std::set<base::Uuid> MainWorkspaceIds(Profile* main_profile) {
  std::set<base::Uuid> ids;
  if (WorkspaceService* service =
          WorkspaceServiceFactory::GetForProfile(main_profile)) {
    for (const tab_tree::Workspace& workspace : service->ordered_workspaces()) {
      ids.insert(workspace.id);
    }
  }
  return ids;
}

void OpenLastActive(const GURL& url,
                    LinkOpenMode mode,
                    Profile* main_profile,
                    const ExternalUrlFallback& fallback) {
  BrowserWindowInterface* window = FindLastActiveWindow(main_profile);
  Profile* profile = window ? window->GetProfile() : main_profile;
  if (mode == LinkOpenMode::kQuickWindow &&
      !HasOwnWebsiteSessions(profile, window, std::nullopt)) {
    OpenQuickWindow(url, AnchorBounds(window), fallback, profile);
    return;
  }
  if (window) {
    OpenInWindow(url, std::nullopt, fallback, window);
    return;
  }
  session::PresentProfileWindow(
      main_profile, nullptr,
      base::BindOnce(&OpenInWindow, url, std::optional<base::Uuid>(),
                     fallback));
}

void OpenInMainWorkspace(const GURL& url,
                         const base::Uuid& workspace_id,
                         LinkOpenMode mode,
                         Profile* main_profile,
                         const ExternalUrlFallback& fallback) {
  BrowserWindowInterface* source = FindLastActiveWindow(main_profile);
  if (mode == LinkOpenMode::kQuickWindow) {
    // A Quick Window has no Workspace binding, so it can only carry the
    // Profile's shared website session.
    if (!HasOwnWebsiteSessions(main_profile, nullptr, workspace_id)) {
      OpenQuickWindow(url, AnchorBounds(source), fallback, main_profile);
      return;
    }
    LOG(WARNING) << "Link routing opens a normal tab: a Quick Window cannot "
                    "use the Workspace's own website session.";
  }
  if (source && source->GetProfile() == main_profile) {
    OpenInWindow(url, workspace_id, fallback, source);
    return;
  }
  session::PresentProfileWindow(
      main_profile, source,
      base::BindOnce(&OpenInWindow, url, workspace_id, fallback));
}

void OpenInIsolatedWorkspace(const GURL& url,
                             const session::IsolatedProfileEntry& entry,
                             LinkOpenMode mode,
                             Profile* main_profile,
                             const ExternalUrlFallback& fallback) {
  BrowserWindowInterface* source = FindLastActiveWindow(main_profile);
  if (mode == LinkOpenMode::kQuickWindow) {
    ProfileManager* manager = g_browser_process->profile_manager();
    if (!manager) {
      fallback.Run({url});
      return;
    }
    manager->CreateProfileAsync(
        manager->user_data_dir().AppendASCII(entry.profile_dir),
        base::BindOnce(&OpenQuickWindow, url, AnchorBounds(source), fallback));
    return;
  }
  if (source && DirName(source->GetProfile()) == entry.profile_dir) {
    OpenInWindow(url, entry.workspace_id, fallback, source);
    return;
  }
  session::PresentIsolatedWorkspace(
      entry.profile_dir, source,
      base::BindOnce(&OpenInWindow, url,
                     std::optional<base::Uuid>(entry.workspace_id), fallback));
}

// Opens `url` in `target` (nullopt: the last active Workspace). A target that
// is neither a main Workspace nor an openable fully separated Workspace falls
// back to the last active Workspace.
void OpenRouted(const GURL& url,
                const std::optional<base::Uuid>& target,
                LinkOpenMode mode,
                Profile* main_profile,
                const ExternalUrlFallback& fallback) {
  if (!target) {
    OpenLastActive(url, mode, main_profile, fallback);
    return;
  }
  if (MainWorkspaceIds(main_profile).contains(*target)) {
    OpenInMainWorkspace(url, *target, mode, main_profile, fallback);
    return;
  }
  std::optional<session::IsolatedProfileEntry> entry =
      session::FindIsolatedProfileByWorkspaceId(
          g_browser_process->local_state(), *target);
  if (entry && entry->state != session::IsolatedProfileState::kDeleting) {
    OpenInIsolatedWorkspace(url, *entry, mode, main_profile, fallback);
    return;
  }
  OpenLastActive(url, mode, main_profile, fallback);
}

// The main Profile's Workspaces and the openable fully separated ones, in
// the process-wide order (ADR 0011 step 2, handoff 054). Only logical
// Workspace ids leave this function.
std::vector<LinkRoutingTargetOption> TargetOptions(Profile* main_profile) {
  std::vector<session::DirectoryWorkspace> main_keys;
  std::map<base::Uuid, LinkRoutingTargetOption> by_id;
  if (WorkspaceService* service =
          WorkspaceServiceFactory::GetForProfile(main_profile)) {
    for (const tab_tree::Workspace& workspace : service->ordered_workspaces()) {
      main_keys.push_back(
          {.workspace_id = workspace.id, .sort_key = workspace.sort_key});
      by_id.emplace(workspace.id,
                    LinkRoutingTargetOption{.workspace_id = workspace.id,
                                            .name = workspace.name});
    }
  }
  const std::vector<session::IsolatedProfileEntry> isolated =
      session::GetOpenableIsolatedWorkspaces();
  for (const session::IsolatedProfileEntry& entry : isolated) {
    by_id.emplace(entry.workspace_id,
                  LinkRoutingTargetOption{.workspace_id = entry.workspace_id,
                                          .name = entry.name,
                                          .separated = true});
  }
  std::vector<LinkRoutingTargetOption> options;
  for (const session::DirectoryWorkspace& key :
       session::OrderDirectoryWorkspaces(main_keys, isolated)) {
    if (auto it = by_id.find(key.workspace_id); it != by_id.end()) {
      options.push_back(it->second);
    }
  }
  return options;
}

// The user's explicit answer to the target chooser. Only a checked
// "Für diese Website merken" writes a rule; nothing is learned implicitly.
void OnTargetChosen(GURL url,
                    LinkOpenMode mode,
                    ExternalUrlFallback fallback,
                    std::optional<LinkRoutingTargetChoice> choice) {
  if (!choice) {
    LOG(WARNING) << "Link routing target choice was cancelled; the link is "
                    "not opened.";
    return;
  }
  Profile* main_profile = session::GetLoadedMainProfile();
  if (!main_profile) {
    fallback.Run({std::move(url)});
    return;
  }
  if (choice->remember) {
    PrefService* prefs = main_profile->GetPrefs();
    RoutingSettings settings = ReadRoutingSettings(*prefs);
    if (!RememberSiteChoice(settings, url, choice->workspace_id, mode,
                            base::Uuid::GenerateRandomV4()) ||
        !WriteRoutingSettings(prefs, settings)) {
      LOG(WARNING) << "Link routing could not remember the chosen Workspace "
                      "for this website.";
    }
  }
  OpenRouted(url, choice->workspace_id, mode, main_profile, fallback);
}

// Asks for a new target over `window` because the winning explicit target no
// longer exists. Without a chooser the last active Workspace receives the link.
void ShowTargetChooser(GURL url,
                       LinkOpenMode mode,
                       ExternalUrlFallback fallback,
                       BrowserWindowInterface* window) {
  Profile* main_profile = session::GetLoadedMainProfile();
  if (!main_profile) {
    fallback.Run({std::move(url)});
    return;
  }
  gfx::NativeWindow parent;
  if (window && !window->IsDeleteScheduled() && window->GetWindow()) {
    window->GetWindow()->Show();
    window->GetWindow()->Activate();
    parent = window->GetWindow()->GetNativeWindow();
  }
  if (!ShowLinkRoutingTargetChooser(
          parent, url, TargetOptions(main_profile),
          base::BindOnce(&OnTargetChosen, url, mode, fallback))) {
    LOG(WARNING) << "Link routing target Workspace is no longer available and "
                    "no chooser could be shown; using the last active "
                    "Workspace.";
    OpenLastActive(url, mode, main_profile, fallback);
  }
}

void Dispatch(Profile* main_profile,
              const RoutingSettings& settings,
              const std::vector<GURL>& urls,
              const ExternalUrlFallback& fallback) {
  std::set<base::Uuid> available = MainWorkspaceIds(main_profile);
  for (const session::IsolatedProfileEntry& entry :
       session::GetOpenableIsolatedWorkspaces()) {
    available.insert(entry.workspace_id);
  }

  for (const GURL& url : urls) {
    const RouteResult route = ResolveRoute(settings, url, available);
    if (route.disposition == RouteDisposition::kNeedsTargetChoice) {
      // Verständliche Zielauswahl: the explicit target is gone, so the user
      // picks a Workspace (optionally remembered for this website).
      if (BrowserWindowInterface* window = FindLastActiveWindow(main_profile)) {
        ShowTargetChooser(url, route.mode, fallback, window);
      } else {
        session::PresentProfileWindow(
            main_profile, nullptr,
            base::BindOnce(&ShowTargetChooser, url, route.mode, fallback));
      }
      continue;
    }
    OpenRouted(url, route.target_workspace_id, route.mode, main_profile,
               fallback);
  }
}

}  // namespace

bool RouteExternalUrls(const std::vector<GURL>& urls,
                       ExternalUrlFallback fallback) {
  std::vector<GURL> routable = NormalizeAndDedupUrls(urls);
  if (routable.empty() || !g_browser_process ||
      !g_browser_process->profile_manager() || !fallback) {
    return false;
  }
  if (Profile* main_profile = session::GetLoadedMainProfile()) {
    const RoutingSettings settings =
        ReadRoutingSettings(*main_profile->GetPrefs());
    if (!settings.enabled) {
      return false;
    }
    Dispatch(main_profile, settings, routable, fallback);
    return true;
  }
  session::LoadMainProfile(base::BindOnce(
      [](std::vector<GURL> urls, ExternalUrlFallback fallback,
         Profile* main_profile) {
        if (!main_profile) {
          fallback.Run(std::move(urls));
          return;
        }
        const RoutingSettings settings =
            ReadRoutingSettings(*main_profile->GetPrefs());
        if (!settings.enabled) {
          fallback.Run(std::move(urls));
          return;
        }
        Dispatch(main_profile, settings, urls, fallback);
      },
      std::move(routable), std::move(fallback)));
  return true;
}

}  // namespace ahoi::navigation
