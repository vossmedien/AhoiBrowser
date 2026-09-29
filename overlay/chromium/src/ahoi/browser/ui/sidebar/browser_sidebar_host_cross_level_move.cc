// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-05: moving a tab, folder or split to a Workspace of
// another Profile ("Vollständig getrennt"), from "Move to", the command bar
// or a drop into that Workspace's window. One calm confirmation names what
// moves and that sign-ins stay behind; the pages then reopen by URL in the
// target, and Cmd+Z in either window brings the item back.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/session/cross_level_move.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

namespace {

constexpr size_t kMaximumCrossLevelTargets = 99;

std::u16string Quoted(bool german, const std::u16string& name) {
  return german ? base::StrCat({u"„", name, u"“"})
                : base::StrCat({u"“", name, u"”"});
}

std::u16string PageCount(bool german, size_t pages) {
  const std::u16string count = base::NumberToString16(pages);
  if (german) {
    return base::StrCat({count, pages == 1 ? u" Seite" : u" Seiten"});
  }
  return base::StrCat({count, pages == 1 ? u" page" : u" pages"});
}

// What moves, in one sentence: a split, a folder with its pages, or pages.
std::u16string DescribeMove(bool german,
                            const session::CrossLevelMovePayload& payload,
                            const std::u16string& target) {
  const std::u16string to = Quoted(german, target);
  const tab_tree::PortableWorkspaceNode& first = payload.nodes.front();
  if (!payload.splits.empty() && payload.folders == 0) {
    return german ? base::StrCat({u"Der Split mit ",
                                  PageCount(true, payload.pages),
                                  u" zieht als Ganzes nach ", to, u" um."})
                  : base::StrCat({u"The split with ",
                                  PageCount(false, payload.pages),
                                  u" moves to ", to, u" as a whole."});
  }
  if (payload.root_ids.size() == 1 &&
      first.type == tab_tree::TreeNodeType::kFolder) {
    return german ? base::StrCat({u"Der Ordner ", Quoted(true, first.title),
                                  u" mit ", PageCount(true, payload.pages),
                                  u" zieht nach ", to, u" um."})
                  : base::StrCat({u"The folder ", Quoted(false, first.title),
                                  u" with ", PageCount(false, payload.pages),
                                  u" moves to ", to, u"."});
  }
  if (payload.pages == 1) {
    return german ? base::StrCat({Quoted(true, first.title), u" zieht nach ",
                                  to, u" um."})
                  : base::StrCat({Quoted(false, first.title), u" moves to ",
                                  to, u"."});
  }
  return german ? base::StrCat({PageCount(true, payload.pages),
                                u" ziehen nach ", to, u" um."})
                : base::StrCat({PageCount(false, payload.pages),
                                u" move to ", to, u"."});
}

bool IsGerman() {
  return base::StartsWith(base::i18n::GetConfiguredLocale(), "de",
                          base::CompareCase::INSENSITIVE_ASCII);
}

}  // namespace

std::vector<SwitcherWorkspace> BrowserSidebarHostView::CrossLevelTargets()
    const {
  std::vector<SwitcherWorkspace> targets;
  for (SwitcherWorkspace& workspace : SwitcherWorkspaces()) {
    if (!workspace.own && targets.size() < kMaximumCrossLevelTargets) {
      targets.push_back(std::move(workspace));
    }
  }
  return targets;
}

std::vector<base::Uuid> BrowserSidebarHostView::CrossLevelRootsForTab(
    tabs::TabInterface* tab) {
  std::vector<base::Uuid> roots;
  if (!tab || !tab_strip_model_) {
    return roots;
  }
  if (const std::optional<base::Uuid> saved =
          session_bridge_->FindTreeNodeIdForTab(tab)) {
    // A saved page brings its split group, as in the same-Profile move.
    return GetMoveGroupNodeIds(*saved);
  }
  const auto split = tab->GetSplit();
  for (tabs::TabInterface* member : *tab_strip_model_) {
    if (member == tab || (split && member && member->GetSplit() == split)) {
      if (const std::optional<base::Uuid> id =
              session_bridge_->FindSharedTreeNodeIdForTab(member)) {
        roots.push_back(*id);
      }
    }
  }
  return roots;
}

