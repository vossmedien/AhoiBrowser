// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

// "Zusammenführen mit …" in the Workspace menu and the command bar (ADR 0012,
// crest-hardening handoff 080). The dialog names what happens;
// SessionBridge::MergeWorkspace asks the source's pages when they have to
// close. Both entries open the same dialog, so confirmation, the no-undo
// warning for own website sessions and undo stay identical.

#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"

namespace ahoi::sidebar {

void BrowserSidebarHostView::AddWorkspaceMergeChoice(views::View* contents) {
  const tab_tree::Workspace* source =
      workspace_dialog_.workspace_id
          ? FindWorkspace(*workspace_dialog_.workspace_id)
          : nullptr;
  const tab_tree::Workspace* target =
      workspace_dialog_.merge_target_id
          ? FindWorkspace(*workspace_dialog_.merge_target_id)
          : nullptr;
  if (!source || !target) {
    return;
  }
  const bool shares_context = session_bridge_->SharesWebContext(
      *workspace_dialog_.workspace_id, *workspace_dialog_.merge_target_id);
  auto* body = contents->AddChildView(std::make_unique<views::Label>(
      shares_context
          ? StructureText(
                base::StrCat({u"Alle Seiten und Ordner aus „", source->name,
                              u"“ ziehen nach „", target->name,
                              u"“ um, offene Tabs laufen weiter. Danach gibt "
                              u"es „",
                              source->name,
                              u"“ nicht mehr. Rückgängig stellt ihn wieder "
                              u"her."}),
                base::StrCat({u"All pages and folders of \"", source->name,
                              u"\" move to \"", target->name,
                              u"\", and open tabs keep running. Then \"",
                              source->name,
                              u"\" is gone. Undo brings it back."}))
          : StructureText(
                base::StrCat({u"Gespeicherte Seiten und Ordner aus „",
                              source->name, u"“ ziehen nach „", target->name,
                              u"“ um. Anmeldungen ziehen nicht mit: Die "
                              u"offenen Tabs werden geschlossen, und die "
                              u"eigenen Website-Sitzungen werden gelöscht. "
                              u"Das lässt sich nicht rückgängig machen."}),
                base::StrCat({u"Saved pages and folders of \"", source->name,
                              u"\" move to \"", target->name,
                              u"\". Logins do not move along: the open tabs "
                              u"close, and the own website sessions are "
                              u"deleted. This cannot be undone."}))));
  body->SetSubpixelRenderingEnabled(false);
  body->SetMultiLine(true);
  body->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  workspace_dialog_.merge_into_folder =
      contents->AddChildView(std::make_unique<views::Checkbox>(StructureText(
          base::StrCat({u"Als Ordner „", source->name, u"“ ablegen"}),
          base::StrCat({u"Keep them in a folder \"", source->name, u"\""}))));
  workspace_dialog_.merge_into_folder->SetChecked(true);
}

bool BrowserSidebarHostView::AcceptWorkspaceMerge() {
  if (!workspace_dialog_.workspace_id || !workspace_dialog_.merge_target_id) {
    return true;
  }
  const bool into_folder = !workspace_dialog_.merge_into_folder ||
                           workspace_dialog_.merge_into_folder->GetChecked();
  // A veto of a closing page changes nothing (kCancelled).
  session_bridge_->MergeWorkspace(
      *workspace_dialog_.workspace_id, *workspace_dialog_.merge_target_id,
      into_folder,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> view,
             tab_tree::TabTreeStore::Result result) {
            if (view && result != tab_tree::TabTreeStore::Result::kOk &&
                result != tab_tree::TabTreeStore::Result::kCancelled) {
              view->OnMutationFailed(result);
            }
          },
          weak_ptr_factory_.GetWeakPtr()));
  return true;
}

bool BrowserSidebarHostView::ShowWorkspaceMergeDialog(
    const base::Uuid& target_id,
    bool dry_run) {
  if (!session_bridge_ || !workspace_service_ || !controller_ ||
      !modal_overlay_controller_) {
    return false;
  }
  // The same partners as the Workspace menu's submenu: the shown Workspace
  // into another Workspace of this Profile.
  const std::optional<base::Uuid> source_id =
      controller_->view_model().workspace_id();
  if (!source_id || !target_id.is_valid() || *source_id == target_id ||
      !FindWorkspace(*source_id) || !FindWorkspace(target_id) ||
      workspace_dialog_.widget || group_dialog_.widget) {
    return false;
  }
  if (dry_run) {
    return true;
  }
  // The command bar is still on screen (and fading out) while its item runs;
  // the overlay shows one panel at a time.
  modal_overlay_controller_->RunWhenIdle(
      base::BindOnce(&BrowserSidebarHostView::OpenWorkspaceMergeDialog,
                     weak_ptr_factory_.GetWeakPtr(), *source_id, target_id));
  return true;
}

void BrowserSidebarHostView::OpenWorkspaceMergeDialog(
    const base::Uuid& source_id,
    const base::Uuid& target_id) {
  if (!controller_ || controller_->view_model().workspace_id() != source_id ||
      !FindWorkspace(source_id) || !FindWorkspace(target_id) ||
      workspace_dialog_.widget || group_dialog_.widget) {
    return;
  }
  workspace_dialog_.merge_target_id = target_id;
  ShowWorkspaceDialog(PendingWorkspaceAction::kMerge, source_id);
}

}  // namespace ahoi::sidebar
