// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import 'chrome://resources/cr_elements/cr_view_manager/cr_view_manager.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
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

export interface RemoteControlStatusResponse {
  action: string;
  prerequisite: string;
  syncEnabled: boolean;
  cloudKitAvailable: boolean;
  syncStatusLabel?: string;
  canPair: boolean;
  canEnable: boolean;
  enabled: boolean;
  approvedDeviceIds: string[];
}

export interface BrowserSettingsSyncStatusResponse {
  action: string;
  selection: 'none'|'some'|'all';
  supportedCount: number;
  selectedCount: number;
  canChange: boolean;
  syncEnabled: boolean;
}

export interface PortableExportOptionsResponse {
  available: boolean;
  workspaces: Array<{id: string, name: string}>;
  labels: {
    title: string,
    description: string,
    temporary: string,
    archives: string,
    prepare: string,
    save: string,
    unencrypted: string,
    workspaces: string,
    pages: string,
    splits: string,
    archivesCount: string,
    excluded: string,
    saved: string,
    cancelled: string,
    failed: string,
  };
}

export interface PortableExportPreviewResponse {
  status: 'ready'|'blocked';
  token?: string;
  bytes?: number;
  workspaces?: number;
  pages?: number;
  splits?: number;
  archives?: number;
  excluded?: number;
}

export interface SyncControlsStatusResponse {
  action: 'requested'|'blocked'|'';
  statusLabel: string;
  syncEnabled: boolean;
  providerAvailable: boolean;
  keySetupIssue: string;
  accountTransitionPending: boolean;
  zoneRecoveryPending: boolean;
  canSyncNow: boolean;
  canRetryKey: boolean;
  canChangeExtensionConsent: boolean;
  extensionSetupEnabled: boolean;
  extensionSettingsEnabled: boolean;
  bookmarkSyncEnabled: boolean;
  canChangeBookmarkConsent: boolean;
  bookmarkIssueLabel: string;
  extensionResults: Array<{
    id: string,
    status: string,
    canRetry: boolean,
    needsConfirmation: boolean,
  }>;
  labels: {
    syncNow: string,
    retryKey: string,
    extensions: string,
    extensionSetup: string,
    extensionSettings: string,
    extensionSettingsHint: string,
    bookmarks: string,
    bookmarkConsentHint: string,
    bookmarkStopHint: string,
    approveBookmarks: string,
    stopBookmarks: string,
    retryExtension: string,
    reviewExtension: string,
    recovery: string,
    accountRecoveryHint: string,
    uploadLocal: string,
    withoutUpload: string,
    recoverZone: string,
  };
}

export interface SettingsAhoiPageElement {
  $: {
    viewManager: CrViewManagerElement,
  };
}