bool BrowserSidebarHostView::AppendCrossLevelMoveItems(
    std::vector<base::Uuid> roots,
    bool has_menu) {
  context_.cross_level_targets = CrossLevelTargets();
  context_.cross_level_roots = std::move(roots);
  if (context_.cross_level_targets.empty() ||
      context_.cross_level_roots.empty()) {
    context_.cross_level_targets.clear();
    return has_menu;
  }
  if (!context_move_menu_model_) {
    context_move_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  } else if (context_move_menu_model_->GetItemCount() > 0) {
    context_move_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  // The other Profiles' Workspaces keep their own sign-ins; the header says
  // so before the user picks one, the confirmation says it in full.
  context_move_menu_model_->AddTitle(
      StructureText(u"Getrennte Anmeldungen", u"Separate sign-ins"));
  for (size_t index = 0; index < context_.cross_level_targets.size();
       ++index) {
    const SwitcherWorkspace& target = context_.cross_level_targets[index];
    context_move_menu_model_->AddItem(
        kCrossLevelMoveCommandBase + static_cast<int>(index),
        target.icon.empty() ? target.name
                            : base::StrCat({target.icon, u"  ", target.name}));
  }
  return true;
}

bool BrowserSidebarHostView::RunCrossLevelMoveCommand(int command_id) {
  if (command_id < kCrossLevelMoveCommandBase ||
      command_id >= kMoveToDestinationCommandBase) {
    return false;
  }
  const size_t index =
      static_cast<size_t>(command_id - kCrossLevelMoveCommandBase);
  if ((context_.scope != ContextMenuScope::kTree &&
       context_.scope != ContextMenuScope::kOpenTab) ||
      index >= context_.cross_level_targets.size()) {
    return true;
  }
  const std::vector<base::Uuid> roots = context_.cross_level_roots;
  tabs::TabInterface* const active = tab_strip_model_->GetActiveTab();
  const bool follow =
      active && (context_.runtime_tab.get() == active ||
                 std::ranges::any_of(roots, [this, active](const auto& id) {
                   return session_bridge_->FindTabByTreeNodeId(id) == active;
                 }));
  // Leave the nested menu loop before a dialog opens.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             std::vector<base::Uuid> roots, SwitcherWorkspace target,
             bool follow) {
            if (host) {
              host->RequestCrossLevelMove(std::move(roots), target, follow,
                                          nullptr);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), roots,
          context_.cross_level_targets[index], follow));
  return true;
}

