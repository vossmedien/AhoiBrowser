// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/command_service_factory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_overlay_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tabs_surface_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {
BrowserSidebarHostView::BrowserSidebarHostView(
    Browser* browser,
    SessionBridge* session_bridge,
    WorkspaceService* workspace_service,
    ModalOverlayController* modal_overlay_controller)
    : browser_(browser),
      session_bridge_(session_bridge),
      workspace_service_(workspace_service),
      modal_overlay_controller_(modal_overlay_controller),
      tab_strip_model_(browser->tab_strip_model()),
      controller_(std::make_unique<SidebarTreeController>(
          session_bridge->tab_tree_store())) {
  CHECK(modal_overlay_controller_);
  CHECK(browser_);
  CHECK(session_bridge_);
  CHECK(session_bridge_->tab_tree_store());
  CHECK(workspace_service_);
  CHECK(tab_strip_model_);
  favicon_service_ = FaviconServiceFactory::GetForProfile(
      browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  history_service_ = HistoryServiceFactory::GetForProfile(
      browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  // SessionBridge and CommandServiceFactory both reject OTR and non-regular
  // profiles. Keep that boundary explicit here as well: discovery must never
  // expose another profile's durable tree or recently-closed session list.
  Profile* const profile = browser_->GetProfile();
  if (profile && profile->IsRegularProfile() && !profile->IsOffTheRecord()) {
    command_service_ = CommandServiceFactory::GetForProfile(profile);
    if (command_service_) {
      discovery_model_ = std::make_unique<SidebarDiscoveryModel>(
          command_service_, TabRestoreServiceFactory::GetForProfile(profile));
    }
  }
  tab_preview_controller_ = std::make_unique<SidebarTabPreviewController>(
      base::BindRepeating(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             const SidebarTabPreviewTarget& target)
              -> std::optional<SidebarTabPreviewData> {
            return host ? host->ResolveTabPreviewData(target) : std::nullopt;
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             const SidebarTabPreviewTarget& target, const views::View* anchor) {
            return host && host->ValidateTabPreviewAnchor(target, anchor);
          },
          weak_ptr_factory_.GetWeakPtr()));

  SetPreferredSize(gfx::Size(visual_style::kSidebarWidthDefault, 480));
  // The appearance resolver installs the themed opaque/glass surface after
  // the child hierarchy exists. Keeping this container transparent here lets
  // the resolver's backdrop blur sample the browser content behind the tree.
  SetBackground(nullptr);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
  GetViewAccessibility().SetName(u"AhoiBrowser");

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(visual_style::kSidebarTopInset,
                        visual_style::kSidebarHorizontalInset,
                        visual_style::kSidebarBottomInset,
                        visual_style::kSidebarHorizontalInset),
      visual_style::kSidebarSectionSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto workspace_header = std::make_unique<views::View>();
  auto* workspace_header_layout =
      workspace_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          visual_style::kSidebarFooterSpacing));
  workspace_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto workspace_selector_host = std::make_unique<views::View>();
  workspace_selector_host->SetPreferredSize(
      gfx::Size(0, visual_style::kSidebarActionCellHeight));
  workspace_selector_host->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  workspace_button_ =
      workspace_selector_host->AddChildView(CreateWorkspaceSelectorButton(
          base::BindRepeating(&BrowserSidebarHostView::OnWorkspacePressed,
                              base::Unretained(this))));

  // The drag-only target is a sibling of the selector inside a fixed-height
  // FillLayout. It therefore covers the workspace name while dragging but can
  // never insert a row or move the saved/open-tab surfaces below it.
  new_group_drop_target_ =
      workspace_selector_host->AddChildView(CreateNewGroupDropTargetView(
          base::BindRepeating(
              [](base::WeakPtr<BrowserSidebarHostView> host,
                 const base::Uuid& node_id) {
                if (!host ||
                    !host->controller_->view_model().GetNode(node_id)) {
                  return false;
                }
                host->ShowCreateGroupDialog(node_id);
                return host && host->group_dialog_widget_;
              },
              weak_ptr_factory_.GetWeakPtr()),
          base::BindRepeating(
              [](base::WeakPtr<BrowserSidebarHostView> host,
                 int runtime_tab_handle) {
                if (!host || !host->FindTemporaryTab(runtime_tab_handle)) {
                  return false;
                }
                host->ShowCreateGroupDialogForTemporaryTab(runtime_tab_handle);
                return host && host->group_dialog_widget_;
              },
              weak_ptr_factory_.GetWeakPtr()),
          base::BindRepeating(
              &BrowserSidebarHostView::ClaimDropTargetPresentation,
              weak_ptr_factory_.GetWeakPtr())));
  SetNewGroupDropTargetVisible(new_group_drop_target_, false);

  views::View* workspace_selector_host_ptr =
      workspace_header->AddChildView(std::move(workspace_selector_host));
  workspace_header_layout->SetFlexForView(workspace_selector_host_ptr, 1);
  if (discovery_model_) {
    workspace_header->AddChildView(CreateSidebarHeaderActionButton(
        base::BindRepeating(&BrowserSidebarHostView::OnSidebarDiscoveryPressed,
                            weak_ptr_factory_.GetWeakPtr()),
        vector_icons::kSearchIcon,
        l10n_util::GetStringUTF16(IDS_AHOI_SIDEBAR_DISCOVERY_SEARCH)));
  }
  floating_sidebar_button_ =
      workspace_header->AddChildView(CreateSidebarHeaderActionButton(
          base::BindRepeating(
              &BrowserSidebarHostView::OnSidebarHeaderActionPressed,
              weak_ptr_factory_.GetWeakPtr(),
              /*toggle_visibility=*/false),
          kDockToLeftIcon,
          l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_FLOATING_SIDEBAR)));
  SetSidebarHeaderActionToggleState(
      floating_sidebar_button_,
      browser_->GetBrowserView().GetAhoiSidebarPresentationMode() ==
          SidebarPresentationMode::kFloating);
  workspace_header->AddChildView(CreateSidebarHeaderActionButton(
      base::BindRepeating(&BrowserSidebarHostView::OnSidebarHeaderActionPressed,
                          weak_ptr_factory_.GetWeakPtr(),
                          /*toggle_visibility=*/true),
      kLeftPanelCloseFlippableIcon,
      l10n_util::GetStringUTF16(IDS_AHOI_CONTEXT_HIDE_SIDEBAR)));
  AddChildView(std::move(workspace_header));
  SetWorkspaceSelectorPresentation(workspace_button_, u"Ahoi", u"A",
                                   visual_style::kDefaultAccent);
  workspace_button_->set_context_menu_controller(this);

  auto tabs_surface = CreateSidebarTabsSurfaceView();
  auto* tabs_surface_layout =
      tabs_surface->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  tabs_surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto tree = std::make_unique<SidebarTreeView>(
      controller_.get(), this,
      l10n_util::GetStringUTF16(IDS_AHOI_SIDEBAR_TREE_ACCESSIBLE_NAME),
      l10n_util::GetStringUTF16(IDS_AHOI_SPLIT_WITH_PREFIX));
  tree_view_ = tree.get();
  tabs_surface->AddChildView(std::move(tree));

  open_tabs_header_ = tabs_surface->AddChildView(CreateSidebarSectionDivider(
      base::BindRepeating(&BrowserSidebarHostView::CloseAllTemporaryTabs,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_DOWNLOAD_LINK_CLEAR_ALL)));

  auto open_tabs = CreateOpenTabsDropTargetView(
      base::BindRepeating(&BrowserSidebarHostView::CanDropOpenTabToTemporary,
                          base::Unretained(this)),
      base::BindRepeating(&BrowserSidebarHostView::DropOpenTabToTemporary,
                          base::Unretained(this)),
      base::BindRepeating(&BrowserSidebarHostView::ClaimDropTargetPresentation,
                          weak_ptr_factory_.GetWeakPtr()));
  open_tabs->GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  open_tabs->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_OPEN_TABS));
  open_tabs_container_ = tabs_surface->AddChildView(std::move(open_tabs));
  // ScrollView expands its contents to at least the viewport. Giving the
  // temporary-tab target the remaining height makes the entire free lower
  // sidebar a valid saved-to-temporary or split-detach drop zone instead of
  // requiring a hit on the narrow list itself.
  tabs_surface_layout->SetFlexForView(open_tabs_container_, 1,
                                      /*use_min_size=*/true);

  auto remote_tabs_header = std::make_unique<views::View>();
  auto* remote_header_layout =
      remote_tabs_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(10, 8, 4, 8)));
  remote_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* remote_header_label = remote_tabs_header->AddChildView(
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(
          IDS_SIDE_PANEL_TABS_FROM_OTHER_DEVICES_TITLE)));
  remote_header_label->SetSubpixelRenderingEnabled(false);
  remote_header_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  remote_header_label->SetEnabledColor(visual_style::kMutedText);
  remote_tabs_header_ =
      tabs_surface->AddChildView(std::move(remote_tabs_header));
  remote_tabs_header_->SetVisible(false);

  auto remote_tabs = std::make_unique<views::View>();
  auto* remote_tabs_layout =
      remote_tabs->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  remote_tabs_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  remote_tabs_container_ = tabs_surface->AddChildView(std::move(remote_tabs));
  remote_tabs_container_->SetVisible(false);
  // Cross-device rows are part of the tab list, not a detached management
  // page. Keep them immediately below the saved tree and above temporary
  // local tabs; the latter may flex into otherwise unused sidebar height.
  tabs_surface->ReorderChildView(remote_tabs_header_, 1);
  tabs_surface->ReorderChildView(remote_tabs_container_, 2);
  auto* const mini_player_scroll_inset =
      tabs_surface->AddChildView(std::make_unique<views::View>());
  mini_player_scroll_inset->SetPreferredSize(gfx::Size());

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackground(nullptr);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // Re-query SidebarTabsSurfaceView with the live viewport width on every
  // layout pass. Without this, ScrollView keeps the contents' former preferred
  // width after a native sidebar resize, so the separator and "Clear all"
  // action remain at their old x position and are clipped by the new viewport.
  scroll->SetUseContentsPreferredSize(true);
  scroll->SetContents(std::move(tabs_surface));
  auto media_overlay =
      CreateMiniPlayerOverlay(std::move(scroll), mini_player_scroll_inset);
  media_overlay_view_ = media_overlay.get();
  if (discovery_model_) {
    auto discovery = std::make_unique<SidebarDiscoveryView>(
        discovery_model_.get(), std::move(media_overlay),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               const std::u16string& query,
               const std::vector<SidebarDiscoveryItem>& items) {
              return host ? host->ApplySidebarDiscoveryFilter(query, items)
                          : std::set<std::string>();
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               SidebarDiscoveryView::PrimaryResultAction action) {
              return host && host->HandleSidebarDiscoveryPrimaryResult(action);
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host,
               const CommandItem& item) {
              return host && host->ActivateSidebarDiscoveryCommand(item);
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(
            [](base::WeakPtr<BrowserSidebarHostView> host, SessionID entry_id) {
              return host && host->RestoreSidebarDiscoveryEntry(entry_id);
            },
            weak_ptr_factory_.GetWeakPtr()),
        base::BindRepeating(&BrowserSidebarHostView::CloseSidebarDiscovery,
                            weak_ptr_factory_.GetWeakPtr()));
    discovery_view_ = AddChildView(std::move(discovery));
    layout->SetFlexForView(discovery_view_, 1, /*use_min_size=*/true);
  } else {
    media_overlay_view_ = AddChildView(std::move(media_overlay));
    layout->SetFlexForView(media_overlay_view_, 1, /*use_min_size=*/true);
  }

  auto actions = std::make_unique<views::View>();
  auto* actions_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0),
          visual_style::kSidebarFooterSpacing));
  actions_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  actions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  const auto add_action = [&](int command_id, const gfx::VectorIcon& icon,
                              int label_id) {
    auto* action = actions->AddChildView(CreateSidebarActionButton(
        base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                            weak_ptr_factory_.GetWeakPtr(), command_id),
        icon, l10n_util::GetStringUTF16(label_id)));
    actions_layout->SetFlexForView(action, 1);
  };
  auto* split_action = actions->AddChildView(CreateSidebarSplitActionCell(
      base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                          weak_ptr_factory_.GetWeakPtr(), IDC_NEW_TAB),
      vector_icons::kAddWeight500Icon, l10n_util::GetStringUTF16(IDS_NEW_TAB),
      base::BindRepeating(&BrowserSidebarHostView::RunBrowserCommand,
                          weak_ptr_factory_.GetWeakPtr(),
                          IDC_NEW_INCOGNITO_WINDOW),
      kIncognitoIcon, l10n_util::GetStringUTF16(IDS_NEW_INCOGNITO_WINDOW)));
  actions_layout->SetFlexForView(split_action, 1);
  add_action(IDC_SHOW_DOWNLOADS, vector_icons::kDownloadIcon,
             IDS_DOWNLOAD_HISTORY_TITLE);
  add_action(IDC_SHOW_HISTORY, vector_icons::kHistoryIcon, IDS_HISTORY_MENU);
  add_action(IDC_OPTIONS, vector_icons::kSettingsIcon, IDS_SETTINGS);
  sidebar_actions_ = AddChildView(std::move(actions));

  // The drag-only action overlays the workspace pill at exactly the same
  // bounds. It therefore remains easy to hit without changing the scroll
  // viewport or moving any saved/temporary tab while AppKit owns the drag.

  workspace_service_->AddObserver(this);
  tab_strip_model_->AddObserver(this);
  session_presentation_subscription_ =
      session_bridge_->AddRuntimePresentationChangedCallback(
          base::BindRepeating(
              &BrowserSidebarHostView::OnSessionPresentationChanged,
              base::Unretained(this)));
  window_id_ = session_bridge_->GetWindowId(browser_);
  ActivateInitialWorkspace();
  UpdateWorkspaceSelectorIndicators();
  SynchronizeSelection();
  // Project the local TabStrip/session state before attaching remote sync.
  // CloudKit transport, device snapshots and thumbnail work must never hold
  // the first interactive sidebar frame hostage.
  RefreshRuntimePresentation();

  profile_sync_service_ =
      sync::ProfileSyncServiceFactory::GetForProfile(browser_->GetProfile());

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          browser_->GetProfile()->GetPrefs(),
          base::BindRepeating(&BrowserSidebarHostView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  PrefService* const prefs = browser_->GetProfile()->GetPrefs();
  if (prefs->FindPreference(appearance::kSidebarPageTintEnabledPref)) {
    page_tint_pref_change_registrar_.Init(prefs);
    page_tint_pref_change_registrar_.Add(
        appearance::kSidebarPageTintEnabledPref,
        base::BindRepeating(&BrowserSidebarHostView::RefreshPageTint,
                            weak_ptr_factory_.GetWeakPtr(),
                            /*allow_animation=*/true));
  }
  OnAppearanceChanged(appearance_signal_source_->policy());
}

}  // namespace ahoi::sidebar
