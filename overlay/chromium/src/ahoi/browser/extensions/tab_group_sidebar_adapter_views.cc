// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/tab_group_sidebar_adapter_views.h"

#include <set>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_menu_presence.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"

namespace ahoi::extensions {
namespace {

class TabGroupFolderView final : public views::View,
                                 public views::ViewObserver,
                                 public views::TextfieldController,
                                 public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(TabGroupFolderView, views::View)

 public:
  TabGroupFolderView(base::WeakPtr<TabGroupSidebarAdapter> adapter,
                     const SidebarTabGroup& group, bool reveal_for_search,
                     ResolveGroupDropTab resolve_tab,
                     base::RepeatingClosure clear_drop_targets,
                     base::RepeatingClosure finish_drag)
      : adapter_(std::move(adapter)), group_id_(group.id),
        workspace_id_(group.workspace_id),
        resolve_tab_(std::move(resolve_tab)),
        clear_drop_targets_(std::move(clear_drop_targets)),
        finish_drag_(std::move(finish_drag)) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    auto* header = AddChildView(std::make_unique<views::View>());
    auto* header_layout = header->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    const auto title = group.visuals.title().empty()
                           ? l10n_util::GetStringUTF16(
                                 IDS_TAB_GROUPS_UNNAMED_GROUP_TOOLTIP)
                           : group.visuals.title();
    auto* toggle = header->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&TabGroupFolderView::Toggle,
                            weak_ptr_factory_.GetWeakPtr()), title));
    toggle->SetImageModel(views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kFolderFlippableIcon,
            GetTabGroupTabStripColorId(group.visuals.color(), true), 18));
    toggle->GetViewAccessibility().SetRole(ax::mojom::Role::kDisclosureTriangle);
    if (reveal_for_search || !group.visuals.is_collapsed()) {
      toggle->GetViewAccessibility().SetIsExpanded();
    } else {
      toggle->GetViewAccessibility().SetIsCollapsed();
    }
    header_layout->SetFlexForView(toggle, 1);
    header->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&TabGroupFolderView::OpenMenu,
                            weak_ptr_factory_.GetWeakPtr()),
        l10n_util::GetStringUTF16(IDS_TAB_GROUP_MORE_OPTIONS)));
    name_ = AddChildView(std::make_unique<views::Textfield>());
    name_->SetText(group.visuals.title());
    name_->SetAccessibleName(l10n_util::GetStringUTF16(
        IDS_TAB_GROUP_HEADER_CXMENU_TAB_GROUP_TITLE_ACCESSIBLE_NAME));
    name_->SetPlaceholderText(l10n_util::GetStringUTF16(
        IDS_TAB_GROUP_HEADER_BUBBLE_TITLE_PLACEHOLDER));
    name_->SetController(this);
    name_->AddObserver(this);
    name_->SetVisible(false);
    contents_ = AddChildView(std::make_unique<views::View>());
    contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(0, 12, 0, 0)));
    contents_->SetVisible(reveal_for_search || !group.visuals.is_collapsed());
  }

  ~TabGroupFolderView() override {
    name_->RemoveObserver(this);
    if (editing_ && adapter_) {
      adapter_->SetTitleEdit(std::nullopt);
    }
  }

  views::View* contents() { return contents_; }

  void OnViewBlurred(views::View*) override {
    // Leaving the editor cancels the draft, like Escape. Passive refreshes
    // still preserve it while focused; searching or opening a tab can refresh.
    if (editing_) {
      editing_ = false;
      name_->SetVisible(false);
      if (adapter_) {
        adapter_->SetTitleEdit(std::nullopt);
      }
      InvalidateLayout();
    }
  }

  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (event.key_code() == ui::VKEY_ESCAPE) {
      editing_ = false;
      name_->SetVisible(false);
      if (adapter_) {
        adapter_->SetTitleEdit(std::nullopt);
      }
      InvalidateLayout();
      return true;
    }
    if (event.key_code() != ui::VKEY_RETURN || !adapter_) {
      return false;
    }
    // Commit on Enter, not every keystroke: observers rebuild next turn.
    auto visuals = CurrentVisuals();
    if (!visuals) {
      return true;
    }
    visuals->SetTitle(std::u16string(name_->GetText()));
    if (adapter_->SetVisuals(group_id_, *visuals)) {
      editing_ = false;
      name_->SetVisible(false);
      adapter_->SetTitleEdit(std::nullopt);
      InvalidateLayout();
    }
    return true;
  }

  bool GetDropFormats(int* formats,
                     std::set<ui::ClipboardFormatType>* types) override {
    *formats |= ui::OSExchangeData::PICKLED_DATA;
    types->insert(drag::SavedSidebarTabDragFormat());
    types->insert(drag::RuntimeSidebarTabDragFormat());
    return true;
  }
  bool AreDropTypesRequired() override { return true; }
  bool CanDrop(const ui::OSExchangeData& data) override {
    const auto payload = drag::ReadSidebarTabDragPayload(data);
    base::WeakPtr<tabs::TabInterface> tab;
    if (payload && resolve_tab_) {
      tab = resolve_tab_.Run(*payload);
    }
    return tab && adapter_ && adapter_->CanAddTab(group_id_, tab.get());
  }
  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    const bool accepted = CanDrop(event.data());
    // Clear existing row highlights even when macOS skipped their exit event.
    // The native MOVE cursor is this folder's drop feedback.
    if (clear_drop_targets_) {
      clear_drop_targets_.Run();
    }
    return accepted ? ui::DragDropTypes::DRAG_MOVE : ui::DragDropTypes::DRAG_NONE;
  }
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    const auto payload = drag::ReadSidebarTabDragPayload(event.data());
    if (!payload || !CanDrop(event.data())) {
      return {};
    }
    // Bind identity, not a source View or an index captured during hover.
    return base::BindOnce(&TabGroupFolderView::Drop,
                          weak_ptr_factory_.GetWeakPtr(), *payload);
  }

 private:
  enum { kRename = 1, kUngroup = 2, kColorBase = 100 };

  std::optional<tab_groups::TabGroupVisualData> CurrentVisuals() const {
    if (adapter_) {
      for (const auto& group : adapter_->ReadGroups(workspace_id_)) {
        if (group.id == group_id_) {
          return group.visuals;
        }
      }
    }
    return std::nullopt;
  }
  void Toggle() {
    const auto visuals = CurrentVisuals();
    if (visuals) {
      adapter_->SetVisuals(group_id_, tab_groups::TabGroupVisualData(
          visuals->title(), visuals->color(), !visuals->is_collapsed()));
    }
  }
  void OpenMenu() {
    if (!GetWidget() || !CurrentVisuals()) {
      return;
    }
    menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_->AddItem(kRename, l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_RENAME));
    for (const auto& [color, label] : tab_groups::GetTabGroupColorLabelMap()) {
      menu_->AddRadioItem(kColorBase + static_cast<int>(color), label, 1);
    }
    menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_->AddItem(kUngroup,
        l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_UNGROUP));
    runner_ = std::make_unique<views::MenuRunner>(menu_.get(),
        views::MenuRunner::HAS_MNEMONICS);
    const auto alive = weak_ptr_factory_.GetWeakPtr();
    {
      const sidebar::ScopedSidebarMenu showing(this);
      runner_->RunMenuAt(GetWidget(), nullptr, GetBoundsInScreen(),
                        views::MenuAnchorPosition::kTopLeft,
                        ui::mojom::MenuSourceType::kNone);
    }
    if (alive) {
      runner_.reset();
      menu_.reset();
    }
  }
  bool IsCommandIdChecked(int command) const override {
    const auto visuals = CurrentVisuals();
    return visuals && command == kColorBase + static_cast<int>(visuals->color());
  }
  bool IsCommandIdEnabled(int) const override { return CurrentVisuals().has_value(); }
  void ExecuteCommand(int command, int) override {
    const auto visuals = CurrentVisuals();
    if (!visuals) {
      return;
    }
    if (command == kRename) {
      name_->SetText(visuals->title());
      name_->SetVisible(true);
      editing_ = true;
      adapter_->SetTitleEdit(group_id_);
      InvalidateLayout();
      name_->RequestFocus();
      name_->SelectAll(false);
    } else if (command == kUngroup) {
      adapter_->Ungroup(group_id_);
    } else if (command >= kColorBase) {
      adapter_->SetVisuals(group_id_, tab_groups::TabGroupVisualData(
          visuals->title(), static_cast<tab_groups::TabGroupColorId>(command - kColorBase),
          visuals->is_collapsed()));
    }
  }
  void Drop(drag::SidebarTabDragPayload payload, const ui::DropTargetEvent&,
            ui::mojom::DragOperation& operation,
            std::unique_ptr<ui::LayerTreeOwner>) {
    auto adapter = adapter_;
    auto finish = finish_drag_;
    auto tab = resolve_tab_.Run(payload);
    operation = tab && adapter && adapter->AddTab(group_id_, tab.get())
                    ? ui::mojom::DragOperation::kMove
                    : ui::mojom::DragOperation::kNone;
    // Releasing the host drag gate may destroy this view on a refresh.
    if (finish) {
      finish.Run();
    }
  }

  const base::WeakPtr<TabGroupSidebarAdapter> adapter_;
  const tab_groups::TabGroupId group_id_;
  const base::Uuid workspace_id_;
  const ResolveGroupDropTab resolve_tab_;
  const base::RepeatingClosure clear_drop_targets_;
  const base::RepeatingClosure finish_drag_;
  raw_ptr<views::Textfield> name_ = nullptr;
  raw_ptr<views::View> contents_ = nullptr;
  bool editing_ = false;
  std::unique_ptr<ui::SimpleMenuModel> menu_;
  std::unique_ptr<views::MenuRunner> runner_;
  base::WeakPtrFactory<TabGroupFolderView> weak_ptr_factory_{this};
};

BEGIN_METADATA(TabGroupFolderView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateTabGroupSidebarFolder(
    base::WeakPtr<TabGroupSidebarAdapter> adapter, const SidebarTabGroup& group,
    bool reveal_for_search, ResolveGroupDropTab resolve_tab,
    base::RepeatingClosure clear_drop_targets,
    base::RepeatingClosure finish_drag, views::View** contents) {
  auto folder = std::make_unique<TabGroupFolderView>(
      std::move(adapter), group, reveal_for_search, std::move(resolve_tab),
      std::move(clear_drop_targets), std::move(finish_drag));
  *contents = folder->contents();
  return folder;
}

}  // namespace ahoi::extensions