void BrowserSidebarHostView::RequestCrossLevelMove(
    std::vector<base::Uuid> roots,
    const SwitcherWorkspace& target,
    bool follow,
    BrowserSidebarHostView* presenter) {
  BrowserSidebarHostView* const shown = presenter ? presenter : this;
  session::CrossLevelMovePayload payload;
  switch (session_bridge_->CheckCrossLevelMove(roots, &payload)) {
    case session::CrossLevelMoveCheck::kOk:
      break;
    case session::CrossLevelMoveCheck::kSplitWouldMix:
      shown->ShowStructureNotice(
          StructureText(u"Der Split bleibt zusammen",
                        u"The split stays together"),
          StructureText(
              u"Ein Split kann nicht auf zwei vollständig getrennte "
              u"Workspaces verteilt werden. Verschiebe den ganzen Split "
              u"oder löse ihn zuerst.",
              u"A split can't be spread across two fully separate "
              u"Workspaces. Move the whole split, or separate it first."));
      return;
    case session::CrossLevelMoveCheck::kLocalOnlyPage:
      shown->ShowStructureNotice(
          StructureText(u"Nicht verschiebbar", u"Can't be moved"),
          StructureText(
              u"Darunter ist eine Seite, die sich nur hier öffnen lässt, "
              u"etwa eine Browserseite, eine Datei oder ein leerer Tab. "
              u"Verschiebe die übrigen Seiten einzeln.",
              u"It includes a page that only opens here, such as a browser "
              u"page, a file or an empty tab. Move the other pages one by "
              u"one."));
      return;
    case session::CrossLevelMoveCheck::kNothingToMove:
      return;
  }
  if (!shown->GetWidget() || shown->structure_dialog_widget_) {
    return;
  }
  const bool german = IsGerman();
  CrossLevelMoveRequest request;
  request.root_ids = std::move(roots);
  request.target = target;
  request.follow = follow;
  if (presenter) {
    request.presenter = presenter->weak_ptr_factory_.GetWeakPtr();
  }
  auto dialog =
      ui::DialogModel::Builder()
          .SetTitle(german
                        ? base::StrCat({u"Nach ", Quoted(true, target.name),
                                        u" verschieben?"})
                        : base::StrCat({u"Move to ",
                                        Quoted(false, target.name), u"?"}))
          .AddParagraph(ui::DialogModelLabel(base::StrCat(
              {DescribeMove(german, payload, target.name),
               StructureText(u" Geöffnete Seiten laden dort neu.",
                             u" Open pages reload there.")})))
          .AddParagraph(ui::DialogModelLabel(StructureText(
              u"Anmeldungen ziehen nicht mit: Cookies, Logins und "
              u"Website-Daten bleiben hier, weil die beiden Workspaces "
              u"vollständig getrennt sind. ⌘Z holt alles zurück.",
              u"Sign-ins don't move along: cookies, logins and website data "
              u"stay here, because the two Workspaces are kept fully "
              u"separate. ⌘Z brings everything back.")))
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<BrowserSidebarHostView> source,
                     CrossLevelMoveRequest request) {
                    base::SingleThreadTaskRunner::GetCurrentDefault()
                        ->PostTask(
                            FROM_HERE,
                            base::BindOnce(
                                &BrowserSidebarHostView::StartCrossLevelMove,
                                source, std::move(request)));
                  },
                  weak_ptr_factory_.GetWeakPtr(), request),
              ui::DialogModel::Button::Params().SetLabel(
                  StructureText(u"Verschieben", u"Move")))
          .AddCancelButton(base::DoNothing(),
                           ui::DialogModel::Button::Params().SetLabel(
                               StructureText(u"Abbrechen", u"Cancel")))
          .Build();
  auto delegate = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), shown->workspace_button_,
      views::BubbleBorder::TOP_LEFT);
  delegate->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
  shown->structure_dialog_widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(),
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&BrowserSidebarHostView::OnStructureDialogClosed,
                         shown->weak_ptr_factory_.GetWeakPtr())));
  if (shown->structure_dialog_widget_) {
    delegate.release();
    shown->structure_dialog_widget_->Show();
  }
}

void BrowserSidebarHostView::StartCrossLevelMove(
    CrossLevelMoveRequest request) {
  const std::vector<base::Uuid> roots = request.root_ids;
  // The item's open pages answer first; a veto changes nothing anywhere.
  session_bridge_->AskCrossLevelMovePages(
      roots,
      base::BindOnce(&BrowserSidebarHostView::OnCrossLevelPagesAnswered,
                     weak_ptr_factory_.GetWeakPtr(), std::move(request)));
}