const SettingsAhoiPageElementBase =
    SearchableViewContainerMixinLit(
        PrefServiceObserverMixinLit(WebUiListenerMixinLit(CrLitElement)));

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
      browserSettingsSyncStatus_: {type: Object},
      browserSettingsSyncActionPending_: {type: Boolean},
      browserSettingsSyncActionFailed_: {type: Boolean},
      syncControlsStatus_: {type: Object},
      syncControlsActionPending_: {type: Boolean},
      syncControlsActionFailed_: {type: Boolean},
      remoteControlStatus_: {type: Object},
      remoteControlDeviceId_: {type: String},
      remoteControlPublicKey_: {type: String},
      remoteControlActionPending_: {type: Boolean},
      portableExportOptions_: {type: Object},
      portableSelectedWorkspaceIds_: {type: Array},
      portableIncludeTemporary_: {type: Boolean},
      portableIncludeArchives_: {type: Boolean},
      portableExportPreview_: {type: Object},
      portableExportStatus_: {type: String},
      portableExportPending_: {type: Boolean},
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
  protected accessor browserSettingsSyncStatus_:
      BrowserSettingsSyncStatusResponse|null = null;
  protected accessor browserSettingsSyncActionPending_: boolean = false;
  protected accessor browserSettingsSyncActionFailed_: boolean = false;
  protected accessor syncControlsStatus_: SyncControlsStatusResponse|null = null;
  protected accessor syncControlsActionPending_: boolean = false;
  protected accessor syncControlsActionFailed_: boolean = false;
  protected accessor remoteControlStatus_: RemoteControlStatusResponse|null =
      null;
  protected accessor remoteControlDeviceId_: string = '';
  protected accessor remoteControlPublicKey_: string = '';
  protected accessor remoteControlActionPending_: boolean = false;
  protected accessor portableExportOptions_: PortableExportOptionsResponse|null =
      null;
  protected accessor portableSelectedWorkspaceIds_: string[] = [];
  protected accessor portableIncludeTemporary_: boolean = false;
  protected accessor portableIncludeArchives_: boolean = false;
  protected accessor portableExportPreview_: PortableExportPreviewResponse|null =
      null;
  protected accessor portableExportStatus_: string = '';
  protected accessor portableExportPending_: boolean = false;

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
    this.addWebUiListener(
        'ahoi-browser-settings-sync-status-changed',
        (status: BrowserSettingsSyncStatusResponse) => {
          this.applyBrowserSettingsSyncStatus_(status);
        });
    this.addWebUiListener(
        'ahoi-remote-control-status-changed',
        (status: RemoteControlStatusResponse) => {
          this.applyRemoteControlStatus_(status);
        });
    this.addWebUiListener(
        'ahoi-sync-controls-status-changed',
        (status: SyncControlsStatusResponse) => {
          this.applySyncControlsStatus_(status);
        });
    this.addWebUiListener(
        'ahoi-portable-export-result', (result: {status: string}) => {
          this.portableExportPending_ = false;
          this.portableExportStatus_ = result.status;
          if (result.status === 'saved') {
            this.portableExportPreview_ = null;
          }
        });
    this.mirrorPrefs({
      'ahoi.developer_toolkit.enabled': 'developerToolkitEnabledPref_',
      'ahoi.navigation.floating_auto_hide_enabled':
          'floatingNavigationAutoHideEnabledPref_',
      'ahoi.sync.enabled': 'syncEnabledPref_',
    });
    void this.refreshRemoteControlStatus_();
    void this.refreshBrowserSettingsSyncStatus_();
    void this.refreshSyncControlsStatus_();
    void this.refreshPortableExportOptions_();
  }

  private async refreshPortableExportOptions_() {
    try {
      this.portableExportOptions_ =
          await sendWithPromise<PortableExportOptionsResponse>(
              'ahoiGetPortableExportOptions');
    } catch {
      this.portableExportStatus_ = 'failed';
    }
  }

  protected onPortableWorkspaceChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const id = checkbox.dataset['workspaceId'];
    if (!id) {
      return;
    }
    const selected = new Set(this.portableSelectedWorkspaceIds_);
    checkbox.checked ? selected.add(id) : selected.delete(id);
    this.portableSelectedWorkspaceIds_ = [...selected];
    this.portableExportPreview_ = null;
    this.portableExportStatus_ = '';
  }

  protected onPortableTemporaryChange_(event: Event) {
    this.portableIncludeTemporary_ =
        (event.currentTarget as HTMLInputElement).checked;
    this.portableExportPreview_ = null;
    this.portableExportStatus_ = '';
  }

  protected onPortableArchivesChange_(event: Event) {
    this.portableIncludeArchives_ =
        (event.currentTarget as HTMLInputElement).checked;
    this.portableExportPreview_ = null;
    this.portableExportStatus_ = '';
  }

  protected async onPortablePreviewClick_() {
    if (this.portableExportPending_ ||
        this.portableSelectedWorkspaceIds_.length === 0) {
      return;
    }
    this.portableExportPending_ = true;
    this.portableExportPreview_ = null;
    this.portableExportStatus_ = '';
    try {
      const preview = await sendWithPromise<PortableExportPreviewResponse>(
          'ahoiPreparePortableExport', this.portableSelectedWorkspaceIds_,
          this.portableIncludeTemporary_, this.portableIncludeArchives_);
      this.portableExportPreview_ = preview.status === 'ready' ? preview : null;
      this.portableExportStatus_ = preview.status === 'ready' ? '' : 'failed';
    } catch {
      this.portableExportStatus_ = 'failed';
    } finally {
      this.portableExportPending_ = false;
    }
  }

  protected async onPortableSaveClick_() {
    if (this.portableExportPending_ || !this.portableExportPreview_?.token) {
      return;
    }
    this.portableExportPending_ = true;
    this.portableExportStatus_ = '';
    try {
      const result = await sendWithPromise<{status: string}>(
          'ahoiSavePortableExport', this.portableExportPreview_.token);
      if (result.status !== 'dialog') {
        this.portableExportPending_ = false;
        this.portableExportStatus_ = 'failed';
      }
    } catch {
      this.portableExportPending_ = false;
      this.portableExportStatus_ = 'failed';
    }
  }

  protected portableExportStatusText_(): string {
    const labels = this.portableExportOptions_?.labels;
    switch (this.portableExportStatus_) {
      case 'saved':
        return labels?.saved || '';
      case 'cancelled':
        return labels?.cancelled || '';
      case 'failed':
        return labels?.failed || '';
      default:
        return '';
    }
  }

  private applySyncControlsStatus_(status: SyncControlsStatusResponse) {
    this.syncControlsStatus_ = status;
    this.syncControlsActionFailed_ = status.action === 'blocked';
  }

  private async refreshSyncControlsStatus_() {
    try {
      this.applySyncControlsStatus_(
          await sendWithPromise<SyncControlsStatusResponse>(
              'ahoiGetSyncControlsStatus'));
    } catch {
      this.syncControlsStatus_ = null;
      this.syncControlsActionFailed_ = true;
    }
  }

  private async runSyncControlAction_(
      action: string, value?: boolean|string) {
    if (this.syncControlsActionPending_ || !this.syncControlsStatus_) {
      return;
    }
    this.syncControlsActionPending_ = true;
    this.syncControlsActionFailed_ = false;
    try {
      const status = value === undefined ?
          await sendWithPromise<SyncControlsStatusResponse>(
              'ahoiSyncControlAction', action) :
          await sendWithPromise<SyncControlsStatusResponse>(
              'ahoiSyncControlAction', action, value);
      this.applySyncControlsStatus_(status);
    } catch {
      await this.refreshSyncControlsStatus_();
      this.syncControlsActionFailed_ = true;
    } finally {
      this.syncControlsActionPending_ = false;
    }
  }

  protected onSyncNowClick_() {
    void this.runSyncControlAction_('syncNow');
  }

  protected onRetrySyncKeyClick_() {
    void this.runSyncControlAction_('retryKey');
  }

  protected onExtensionSetupChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const enabled = checkbox.checked;
    checkbox.checked = this.syncControlsStatus_?.extensionSetupEnabled ?? false;
    void this.runSyncControlAction_('extensionSetup', enabled);
  }

  protected onExtensionSettingsChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const enabled = checkbox.checked;
    checkbox.checked = this.syncControlsStatus_?.extensionSettingsEnabled ?? false;
    void this.runSyncControlAction_('extensionSettings', enabled);
  }

  protected onExtensionRetryClick_(event: Event) {
    const id = (event.currentTarget as HTMLElement).dataset['extensionId'];
    if (id) {
      void this.runSyncControlAction_('retryExtension', id);
    }
  }

  protected onBookmarkSyncClick_() {
    if (this.syncControlsStatus_) {
      void this.runSyncControlAction_(
          'bookmarkSync', !this.syncControlsStatus_.bookmarkSyncEnabled);
    }
  }

  protected onAccountRecoveryUploadClick_() {
    void this.runSyncControlAction_('confirmAccount', true);
  }

  protected onAccountRecoveryWithoutUploadClick_() {
    void this.runSyncControlAction_('confirmAccount', false);
  }

  protected onZoneRecoveryClick_() {
    void this.runSyncControlAction_('recoverZone');
  }

  private applyBrowserSettingsSyncStatus_(
      status: BrowserSettingsSyncStatusResponse) {
    this.browserSettingsSyncStatus_ = status;
    this.browserSettingsSyncActionFailed_ =
        status.action === 'blocked' || status.action === 'invalidRequest';
  }

  private async refreshBrowserSettingsSyncStatus_() {
    try {
      this.applyBrowserSettingsSyncStatus_(
          await sendWithPromise<BrowserSettingsSyncStatusResponse>(
              'ahoiGetBrowserSettingsSyncStatus'));
    } catch {
      this.browserSettingsSyncStatus_ = null;
      this.browserSettingsSyncActionFailed_ = true;
    }
  }

  protected async onBrowserSettingsSyncChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const enabled = checkbox.checked;
    // The native control toggles before dispatching change. Restore the last
    // authoritative state until the service replies, including a mixed state.
    checkbox.checked = this.browserSettingsSyncStatus_?.selection === 'all';
    checkbox.indeterminate =
        this.browserSettingsSyncStatus_?.selection === 'some';
    if (this.browserSettingsSyncActionPending_ ||
        !this.browserSettingsSyncStatus_?.canChange) {
      return;
    }
    this.browserSettingsSyncActionPending_ = true;
    this.browserSettingsSyncActionFailed_ = false;
    try {
      this.applyBrowserSettingsSyncStatus_(
          await sendWithPromise<BrowserSettingsSyncStatusResponse>(
              'ahoiSetBrowserSettingsSyncEnabled', enabled));
    } catch {
      await this.refreshBrowserSettingsSyncStatus_();
      this.browserSettingsSyncActionFailed_ = true;
    } finally {
      this.browserSettingsSyncActionPending_ = false;
    }
  }

  protected browserSettingsSyncStatusText_(): string {
    const status = this.browserSettingsSyncStatus_;
    if (!status) {
      return loadTimeData.getString(
          this.browserSettingsSyncActionFailed_ ?
              'ahoiBrowserSettingsSyncUnavailable' :
              'ahoiBrowserSettingsSyncLoading');
    }
    if (status.supportedCount === 0) {
      return loadTimeData.getString('ahoiBrowserSettingsSyncUnavailable');
    }
    return loadTimeData.getStringF(
        'ahoiBrowserSettingsSyncSelection', status.selectedCount,
        status.supportedCount);
  }

  private applyRemoteControlStatus_(status: RemoteControlStatusResponse) {
    this.remoteControlStatus_ = status;
    this.cloudKitAvailable_ = status.cloudKitAvailable;
  }

  private async refreshRemoteControlStatus_() {
    try {
      this.applyRemoteControlStatus_(
          await sendWithPromise<RemoteControlStatusResponse>(
              'ahoiGetRemoteControlStatus'));
    } catch {
      this.remoteControlStatus_ = null;
      this.cloudKitAvailable_ = false;
    }
  }

  protected async onRemoteControlEnabledChange_(
      event: CustomEvent<boolean>) {
    if (this.remoteControlActionPending_) {
      return;
    }
    this.remoteControlActionPending_ = true;
    try {
      this.applyRemoteControlStatus_(
          await sendWithPromise<RemoteControlStatusResponse>(
              'ahoiSetRemoteControlEnabled', event.detail));
    } catch {
      // The control is deliberately not allowed to persist its optimistic
      // checked state. Re-read the service authority so a blocked or failed
      // enable snaps back to the fail-closed backend state immediately.
      await this.refreshRemoteControlStatus_();
    } finally {
      this.remoteControlActionPending_ = false;
    }
  }

  protected onRemoteControlDeviceIdInput_(event: Event) {
    this.remoteControlDeviceId_ =
        (event.currentTarget as HTMLInputElement).value;
  }

  protected onRemoteControlPublicKeyInput_(event: Event) {
    this.remoteControlPublicKey_ =
        (event.currentTarget as HTMLInputElement).value;
  }

  protected canApproveRemoteControlDevice_(): boolean {
    return !!this.remoteControlStatus_?.canPair &&
        !this.remoteControlActionPending_ &&
        this.remoteControlDeviceId_.trim().length > 0 &&
        this.remoteControlPublicKey_.trim().length > 0;
  }

  protected async onRemoteControlApproveClick_() {
    if (!this.canApproveRemoteControlDevice_()) {
      return;
    }
    this.remoteControlActionPending_ = true;
    try {
      const status = await sendWithPromise<RemoteControlStatusResponse>(
          'ahoiApproveRemoteControlDevice',
          this.remoteControlDeviceId_.trim(),
          this.remoteControlPublicKey_.trim());
      this.applyRemoteControlStatus_(status);
      if (status.action === 'approved') {
        this.remoteControlDeviceId_ = '';
        this.remoteControlPublicKey_ = '';
      }
    } catch {
      await this.refreshRemoteControlStatus_();
    } finally {
      this.remoteControlActionPending_ = false;
    }
  }

  protected async onRemoteControlRevokeClick_(event: Event) {
    const deviceId =
        (event.currentTarget as HTMLElement).dataset['deviceId'];
    if (!deviceId || this.remoteControlActionPending_) {
      return;
    }
    this.remoteControlActionPending_ = true;
    try {
      this.applyRemoteControlStatus_(
          await sendWithPromise<RemoteControlStatusResponse>(
              'ahoiRevokeRemoteControlDevice', deviceId));
    } catch {
      await this.refreshRemoteControlStatus_();
    } finally {
      this.remoteControlActionPending_ = false;
    }
  }

  protected remoteControlPrerequisiteText_(): string {
    switch (this.remoteControlStatus_?.prerequisite) {
      case 'syncDisabled':
        return loadTimeData.getString('ahoiRemoteControlNeedsSync');
      case 'transportUnavailable':
        return loadTimeData.getString('ahoiRemoteControlNeedsCloudKit');
      case 'recoveryPending':
        return loadTimeData.getString('ahoiRemoteControlNeedsRecovery');
      case 'approvedDeviceRequired':
        return loadTimeData.getString('ahoiRemoteControlNeedsApproval');
      case 'ready':
        return loadTimeData.getString(
            this.remoteControlStatus_?.enabled ?
                'ahoiRemoteControlActive' :
                'ahoiRemoteControlReady');
      default:
        return loadTimeData.getString('ahoiRemoteControlLoading');
    }
  }

  protected remoteControlActionText_(): string {
    switch (this.remoteControlStatus_?.action) {
      case 'approved':
        return loadTimeData.getString('ahoiRemoteControlApproved');
      case 'invalidApproval':
        return loadTimeData.getString('ahoiRemoteControlInvalidApproval');
      case 'revoked':
        return loadTimeData.getString('ahoiRemoteControlRevoked');
      case 'blocked':
        return this.remoteControlPrerequisiteText_();
      default:
        return '';
    }
  }

  protected shortRemoteControlDeviceId_(deviceId: string): string {
    return deviceId.slice(0, 8);
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
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-ahoi-page': SettingsAhoiPageElement;
  }
}

customElements.define(SettingsAhoiPageElement.is, SettingsAhoiPageElement);
