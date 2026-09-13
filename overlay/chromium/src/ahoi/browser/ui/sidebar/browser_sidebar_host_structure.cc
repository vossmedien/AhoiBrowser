// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"

#include <algorithm>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {
std::u16string BrowserSidebarHostView::StructureText(
    std::u16string_view german,
    std::u16string_view english) {
  return std::u16string(base::StartsWith(base::i18n::GetConfiguredLocale(),
                                         "de",
                                         base::CompareCase::INSENSITIVE_ASCII)
                            ? german
                            : english);
}

void BrowserSidebarHostView::BuildArchiveMenus() {
  context_archive_workspace_id_ = controller_->view_model().workspace_id();
  context_menu_model_->AddItem(
      kArchiveList, StructureText(u"Archiv durchsuchen …", u"Search archive…"));
  context_archive_policy_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::u16string labels[] = {
      StructureText(u"Nie (Standard)", u"Never (default)"),
      StructureText(u"Nach 12 Stunden", u"After 12 hours"),
      StructureText(u"Nach 24 Stunden", u"After 24 hours"),
      StructureText(u"Nach 7 Tagen", u"After 7 days"),
      StructureText(u"Nach 30 Tagen", u"After 30 days")};
  for (int i = 0; i < 5; ++i)
    context_archive_policy_model_->AddCheckItem(kArchivePolicyCommandBase + i,
                                                labels[i]);
  context_menu_model_->AddSubMenu(
      kArchivePolicy,
      StructureText(u"Inaktive temporäre Tabs archivieren",
                    u"Archive inactive temporary tabs"),
      context_archive_policy_model_.get());
}

std::vector<base::Uuid> BrowserSidebarHostView::ContextArchiveNodes() const {
  if (context_node_id_ && session_bridge_)
    return session_bridge_->GetArchivePageGroup(*context_node_id_);
  auto* tab = context_runtime_tab_.get();
  if (!tab || !session_bridge_)
    return {};
  auto* model = session_bridge_->FindTabStripModelForTab(tab);
  if (!model || model != tab_strip_model_)
    return {};
  std::vector<base::Uuid> result;
  auto append = [&](tabs::TabInterface* member) {
    const auto id = session_bridge_->FindSharedTreeNodeIdForTab(member);
    if (!id)
      return false;
    result.push_back(*id);
    return true;
  };
  if (tab->GetSplit()) {
    const auto* split = model->GetSplitData(*tab->GetSplit());
    if (!split)
      return {};
    for (auto* member : split->ListTabs())
      if (!append(member))
        return {};
  } else if (!append(tab))
    return {};
  return result;
}

void BrowserSidebarHostView::ArchiveContextTabs() {
  session_bridge_->ArchiveTemporaryPages(
      ContextArchiveNodes(),
      base::BindOnce(&BrowserSidebarHostView::CompleteArchiveAction,
                     weak_ptr_factory_.GetWeakPtr()));
}

void BrowserSidebarHostView::CompleteArchiveAction(bool success) {
  if (success) {
    ScheduleRuntimePresentationRefresh();
    return;
  }
  ShowStructureNotice(
      StructureText(u"Aktion noch nicht abgeschlossen",
                    u"Action not completed"),
      StructureText(
          u"Die Aktion konnte nicht abgeschlossen werden. Aktive, geschützte "
          u"oder noch nicht gespeicherte Tabs bleiben erhalten. Fehlt der "
          u"ursprüngliche Ordner, wähle im Archiv ausdrücklich "
          u"„Wiederherstellen in …“.",
          u"The action could not complete. Active, protected or not yet "
          u"persisted tabs are retained. If the original folder is missing, "
          u"explicitly choose “Restore in…” in the archive."));
}

