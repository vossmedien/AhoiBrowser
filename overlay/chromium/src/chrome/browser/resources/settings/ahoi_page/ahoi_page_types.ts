// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Response and draft shapes exchanged between the Ahoi settings page and its
// C++ handler (split from ahoi_page.ts, source line budget).

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
