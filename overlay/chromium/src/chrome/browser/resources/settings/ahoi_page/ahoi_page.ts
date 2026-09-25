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
    importFile: string,
    importReady: string,
    importFailed: string,
    importNew: string,
    importIdentical: string,
    importConflict: string,
    importDestination: string,
    importTargetUnavailable: string,
    importCommit: string,
    importSelectionHint: string,
    imported: string,
    noChanges: string,
    importChanged: string,
    importCommitFailed: string,
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

export interface PortableImportPreviewResponse {
  status: 'preview'|'failed'|'cancelled'|'targetUnavailable'|
      'imported'|'noChanges'|'conflict'|'commitFailed';
  token?: string;
  canImport?: boolean;
  workspaces?: Array<{
    id: string,
    name: string,
    destination: 'new'|'identical'|'conflict',
  }>;
  pages?: number;
  splits?: number;
  archives?: number;
  newItems?: number;
  identicalItems?: number;
  conflictingItems?: number;
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

export type LinkRoutingMode = 'normal_tab'|'quick_window';

export interface LinkRoutingRule {
  id: string;
  enabled: boolean;
  host: string;
  includeSubdomains: boolean;
  path: string;
  target: string;
  mode: LinkRoutingMode;
  targetAvailable: boolean;
  targetName: string;
}

export interface LinkRoutingStatusResponse {
  available: boolean;
  canChange: boolean;
  enabled: boolean;
  rules: LinkRoutingRule[];
  defaultRoute: {target: string, mode: LinkRoutingMode, targetAvailable: boolean};
  workspaces: Array<{id: string, name: string, separated: boolean}>;
  action: 'saved'|'invalid'|'blocked'|'';
  error: string;
  errorLabel: string;
  labels: {
    title: string,
    description: string,
    enabled: string,
    rules: string,
    noRules: string,
    ruleEnabled: string,
    host: string,
    hostPortHint: string,
    includeSubdomains: string,
    path: string,
    target: string,
    mode: string,
    normalTab: string,
    quickWindow: string,
    add: string,
    delete: string,
    moveUp: string,
    moveDown: string,
    reset: string,
    resetHint: string,
    resetConfirm: string,
    defaultRoute: string,
    defaultRouteHint: string,
    lastActive: string,
    unavailableTarget: string,
    separated: string,
    example: string,
    examplePlaceholder: string,
    rememberHint: string,
    saved: string,
  };
}

export interface ShortcutCommandItem {
  id: string;
  category: string;
  categoryLabel: string;
  title: string;
  keys: string[];
  defaultKeys: string[];
  customized: boolean;
  rebindable: boolean;
}

export interface ShortcutLabels {
  title: string;
  description: string;
  search: string;
  change: string;
  recording: string;
  unbind: string;
  reset: string;
  resetAll: string;
  none: string;
  fixed: string;
  customized: string;
  defaultIs: string;
  noMatch: string;
}

export interface ShortcutStatusResponse {
  labels: ShortcutLabels;
  commands: ShortcutCommandItem[];
  canChange: boolean;
  action: string;
  errorLabel: string;
}

export interface LinkRoutingExampleResponse {
  status: 'empty'|'invalidUrl'|'notRoutable'|'disabled'|'routed'|'needsChoice';
  ruleIndex: number;
  allPorts: boolean;
  ruleId: string;
  targetId: string;
  text: string;
}

export interface LinkRoutingDraft {
  host: string;
  includeSubdomains: boolean;
  path: string;
  target: string;
  mode: LinkRoutingMode;
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
      portableImportPreview_: {type: Object},
      portableImportSelectedWorkspaceIds_: {type: Array},
      portableImportStatus_: {type: String},
      portableImportPending_: {type: Boolean},
      linkRouting_: {type: Object},
      linkRoutingPending_: {type: Boolean},
      linkRoutingErrorRuleId_: {type: String},
      linkRoutingDraft_: {type: Object},
      linkRoutingExampleInput_: {type: String},
      linkRoutingExample_: {type: Object},
      linkRoutingResetArmed_: {type: Boolean},
      shortcuts_: {type: Object},
      shortcutQuery_: {type: String},
      shortcutRecordingId_: {type: String},
      shortcutErrorId_: {type: String},
      shortcutPending_: {type: Boolean},
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
  protected accessor portableImportPreview_: PortableImportPreviewResponse|null =
      null;
  protected accessor portableImportSelectedWorkspaceIds_: string[] = [];
  protected accessor portableImportStatus_: string = '';
  protected accessor portableImportPending_: boolean = false;
  protected accessor linkRouting_: LinkRoutingStatusResponse|null = null;
  protected accessor linkRoutingPending_: boolean = false;
  // Which editor part the shown error belongs to: a rule id, 'new' for the
  // add form, 'default' for the default route, '' for the whole editor.
  protected accessor linkRoutingErrorRuleId_: string = '';
  protected accessor linkRoutingDraft_: LinkRoutingDraft = {
    host: '',
    includeSubdomains: false,
    path: '',
    target: '',
    mode: 'normal_tab',
  };
  protected accessor linkRoutingExampleInput_: string = '';
  protected accessor linkRoutingExample_: LinkRoutingExampleResponse|null =
      null;
  protected accessor linkRoutingResetArmed_: boolean = false;
  private linkRoutingExampleSequence_: number = 0;
  protected accessor shortcuts_: ShortcutStatusResponse|null = null;
  protected accessor shortcutQuery_: string = '';
  // The command whose new key is being recorded, or ''.
  protected accessor shortcutRecordingId_: string = '';
  // The command the shown error belongs to ('' for the whole editor).
  protected accessor shortcutErrorId_: string = '';
  protected accessor shortcutPending_: boolean = false;

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
    this.addWebUiListener(
      'ahoi-portable-import-result',
      (result: PortableImportPreviewResponse) => {
        this.portableImportPending_ = false;
        this.portableImportPreview_ =
            result.status === 'preview' ? result : null;
        this.portableImportSelectedWorkspaceIds_ =
            result.status === 'preview' ?
            (result.workspaces || [])
                .filter(workspace => workspace.destination !== 'conflict')
                .map(workspace => workspace.id) :
            [];
        this.portableImportStatus_ = result.status;
        if (result.status === 'imported') {
          this.portableExportPreview_ = null;
          void this.refreshPortableExportOptions_();
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
    this.addWebUiListener(
        'ahoi-link-routing-changed', (status: LinkRoutingStatusResponse) => {
          this.applyLinkRoutingStatus_(status);
          void this.resolveLinkRoutingExample_();
        });
    void this.refreshLinkRouting_();
    this.addWebUiListener(
        'ahoi-shortcuts-changed', (status: ShortcutStatusResponse) => {
          this.shortcuts_ = status;
        });
    void this.refreshShortcuts_();
  }

  private async refreshShortcuts_() {
    try {
      this.shortcuts_ =
          await sendWithPromise<ShortcutStatusResponse>('ahoiGetShortcuts');
    } catch {
      this.shortcuts_ = null;
    }
  }

  private async runShortcutAction_(
      action: string, payload: Record<string, unknown>, scope: string) {
    if (this.shortcutPending_ || !this.shortcuts_?.canChange) {
      return;
    }
    this.shortcutPending_ = true;
    try {
      const status = await sendWithPromise<ShortcutStatusResponse>(
          'ahoiShortcutAction', action, payload);
      this.shortcuts_ = status;
      // A refused key stays explained next to its command.
      this.shortcutErrorId_ = status.errorLabel ? scope : '';
    } catch {
      await this.refreshShortcuts_();
    } finally {
      this.shortcutPending_ = false;
    }
  }

  protected get filteredShortcuts_(): ShortcutCommandItem[] {
    const query = this.shortcutQuery_.trim().toLocaleLowerCase();
    const commands = this.shortcuts_?.commands || [];
    if (!query) {
      return commands;
    }
    return commands.filter(
        command => command.title.toLocaleLowerCase().includes(query) ||
            command.categoryLabel.toLocaleLowerCase().includes(query) ||
            command.keys.some(key => key.toLocaleLowerCase().includes(query)));
  }

  protected isShortcutLocked_(): boolean {
    return !this.shortcuts_?.canChange || this.shortcutPending_;
  }

  protected isShortcutError_(id: string): boolean {
    return !!this.shortcuts_?.errorLabel && this.shortcutErrorId_ === id;
  }

  protected isShortcutRecording_(id: string): boolean {
    return this.shortcutRecordingId_ === id;
  }

  protected shortcutKeysText_(command: ShortcutCommandItem): string {
    return command.keys.length ? command.keys.join('  ') :
                                 (this.shortcuts_?.labels.none || '');
  }

  private shortcutIdOf_(event: Event): string {
    return (event.currentTarget as HTMLElement).dataset['commandId'] || '';
  }

  protected onShortcutSearchInput_(event: Event) {
    this.shortcutQuery_ = (event.target as HTMLInputElement).value;
  }

  protected onShortcutChangeClick_(event: Event) {
    this.shortcutRecordingId_ = this.shortcutIdOf_(event);
    this.shortcutErrorId_ = '';
    (event.currentTarget as HTMLElement).focus();
  }

  protected onShortcutRecordKeydown_(event: KeyboardEvent) {
    const id = this.shortcutIdOf_(event);
    if (!id || this.shortcutRecordingId_ !== id) {
      return;
    }
    // The pressed key is taken by the editor, never by the page or browser.
    event.preventDefault();
    event.stopPropagation();
    if (['Shift', 'Control', 'Alt', 'Meta', 'CapsLock'].includes(event.key)) {
      return;
    }
    this.shortcutRecordingId_ = '';
    if (event.key === 'Escape' && !event.metaKey && !event.ctrlKey &&
        !event.altKey && !event.shiftKey) {
      return;
    }
    void this.runShortcutAction_(
        'set', {
          id,
          keyCode: event.keyCode,
          cmd: event.metaKey,
          ctrl: event.ctrlKey,
          alt: event.altKey,
          shift: event.shiftKey,
        },
        id);
  }

  protected onShortcutRecordBlur_() {
    this.shortcutRecordingId_ = '';
  }

  protected onShortcutUnbindClick_(event: Event) {
    const id = this.shortcutIdOf_(event);
    void this.runShortcutAction_('unbind', {id}, id);
  }

  protected onShortcutResetClick_(event: Event) {
    const id = this.shortcutIdOf_(event);
    void this.runShortcutAction_('reset', {id}, id);
  }

  protected onShortcutResetAllClick_() {
    void this.runShortcutAction_('resetAll', {}, '');
  }

  private applyLinkRoutingStatus_(status: LinkRoutingStatusResponse) {
    this.linkRouting_ = status;
    if (!status.error) {
      this.linkRoutingErrorRuleId_ = '';
    }
    const ids = status.workspaces.map(workspace => workspace.id);
    if (!ids.includes(this.linkRoutingDraft_.target)) {
      this.linkRoutingDraft_ = {...this.linkRoutingDraft_, target: ids[0] || ''};
    }
  }

  private async refreshLinkRouting_() {
    try {
      this.applyLinkRoutingStatus_(
          await sendWithPromise<LinkRoutingStatusResponse>(
              'ahoiGetLinkRouting'));
    } catch {
      this.linkRouting_ = null;
    }
  }

  private async runLinkRoutingAction_(
      action: string, payload: Record<string, unknown>,
      errorScope: string = ''): Promise<boolean> {
    if (this.linkRoutingPending_ || !this.linkRouting_?.canChange) {
      return false;
    }
    this.linkRoutingPending_ = true;
    try {
      const status = await sendWithPromise<LinkRoutingStatusResponse>(
          'ahoiLinkRoutingAction', action, payload);
      this.applyLinkRoutingStatus_(status);
      // A rejected edit is never dropped silently: the reason stays visible
      // next to the part that caused it until the next successful change.
      this.linkRoutingErrorRuleId_ = status.error ? errorScope : '';
      return status.action === 'saved';
    } catch {
      await this.refreshLinkRouting_();
      return false;
    } finally {
      this.linkRoutingPending_ = false;
      void this.resolveLinkRoutingExample_();
    }
  }

  protected linkRoutingRulePayload_(
      rule: LinkRoutingRule,
      overrides: Partial<LinkRoutingRule> = {}): Record<string, unknown> {
    const merged = {...rule, ...overrides};
    return {
      id: merged.id,
      enabled: merged.enabled,
      host: merged.host,
      includeSubdomains: merged.includeSubdomains,
      path: merged.path,
      target: merged.target,
      mode: merged.mode,
    };
  }

  private findLinkRoutingRule_(event: Event): LinkRoutingRule|undefined {
    const id = (event.currentTarget as HTMLElement).dataset['ruleId'];
    return this.linkRouting_?.rules.find(rule => rule.id === id);
  }

  protected onLinkRoutingEnabledChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const enabled = checkbox.checked;
    checkbox.checked = this.linkRouting_?.enabled ?? false;
    void this.runLinkRoutingAction_('setEnabled', {enabled});
  }

  protected onLinkRoutingRuleFieldChange_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    const input = event.currentTarget as HTMLInputElement | HTMLSelectElement;
    const field = input.dataset['field'] as keyof LinkRoutingRule | undefined;
    if (!rule || !field) {
      return;
    }
    const value = input instanceof HTMLInputElement && input.type === 'checkbox' ?
        input.checked :
        input.value;
    if (input instanceof HTMLInputElement && input.type === 'checkbox') {
      // Keep the stored state until the backend confirms the change.
      input.checked = rule[field] as boolean;
    }
    void this.runLinkRoutingAction_(
        'updateRule', this.linkRoutingRulePayload_(rule, {[field]: value}),
        rule.id);
  }

  protected onLinkRoutingRuleDeleteClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_('deleteRule', {id: rule.id}, rule.id);
    }
  }

  protected onLinkRoutingRuleMoveUpClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_(
          'moveRule', {id: rule.id, delta: -1}, rule.id);
    }
  }

  protected onLinkRoutingRuleMoveDownClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_(
          'moveRule', {id: rule.id, delta: 1}, rule.id);
    }
  }

  protected isLinkRoutingLocked_(): boolean {
    return !this.linkRouting_?.canChange || this.linkRoutingPending_;
  }

  protected isLinkRoutingError_(scope: string): boolean {
    return !!this.linkRouting_?.error &&
        this.linkRoutingErrorRuleId_ === scope;
  }

  protected onLinkRoutingDraftInput_(event: Event) {
    this.onLinkRoutingDraftChange_(event);
  }

  protected onLinkRoutingDraftChange_(event: Event) {
    const input = event.currentTarget as HTMLInputElement | HTMLSelectElement;
    const field = input.dataset['field'] as keyof LinkRoutingDraft | undefined;
    if (!field) {
      return;
    }
    const value = input instanceof HTMLInputElement && input.type === 'checkbox' ?
        input.checked :
        input.value;
    this.linkRoutingDraft_ = {...this.linkRoutingDraft_, [field]: value};
  }

  protected async onLinkRoutingAddClick_() {
    const saved = await this.runLinkRoutingAction_(
        'addRule', {...this.linkRoutingDraft_, enabled: true}, 'new');
    if (saved) {
      this.linkRoutingDraft_ = {
        ...this.linkRoutingDraft_,
        host: '',
        path: '',
        includeSubdomains: false,
      };
    }
  }

  protected onLinkRoutingDefaultChange_(event: Event) {
    const select = event.currentTarget as HTMLSelectElement;
    const current = this.linkRouting_?.defaultRoute;
    if (!current) {
      return;
    }
    const payload = {target: current.target, mode: current.mode};
    if (select.dataset['field'] === 'mode') {
      payload.mode = select.value as LinkRoutingMode;
    } else {
      payload.target = select.value;
    }
    void this.runLinkRoutingAction_('setDefault', payload, 'default');
  }

  protected async onLinkRoutingResetClick_() {
    if (!this.linkRoutingResetArmed_) {
      this.linkRoutingResetArmed_ = true;
      return;
    }
    this.linkRoutingResetArmed_ = false;
    await this.runLinkRoutingAction_('reset', {});
  }

  protected onLinkRoutingExampleInput_(event: Event) {
    this.linkRoutingExampleInput_ =
        (event.currentTarget as HTMLInputElement).value;
    void this.resolveLinkRoutingExample_();
  }

  private async resolveLinkRoutingExample_() {
    const sequence = ++this.linkRoutingExampleSequence_;
    const input = this.linkRoutingExampleInput_;
    if (!input.trim()) {
      this.linkRoutingExample_ = null;
      return;
    }
    try {
      const result = await sendWithPromise<LinkRoutingExampleResponse>(
          'ahoiResolveLinkRoutingExample', input);
      if (sequence === this.linkRoutingExampleSequence_) {
        this.linkRoutingExample_ = result;
      }
    } catch {
      if (sequence === this.linkRoutingExampleSequence_) {
        this.linkRoutingExample_ = null;
      }
    }
  }

  protected linkRoutingWorkspaceLabel_(
      workspace: {name: string, separated: boolean}): string {
    return workspace.separated ?
        `${workspace.name} (${this.linkRouting_?.labels.separated || ''})` :
        workspace.name;
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

  protected async onPortableImportClick_() {
    if (this.portableImportPending_ || this.portableExportPending_) {
      return;
    }
    this.portableImportPending_ = true;
    this.portableImportPreview_ = null;
    this.portableImportSelectedWorkspaceIds_ = [];
    this.portableImportStatus_ = '';
    try {
      const result = await sendWithPromise<{status: string}>(
          'ahoiOpenPortableImport');
      if (result.status !== 'dialog') {
        this.portableImportPending_ = false;
        this.portableImportStatus_ = 'failed';
      }
    } catch {
      this.portableImportPending_ = false;
      this.portableImportStatus_ = 'failed';
    }
  }

  protected onPortableImportWorkspaceChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const id = checkbox.dataset['workspaceId'];
    if (!id) {
      return;
    }
    const selected = new Set(this.portableImportSelectedWorkspaceIds_);
    checkbox.checked ? selected.add(id) : selected.delete(id);
    this.portableImportSelectedWorkspaceIds_ = [...selected];
  }

  protected canCommitPortableImport_(): boolean {
    if (this.portableImportPending_ || !this.portableImportPreview_?.token ||
        !this.portableImportPreview_.canImport ||
        this.portableImportSelectedWorkspaceIds_.length === 0) {
      return false;
    }
    const selected = new Set(this.portableImportSelectedWorkspaceIds_);
    const chosen = this.portableImportPreview_.workspaces?.filter(
        workspace => selected.has(workspace.id)) || [];
    return selected.size === this.portableImportSelectedWorkspaceIds_.length &&
        chosen.length === selected.size &&
        chosen.every(workspace => workspace.destination !== 'conflict');
  }

  protected async onPortableCommitClick_() {
    if (!this.canCommitPortableImport_()) {
      return;
    }
    this.portableImportPending_ = true;
    this.portableImportStatus_ = '';
    try {
      const result = await sendWithPromise<{status: string}>(
          'ahoiCommitPortableImport', this.portableImportPreview_!.token,
          this.portableImportSelectedWorkspaceIds_);
      this.portableImportStatus_ = result.status;
      this.portableImportPreview_ = null;
      this.portableImportSelectedWorkspaceIds_ = [];
      if (result.status === 'imported' || result.status === 'noChanges') {
        this.portableExportPreview_ = null;
        void this.refreshPortableExportOptions_();
      }
    } catch {
      this.portableImportStatus_ = 'commitFailed';
    } finally {
      this.portableImportPending_ = false;
    }
  }

  protected portableImportStatusText_(): string {
    const labels = this.portableExportOptions_?.labels;
    switch (this.portableImportStatus_) {
      case 'preview':
        return labels?.importReady || '';
      case 'cancelled':
        return labels?.cancelled || '';
      case 'failed':
        return labels?.importFailed || '';
      case 'targetUnavailable':
        return labels?.importTargetUnavailable || '';
      case 'imported':
        return labels?.imported || '';
      case 'noChanges':
        return labels?.noChanges || '';
      case 'conflict':
        return labels?.importChanged || '';
      case 'commitFailed':
        return labels?.importCommitFailed || '';
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
