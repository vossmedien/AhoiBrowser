// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import 'chrome://resources/cr_elements/cr_view_manager/cr_view_manager.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_checkbox/cr_checkbox.js';
import {sendWithPromise} from 'chrome://resources/js/cr.js';
import '../controls/settings_dropdown_menu.js';
import '../controls/settings_toggle_button.js';
import '../settings_page/settings_section.js';
import {PrefService} from '/shared/settings/prefs2/pref_service.js';
import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';
// <if expr="not is_chromeos">
import 'chrome://resources/cr_components/theme_color_picker/theme_color_picker.js';
// </if>

import type {CrViewManagerElement} from 'chrome://resources/cr_elements/cr_view_manager/cr_view_manager.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {routes} from '../route.js';
import type {Route, SettingsRoutes} from '../router.js';
import {SearchableViewContainerMixinLit} from '../settings_page/searchable_view_container_mixin_lit.js';

import {getCss} from './ahoi_page.css.js';
import {getHtml} from './ahoi_page.html.js';

type PrefObject<T> = chrome.settingsPrivate.PrefObject<T>;

export interface ArcImportStats {
  sourceWorkspaces: number;
  sourceItems: number;
  workspaces: number;
  folders: number;
  pages: number;
  splits: number;
  degradedSplits: number;
  topApps: number;
  unsafeUrls: number;
  unsupportedItems: number;
}

export interface ArcImportPreviewResponse {
  status: string;
  snapshotToken: string;
  stats: ArcImportStats;
  conflictingWorkspaces: number;
  alreadyImported: boolean;
  sourceInUse: boolean;
  targetWorkspaces: string[];
  profiles: string[];
}

export interface ArcImportCommitResponse {
  status: string;
  stats: ArcImportStats;
  renamedWorkspaces: number;
  skippedWorkspaces: number;
  mergedWorkspaces: number;
  reconstructedSplits: number;
  approximatedFourPaneRatios: number;
}

export interface SettingsAhoiPageElement {
  $: {
    viewManager: CrViewManagerElement,
  };
}

const SettingsAhoiPageElementBase =
    SearchableViewContainerMixinLit(PrefServiceObserverMixinLit(CrLitElement));

