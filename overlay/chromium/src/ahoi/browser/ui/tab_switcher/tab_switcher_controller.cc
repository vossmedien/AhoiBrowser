// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/tab_switcher/tab_switcher_controller.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/tab_switcher/tab_switcher_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::tab_switcher {

namespace {

std::u16string ActiveWorkspaceName(BrowserWindowInterface* browser,
                                   SessionBridge* bridge) {
  Profile* const profile = browser->GetProfile();
  WorkspaceService* const workspaces =
      profile ? WorkspaceServiceFactory::GetForProfile(profile) : nullptr;
  const std::optional<base::Uuid> active =
      bridge ? bridge->GetActiveWorkspaceForWindow(browser) : std::nullopt;
  if (!workspaces || !active.has_value()) {
    return std::u16string();
  }
  for (const tab_tree::Workspace& workspace :
       workspaces->ordered_workspaces()) {
    if (workspace.id == *active) {
      return workspace.name;
    }
  }
  return std::u16string();
}

}  // namespace

TabSwitcherController::TabSwitcherController(
    BrowserWindowInterface* browser,
    ModalOverlayController* modal_overlay_controller)
    : browser_(browser), modal_overlay_controller_(modal_overlay_controller) {
  CHECK(browser_);
  CHECK(modal_overlay_controller_);
}

TabSwitcherController::~TabSwitcherController() {
  // As in CommandBarController: no teardown callback may re-enter.
  weak_ptr_factory_.InvalidateWeakPtrs();
  view_ = nullptr;
  if (bubble_widget_) {
    modal_overlay_controller_->DismissPanelImmediately(bubble_widget_.get());
  }
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

bool TabSwitcherController::Toggle() {
  if (view_) {
    view_->FocusNext();
    return true;
  }
  return Show();
}

bool TabSwitcherController::Show() {
  views::View* const anchor_view = modal_overlay_controller_->center_anchor();
  if (bubble_widget_ || !anchor_view || !anchor_view->GetWidget() ||
      modal_overlay_controller_->IsShowingAnyPanel()) {
    return false;
  }
  Profile* const profile = browser_->GetProfile();
  auto view = std::make_unique<TabSwitcherView>(
      ActiveWorkspaceName(browser_, SessionBridgeFactory::GetForProfile(profile)),
      profile ? profile->GetPrefs() : nullptr,
      TabSwitcherView::Callbacks{
          .activate = base::BindRepeating(
              [](base::WeakPtr<TabSwitcherController> controller,
                 size_t index) {
                // Tiles run this from their own key or click handler, which
                // the follow-up may destroy; act after it has returned.
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                    FROM_HERE,
                    base::BindOnce(&TabSwitcherController::Activate,
                                   controller, index));
              },
              weak_ptr_factory_.GetWeakPtr()),
          .close = base::BindRepeating(
              [](base::WeakPtr<TabSwitcherController> controller,
                 size_t index) {
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                    FROM_HERE,
                    base::BindOnce(&TabSwitcherController::CloseEntry,
                                   controller, index));
              },
              weak_ptr_factory_.GetWeakPtr())});

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::FLOAT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->SetShowTitle(false);
  delegate->SetAccessibleTitle(PanelTitle());
  delegate->set_fixed_width(kPanelWidth);
  delegate->set_margins(gfx::Insets());
  delegate->set_use_round_corners(true);
  delegate->set_corner_radius(visual_style::kPanelCornerRadius);
  // The view paints the panel material; the border stays transparent.
  delegate->SetBackgroundColor(SK_ColorTRANSPARENT);
  delegate->set_close_on_deactivate(false);
  delegate->SetCancelCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<TabSwitcherController> controller) {
        if (controller) {
          controller->RequestClose();
        }
        // The modal overlay closes the Widget after its fade-out.
        return false;
      },
      weak_ptr_factory_.GetWeakPtr()));

  view_ = delegate->SetContentsView(std::move(view));
  bubble_widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(), base::IgnoreArgs<views::Widget::ClosedReason>(
                          base::BindOnce(&TabSwitcherController::OnBubbleClosed,
                                         weak_ptr_factory_.GetWeakPtr())));
  if (!bubble_widget_) {
    view_ = nullptr;
    return false;
  }
  bubble_delegate_ = std::move(delegate);
  Rebuild(0, /*initial=*/true);
  if (entries_.empty() ||
      !modal_overlay_controller_->ShowPanel(
          bubble_widget_.get(),
          base::BindRepeating(&TabSwitcherController::CloseBubbleNow,
                              weak_ptr_factory_.GetWeakPtr()))) {
    view_ = nullptr;
    bubble_widget_.reset();
    bubble_delegate_.reset();
    return false;
  }
  if (view_) {
    view_->ReapplyAppearance();
    view_->FocusCurrent();
  }
  return true;
}