void BrowserSidebarHostView::OnCrossLevelPagesAnswered(
    CrossLevelMoveRequest request,
    std::optional<SessionBridge::CrossLevelOpenPages> open) {
  if (!open) {
    return;
  }
  session::CrossLevelMovePayload payload;
  if (session_bridge_->CheckCrossLevelMove(request.root_ids, &payload) !=
      session::CrossLevelMoveCheck::kOk) {
    std::ignore =
        session_bridge_->FinishCrossLevelMoveSource(request.root_ids, false);
    ShowCrossLevelFailure(request.target.name);
    return;
  }
  const std::string profile_dir = request.target.key.profile_dir;
  session::LoadWorkspaceProfile(
      profile_dir,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             CrossLevelMoveRequest request,
             session::CrossLevelMovePayload payload,
             SessionBridge::CrossLevelOpenPages open, Profile* profile) {
            if (!host) {
              return;
            }
            SessionBridge* target =
                profile && profile != host->browser_->GetProfile()
                    ? SessionBridgeFactory::GetForProfile(profile)
                    : nullptr;
            if (!target) {
              std::ignore = host->session_bridge_->FinishCrossLevelMoveSource(
                  request.root_ids, false);
              host->ShowCrossLevelFailure(request.target.name);
              return;
            }
            const base::FilePath target_path = profile->GetPath();
            const base::Uuid workspace_id = request.target.key.workspace_id;
            target->ImportCrossLevelMove(
                std::move(payload), workspace_id,
                base::BindOnce(
                    [](base::WeakPtr<BrowserSidebarHostView> host,
                       CrossLevelMoveRequest request,
                       SessionBridge::CrossLevelOpenPages open,
                       base::FilePath target_path,
                       std::optional<session::CrossLevelMovePlacement>
                           placement) {
                      if (host) {
                        host->FinishCrossLevelMove(std::move(request),
                                                   std::move(open),
                                                   target_path,
                                                   std::move(placement));
                      }
                    },
                    host, std::move(request), std::move(open), target_path));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(request),
          std::move(payload), std::move(*open)));
}

void BrowserSidebarHostView::FinishCrossLevelMove(
    CrossLevelMoveRequest request,
    SessionBridge::CrossLevelOpenPages open,
    const base::FilePath& target_path,
    std::optional<session::CrossLevelMovePlacement> placement) {
  if (!placement) {
    std::ignore =
        session_bridge_->FinishCrossLevelMoveSource(request.root_ids, false);
    ShowCrossLevelFailure(request.target.name);
    return;
  }
  session::CrossLevelMoveReceipt receipt;
  receipt.source_deletion_subject =
      session_bridge_->FinishCrossLevelMoveSource(request.root_ids, true);
  receipt.source_profile_path = browser_->GetProfile()->GetPath();
  receipt.target_profile_path = target_path;
  receipt.source_workspace_id =
      controller_->view_model().workspace_id().value_or(base::Uuid());
  receipt.target_workspace_id = request.target.key.workspace_id;
  receipt.source_open_ids = open.saved_ids;
  receipt.source_temporary_urls = open.temporary_urls;
  receipt.target_root_ids = placement->root_ids;
  for (const auto& [source_id, target_id] : placement->new_ids) {
    receipt.target_node_ids.push_back(target_id);
  }
  receipt.moved_at = base::Time::Now();
  session::RememberCrossLevelMove(std::move(receipt));

  // Only pages that were open reopen, in the item's order.
  std::vector<base::Uuid> reopen;
  for (const auto& node : placement->import.tree.nodes) {
    for (const base::Uuid& open_id : open.open_ids) {
      const auto moved = placement->new_ids.find(open_id);
      if (moved != placement->new_ids.end() && moved->second == node.id) {
        reopen.push_back(node.id);
      }
    }
  }
  auto reopen_in = [](base::FilePath path, std::vector<base::Uuid> ids,
                      bool activate, BrowserWindowInterface* window) {
    Profile* profile = session::FindLoadedProfile(path);
    SessionBridge* bridge =
        profile ? SessionBridgeFactory::GetForProfile(profile) : nullptr;
    if (bridge && window) {
      bridge->ReopenCrossLevelPages(window, ids, activate);
    }
  };
  if (request.follow) {
    // Handoff 011 S1 across Profiles: the window follows the moved active
    // tab by handing its frame to the target's window.
    auto then = base::BindOnce(reopen_in, target_path, std::move(reopen),
                               /*activate=*/true);
    if (request.target.key.profile_dir.empty()) {
      OpenMainWorkspaceByHandOver(request.target.key.workspace_id,
                                  std::move(then));
    } else {
      OpenIsolatedWorkspaceByHandOver(request.target.key.profile_dir,
                                      std::move(then));
    }
    return;
  }
  BrowserWindowInterface* window =
      request.presenter ? request.presenter->browser_.get()
                        : session::FindMostRecentNormalWindow(
                              session::FindLoadedProfile(target_path));
  // Without a window the moved pages wait as saved pages and open by URL
  // when the user opens them there.
  reopen_in(target_path, std::move(reopen), !!request.presenter, window);
}