export class SettingsAhoiPageElement extends SettingsAhoiPageElementBase {
  static get is() {
    return 'settings-ahoi-page';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      routes_: {type: Object},
      cloudKitAvailable_: {type: Boolean},
      developerToolkitEnabledPref_: {type: Object},
      floatingNavigationAutoHideEnabledPref_: {type: Object},
      floatingNavigationDelayOptions_: {type: Array},
      historyRetentionOptions_: {type: Array},
      syncEnabledPref_: {type: Object},
      arcImportStage_: {type: String},
      arcImportPreview_: {type: Object},
      arcImportResult_: {type: Object},
      arcConflictPolicy_: {type: String},
      arcImportSidebar_: {type: Boolean},
      arcReconstructSplits_: {type: Boolean},
      arcSelectedProfiles_: {type: Array},
      arcBackupConfirmed_: {type: Boolean},
      arcCommitConfirmed_: {type: Boolean},
    };
  }

  protected accessor routes_: SettingsRoutes = routes;
  protected accessor cloudKitAvailable_: boolean =
      loadTimeData.getBoolean('ahoiCloudKitAvailable');
  protected accessor developerToolkitEnabledPref_: PrefObject<boolean>|
      undefined = undefined;
  protected accessor floatingNavigationAutoHideEnabledPref_:
      PrefObject<boolean>|undefined = undefined;
  protected accessor syncEnabledPref_: PrefObject<boolean>|undefined =
      undefined;
  protected accessor arcImportStage_: string = 'idle';
  protected accessor arcImportPreview_: ArcImportPreviewResponse|null = null;
  protected accessor arcImportResult_: ArcImportCommitResponse|null = null;
  protected accessor arcConflictPolicy_: string = 'rename';
  protected accessor arcImportSidebar_: boolean = true;
  protected accessor arcReconstructSplits_: boolean = true;
  protected accessor arcSelectedProfiles_: string[] = [];
  protected accessor arcBackupConfirmed_: boolean = false;
  protected accessor arcCommitConfirmed_: boolean = false;

  protected accessor floatingNavigationDelayOptions_: DropdownMenuOptionList = [
    {value: 400, name: loadTimeData.getString('ahoiNavigationDelayFast')},
    {
      value: 650,
      name: loadTimeData.getString('ahoiNavigationDelayBalanced'),
    },
    {
      value: 1000,
      name: loadTimeData.getString('ahoiNavigationDelayRelaxed'),
    },
    {value: 2000, name: loadTimeData.getString('ahoiNavigationDelayLong')},
  ];

  protected accessor historyRetentionOptions_: DropdownMenuOptionList = [
    {value: 30, name: loadTimeData.getString('ahoiHistoryRetention30Days')},
    {value: 90, name: loadTimeData.getString('ahoiHistoryRetention90Days')},
    {value: 365, name: loadTimeData.getString('ahoiHistoryRetention365Days')},
    {value: -1, name: loadTimeData.getString('ahoiHistoryRetentionForever')},
  ];

  override connectedCallback() {
    super.connectedCallback();
    this.mirrorPrefs({
      'ahoi.developer_toolkit.enabled': 'developerToolkitEnabledPref_',
      'ahoi.navigation.floating_auto_hide_enabled':
          'floatingNavigationAutoHideEnabledPref_',
      'ahoi.sync.enabled': 'syncEnabledPref_',
    });
  }

  override currentRouteChanged(newRoute: Route, oldRoute?: Route) {
    super.currentRouteChanged(newRoute, oldRoute);

    queueMicrotask(() => {
      if (newRoute === routes.AHOI || newRoute === routes.BASIC) {
        this.$.viewManager.switchView('parent', 'no-animation', 'no-animation');
      }
    });
  }

  protected onAhoiDeveloperToolkitEnabledChange_(event: CustomEvent<boolean>) {
    if (!event.detail) {
      return;
    }

    const prefService = PrefService.getInstance();
    const hasVisibleAddressBarAction =
        prefService
            .getPref<boolean>('ahoi.developer_toolbar.show_cookie_button')
            .value ||
        prefService.getPref<boolean>('ahoi.developer_toolbar.show_cache_button')
            .value ||
        prefService
            .getPref<boolean>('ahoi.developer_toolbar.show_toolkit_button')
            .value;
    if (!hasVisibleAddressBarAction) {
      prefService.setPrefValue(
          'ahoi.developer_toolbar.show_toolkit_button', true);
    }
  }

  protected async onArcDiscover_() {
    this.arcImportStage_ = 'discovering';
    this.arcImportPreview_ = null;
    this.arcImportResult_ = null;
    this.arcBackupConfirmed_ = false;
    this.arcCommitConfirmed_ = false;
    try {
      const preview = await sendWithPromise<ArcImportPreviewResponse>(
          'ahoiArcDiscover');
      this.arcImportPreview_ = preview;
      this.arcSelectedProfiles_ = [...preview.profiles];
      this.arcImportStage_ = preview.status === 'ok' ?
          'preview' :
          (preview.status === 'sourceInUse' ? 'sourceInUse' : 'error');
    } catch {
      this.arcImportStage_ = 'error';
    }
  }

  protected async onArcCommit_() {
    const preview = this.arcImportPreview_;
    if (!preview || !this.canCommitArcImport_()) {
      return;
    }
    this.arcImportStage_ = 'committing';
    try {
      const result = await sendWithPromise<ArcImportCommitResponse>(
          'ahoiArcCommit', preview.snapshotToken, this.arcConflictPolicy_,
          this.arcSelectedProfiles_, this.arcImportSidebar_,
          this.arcReconstructSplits_, this.arcBackupConfirmed_,
          this.arcCommitConfirmed_);
      this.arcImportResult_ = result;
      this.arcImportStage_ =
          result.status === 'ok' || result.status === 'noChanges' ?
          'done' :
          (result.status === 'sourceInUse' ? 'sourceInUse' : 'error');
    } catch {
      this.arcImportStage_ = 'error';
    }
  }

  protected onArcProfileToggle_(event: Event) {
    const checkbox = event.currentTarget as HTMLElement&{checked: boolean};
    const profile = checkbox.dataset['profile'];
    if (!profile) {
      return;
    }
    const selected = new Set(this.arcSelectedProfiles_);
    checkbox.checked ? selected.add(profile) : selected.delete(profile);
    this.arcSelectedProfiles_ = [...selected];
  }

  protected onArcConflictPolicyChange_(event: Event) {
    this.arcConflictPolicy_ = (event.currentTarget as HTMLSelectElement).value;
  }

  protected onArcImportSidebarChange_(event: Event) {
    this.arcImportSidebar_ =
        (event.currentTarget as HTMLElement&{checked: boolean}).checked;
  }

  protected onArcReconstructSplitsChange_(event: Event) {
    this.arcReconstructSplits_ =
        (event.currentTarget as HTMLElement&{checked: boolean}).checked;
  }

  protected onArcBackupConfirmedChange_(event: Event) {
    this.arcBackupConfirmed_ =
        (event.currentTarget as HTMLElement&{checked: boolean}).checked;
  }

  protected onArcCommitConfirmedChange_(event: Event) {
    this.arcCommitConfirmed_ =
        (event.currentTarget as HTMLElement&{checked: boolean}).checked;
  }

  protected canCommitArcImport_(): boolean {
    return this.arcImportStage_ === 'preview' && this.arcImportSidebar_ &&
        this.arcSelectedProfiles_.length > 0 && this.arcBackupConfirmed_ &&
        this.arcCommitConfirmed_;
  }

  protected arcStatusText_(): string {
    if (this.arcImportStage_ === 'discovering') {
      return loadTimeData.getString('ahoiArcImportDiscovering');
    }
    if (this.arcImportStage_ === 'committing') {
      return loadTimeData.getString('ahoiArcImportCommitting');
    }
    if (this.arcImportStage_ === 'sourceInUse') {
      return loadTimeData.getString('ahoiArcImportSourceInUse');
    }
    if (this.arcImportStage_ === 'done') {
      return this.arcImportResult_?.status === 'noChanges' ?
          loadTimeData.getString('ahoiArcImportNoChanges') :
          loadTimeData.getString('ahoiArcImportSuccess');
    }
    if (this.arcImportStage_ === 'error') {
      return loadTimeData.getString('ahoiArcImportError');
    }
    return '';
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-ahoi-page': SettingsAhoiPageElement;
  }
}

customElements.define(SettingsAhoiPageElement.is, SettingsAhoiPageElement);