void TabSwitcherController::Rebuild(size_t focus, bool initial) {
  TabStripModel* const model = browser_->GetTabStripModel();
  SessionBridge* const bridge =
      SessionBridgeFactory::GetForProfile(browser_->GetProfile());
  tabs_.clear();
  std::vector<TabInput> inputs;
  std::map<split_tabs::SplitTabId, int> split_ids;
  for (int index = 0; model && index < model->count(); ++index) {
    tabs::TabInterface* const tab = model->GetTabAtIndex(index);
    content::WebContents* const contents = tab ? tab->GetContents() : nullptr;
    if (!contents) {
      continue;
    }
    // Tabs of the window's other Workspaces stay out; an unknown answer
    // (no Ahoi session state yet) keeps the tab.
    if (bridge &&
        !bridge->IsTabInActiveWorkspace(browser_, tab).value_or(true)) {
      continue;
    }
    const int id = static_cast<int>(inputs.size());
    tabs_.emplace(id, tab->GetHandle());
    std::optional<int> split;
    if (const std::optional<split_tabs::SplitTabId> split_id = tab->GetSplit()) {
      split = split_ids.emplace(*split_id, static_cast<int>(split_ids.size()))
                  .first->second;
    }
    inputs.push_back({.id = id,
                      .title = contents->GetTitle(),
                      .url = contents->GetLastCommittedURL(),
                      .saved = bridge && bridge->FindTreeNodeIdForTab(tab),
                      .split = split,
                      .last_active = contents->GetLastActiveTimeTicks(),
                      .active = model->GetActiveTab() == tab});
  }
  entries_ = BuildEntries(inputs);
  std::vector<EntryVisual> visuals;
  for (const Entry& entry : entries_) {
    // The most recently used pane stands for a split.
    tabs::TabInterface* preview = nullptr;
    base::TimeTicks preview_active;
    for (int id : entry.tab_ids) {
      tabs::TabInterface* const tab = tabs_.at(id).Get();
      if (tab && (!preview || inputs[id].last_active > preview_active)) {
        preview = tab;
        preview_active = inputs[id].last_active;
      }
    }
    visuals.push_back({.favicon = sidebar::GetLiveTabFavicon(preview),
                       .preview_tab = preview});
  }
  if (view_) {
    view_->SetEntries(entries_, std::move(visuals),
                      initial ? InitialFocus(entries_) : focus);
  }
}

void TabSwitcherController::Activate(size_t index) {
  if (!view_ || index >= entries_.size()) {
    return;
  }
  TabStripModel* const model = browser_->GetTabStripModel();
  tabs::TabInterface* target = nullptr;
  for (int id : entries_[index].tab_ids) {
    tabs::TabInterface* const tab = tabs_.at(id).Get();
    if (tab && (!target || tab->GetContents()->GetLastActiveTimeTicks() >
                               target->GetContents()->GetLastActiveTimeTicks())) {
      target = tab;
    }
  }
  const int tab_index = target && model ? model->GetIndexOfTab(target) : -1;
  if (tab_index >= 0) {
    model->ActivateTabAt(tab_index);
  }
  RequestClose();
}

void TabSwitcherController::CloseEntry(size_t index) {
  if (!view_ || index >= entries_.size()) {
    return;
  }
  for (int id : entries_[index].tab_ids) {
    if (tabs::TabInterface* const tab = tabs_.at(id).Get()) {
      tab->Close();
    }
  }
  // A page with a beforeunload dialog keeps its tile until it is answered.
  Rebuild(index, /*initial=*/false);
  if (entries_.empty()) {
    RequestClose();
    return;
  }
  if (view_) {
    view_->FocusCurrent();
  }
}

void TabSwitcherController::RequestClose() {
  if (bubble_widget_ &&
      !modal_overlay_controller_->RequestClose(bubble_widget_.get())) {
    CloseBubbleNow();
  }
}

void TabSwitcherController::CloseBubbleNow() {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
}

void TabSwitcherController::OnBubbleClosed() {
  // As in CommandBarController::OnBubbleClosed: this can run inside the
  // widget's activation observer, so the widget dies after the stack unwinds.
  view_ = nullptr;
  if (bubble_widget_) {
    modal_overlay_controller_->NotifyPanelClosed(bubble_widget_.get());
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<views::Widget> widget,
             std::unique_ptr<views::BubbleDialogDelegate> delegate) {
            widget.reset();
            delegate.reset();
          },
          std::move(bubble_widget_), std::move(bubble_delegate_)));
}

}  // namespace ahoi::tab_switcher