void BrowserSidebarHostView::ShowArchiveRestoreMenu(base::Uuid entry_id) {
  if (!GetWidget() || context_menu_scope_ != ContextMenuScope::kNone)
    return;
  const auto entries = session_bridge_->GetArchivedPages();
  if (std::ranges::none_of(
          entries, [&](const auto& entry) { return entry.id == entry_id; }))
    return;
  context_menu_scope_ = ContextMenuScope::kArchive;
  context_archive_id_ = entry_id;
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_model_->AddItem(
      kRestoreArchiveOriginal,
      StructureText(u"Am ursprünglichen Ort wiederherstellen",
                    u"Restore at original location"));
  if (BuildMoveToMenu(nullptr))
    context_menu_model_->AddSubMenu(
        kRestoreArchiveElsewhere,
        StructureText(u"Wiederherstellen in …", u"Restore in…"),
        context_move_menu_model_.get());
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, workspace_button_->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kNone);
  context_menu_runner_.reset();
  context_menu_model_.reset();
  context_move_menu_model_.reset();
  context_move_submenu_models_.clear();
  context_move_destinations_.clear();
  context_archive_id_.reset();
  context_menu_scope_ = ContextMenuScope::kNone;
}

void BrowserSidebarHostView::ShowStructureNotice(std::u16string title,
                                                 std::u16string body) {
  if (context_menu_scope_ != ContextMenuScope::kNone) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&BrowserSidebarHostView::ShowStructureNotice,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  std::move(title), std::move(body)));
    return;
  }
  if (!GetWidget() || structure_dialog_widget_)
    return;
  auto dialog = ui::DialogModel::Builder()
                    .SetTitle(std::move(title))
                    .AddParagraph(ui::DialogModelLabel(std::move(body)))
                    .AddOkButton(base::DoNothing())
                    .Build();
  auto delegate = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), workspace_button_, views::BubbleBorder::TOP_LEFT);
  structure_dialog_widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(),
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&BrowserSidebarHostView::OnStructureDialogClosed,
                         weak_ptr_factory_.GetWeakPtr())));
  if (structure_dialog_widget_) {
    delegate.release();
    structure_dialog_widget_->Show();
  }
}

void BrowserSidebarHostView::OnStructureDialogClosed() {
  structure_dialog_widget_.reset();
}

void BrowserSidebarHostView::UseSavedHome(base::Uuid node_id,
                                          bool set_current) {
  tab_tree::TreeNode node;
  auto* store = session_bridge_->tab_tree_store();
  if (store->GetNode(node_id, &node) != tab_tree::TabTreeStore::Result::kOk ||
      node.tombstone || node.is_temporary ||
      node.type != tab_tree::TreeNodeType::kSavedPage)
    return;
  auto* tab = session_bridge_->FindTabByTreeNodeId(node_id);
  if (tab && session_bridge_->FindTabStripModelForTab(tab) != tab_strip_model_)
    return;
  if (set_current) {
    if (!tab || !tab->GetContents())
      return;
    const auto result = store->SetSavedPageHome(
        node_id, tab->GetContents()->GetLastCommittedURL(), base::Time::Now());
    if (result != tab_tree::TabTreeStore::Result::kOk)
      OnMutationFailed(result);
    return;
  }
  if (!tab_tree::GetSharedHomeTarget(node) || node.home_url.is_empty()) {
    ShowStructureNotice(
        StructureText(u"Ausgangsadresse fehlt", u"Home address unavailable"),
        StructureText(
            u"Diese Ausgangsadresse ist auf diesem Gerät nicht verfügbar. Du "
            u"kannst eine eigene über „Aktuelle Seite als Ausgangsadresse "
            u"setzen“ festlegen.",
            u"This Home address is unavailable on this device. Use “Set "
            u"current page as Home address” to choose your own."));
    return;
  }
  if (!tab) {
    const auto opened = MaterializeSavedPage(node, true, true);
    if (!opened.valid)
      ShowStructureNotice(
          StructureText(u"Ausgangsadresse nicht geöffnet",
                        u"Home address not opened"),
          StructureText(u"Die Seite konnte auf diesem Gerät nicht geöffnet "
                        u"werden. Der gespeicherte Tab bleibt unverändert.",
                        u"The page could not be opened on this device. The "
                        u"saved tab is unchanged."));
    return;
  }
  // Explicit navigation through Chromium retains its BeforeUnload contract;
  // ordinary navigation, close/unload and incoming Home never enter this path.
  content::NavigationController::LoadURLParams params(node.home_url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  tab->GetContents()->GetController().LoadURLWithParams(params);
}
}  // namespace ahoi::sidebar