void BrowserSidebarHostView::ShowCrossLevelFailure(
    const std::u16string& target_name) {
  const bool german = IsGerman();
  ShowStructureNotice(
      StructureText(u"Nicht verschoben", u"Not moved"),
      german ? base::StrCat({Quoted(true, target_name),
                             u" konnte die Seiten gerade nicht übernehmen. "
                             u"Hier hat sich nichts geändert."})
             : base::StrCat({Quoted(false, target_name),
                             u" couldn't take the pages just now. Nothing "
                             u"changed here."}));
}

bool BrowserSidebarHostView::UndoCrossLevelMoveIfLatest() {
  const session::CrossLevelMoveReceipt* latest =
      session::GetLatestCrossLevelMove();
  if (!latest) {
    return false;
  }
  const base::FilePath own = browser_->GetProfile()->GetPath();
  const bool is_source = own == latest->source_profile_path;
  if (!is_source && own != latest->target_profile_path) {
    return false;
  }
  Profile* source_profile =
      session::FindLoadedProfile(latest->source_profile_path);
  Profile* target_profile =
      session::FindLoadedProfile(latest->target_profile_path);
  SessionBridge* source =
      source_profile ? SessionBridgeFactory::GetForProfile(source_profile)
                     : nullptr;
  SessionBridge* target =
      target_profile ? SessionBridgeFactory::GetForProfile(target_profile)
                     : nullptr;
  const auto source_latest = [&] {
    return source && session::IsCrossLevelMoveLatestOnSource(
                         *latest, source->GetLatestTreeUndo());
  };
  const auto target_latest = [&] {
    return target && session::IsCrossLevelMoveLatestOnTarget(
                         *latest, target->GetLatestTreeUndo());
  };
  // A newer local change is undone first, as everywhere else.
  if (is_source ? !source_latest() : !target_latest()) {
    return false;
  }
  session::CrossLevelMoveReceipt receipt = *latest;
  session::ForgetCrossLevelMove();
  if (is_source ? !target_latest() : !source_latest()) {
    ShowStructureNotice(
        StructureText(u"Rückgängig nicht mehr möglich", u"Can't undo"),
        StructureText(u"Der andere Workspace hat sich seit dem Verschieben "
                      u"geändert. Die Seiten bleiben, wo sie jetzt sind.",
                      u"The other Workspace changed after the move. The "
                      u"pages stay where they are now."));
    return true;
  }
  const std::vector<base::Uuid> copy_roots = receipt.target_root_ids;
  target->RemoveCrossLevelCopy(
      copy_roots,
      base::BindOnce(
          [](session::CrossLevelMoveReceipt receipt, bool removed) {
            if (!removed) {
              // A page of the copy declined to close: nothing changed, and
              // Cmd+Z can try again.
              session::RememberCrossLevelMove(std::move(receipt));
              return;
            }
            Profile* profile =
                session::FindLoadedProfile(receipt.source_profile_path);
            SessionBridge* source =
                profile ? SessionBridgeFactory::GetForProfile(profile)
                        : nullptr;
            if (!source) {
              return;
            }
            if (receipt.source_deletion_subject &&
                session::IsCrossLevelMoveLatestOnSource(
                    receipt, source->GetLatestTreeUndo())) {
              std::ignore = source->tab_tree_store()->UndoLastMutation();
            }
            // Pages that were open come back open, again by URL.
            if (BrowserWindowInterface* window =
                    session::FindMostRecentNormalWindow(profile)) {
              source->ReopenCrossLevelPages(window, receipt.source_open_ids,
                                            /*activate_first=*/false);
              for (const GURL& url : receipt.source_temporary_urls) {
                window->OpenGURL(url,
                                 WindowOpenDisposition::NEW_BACKGROUND_TAB);
              }
            }
          },
          std::move(receipt)));
  return true;
}

}  // namespace ahoi::sidebar
