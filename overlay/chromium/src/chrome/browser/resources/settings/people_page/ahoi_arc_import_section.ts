// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_checkbox/cr_checkbox.js';

import {sendWithPromise} from 'chrome://resources/js/cr.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {loadTimeData} from '../i18n_setup.js';

import {getCss} from './ahoi_arc_import_section.css.js';
import {getHtml} from './ahoi_arc_import_section.html.js';

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
  unreachableItems: number;
  deduplicatedWorkspaces: number;
  deduplicatedItems: number;
  deduplicatedSplits: number;
  // Pinned top-level folders that can become workspaces (source statistic).
  topLevelFolders?: number;
  // Workspaces planned from such folders; zero unless the option is on.
  folderWorkspaces?: number;
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
  // The layout this preview was built for. Commit must repeat it.
  foldersAsWorkspaces?: boolean;
  // At least one Arc profile has browsing history to offer.
  historyAvailable?: boolean;
}

// Counters of the separately selectable history category; never URLs.
export interface ArcHistoryImportResponse {
  selected: boolean;
  status: string;
  added: number;
  deduplicated: number;
  expired: number;
  excluded: number;
}

export interface ArcImportCommitResponse {
  status: string;
  stats: ArcImportStats;
  renamedWorkspaces: number;
  skippedWorkspaces: number;
  mergedWorkspaces: number;
  reconstructedSplits: number;
  approximatedFourPaneRatios: number;
  history?: ArcHistoryImportResponse;
}

// Fallbacks until the proposed Settings strings are part of a strings patch
// (see ahoiArcImportFoldersAsWorkspaces). loadTimeData wins when present.
const ARC_HISTORY_FALLBACK_TEXT: {[key: string]: {de: string, en: string}} = {
  ahoiArcImportHistoryCategory: {
    de: 'Browserverlauf (ein Eintrag pro Seite)',
    en: 'Browsing history (one entry per page)',
  },
  ahoiArcImportPrivacySublabelWithHistory: {
    de: 'Passwörter, Cookies, Formulardaten, Erweiterungsdaten, Zugangsdaten ' +
        'in URLs, lokale Dateien und nicht unterstützte Arc-Elemente werden ' +
        'nicht importiert. Der Browserverlauf wird nur übernommen, wenn du ' +
        'ihn auswählst.',
    en: 'Passwords, cookies, form data, extension state, credentials in ' +
        'URLs, local files, and unsupported Arc items are not imported. ' +
        'Browsing history is imported only when you select it.',
  },
  ahoiArcImportHistorySuccess: {
    de: 'Der Arc-Verlauf wurde importiert.',
    en: 'Arc browsing history was imported.',
  },
  ahoiArcImportHistoryAdded: {
    de: 'Verlauf: neue Seiten',
    en: 'History: new pages',
  },
  ahoiArcImportHistoryPresent: {
    de: 'Verlauf: bereits vorhanden',
    en: 'History: already present',
  },
  ahoiArcImportHistorySkipped: {
    de: 'Verlauf: zu alt oder ausgeschlossen',
    en: 'History: too old or excluded',
  },
  ahoiArcImportHistoryPending: {
    de: 'Der Verlauf wurde nicht vollständig bestätigt. Er wird beim ' +
        'nächsten Start des Arc-Imports sicher abgeschlossen.',
    en: 'Browsing history was not fully confirmed. It is completed safely ' +
        'the next time you start the Arc import.',
  },
  ahoiArcImportHistoryFailed: {
    de: 'Der Verlauf konnte nicht importiert werden. Dein Verlauf in ' +
        'AhoiBrowser ist unverändert.',
    en: 'Browsing history could not be imported. Your AhoiBrowser history ' +
        'is unchanged.',
  },
};

type ArcImportStage = 'idle'|'discovering'|'preview'|'committing'|'recovering'|
    'recovered'|'sourceInUse'|'done'|'error';

export class SettingsAhoiArcImportSectionElement extends CrLitElement {
  static get is() {
    return 'settings-ahoi-arc-import-section';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      arcImportStage_: {type: String},
      arcImportPreview_: {type: Object},
      arcImportResult_: {type: Object},
      arcConflictPolicy_: {type: String},
      arcImportSidebar_: {type: Boolean},
      arcReconstructSplits_: {type: Boolean},
      arcFoldersAsWorkspaces_: {type: Boolean},
      arcImportHistory_: {type: Boolean},
      arcSelectedProfiles_: {type: Array},
    };
  }

  protected accessor arcImportStage_: ArcImportStage = 'idle';
  protected accessor arcImportPreview_: ArcImportPreviewResponse|null = null;
  protected accessor arcImportResult_: ArcImportCommitResponse|null = null;
  protected accessor arcConflictPolicy_: string = 'rename';
  protected accessor arcImportSidebar_: boolean = true;
  protected accessor arcReconstructSplits_: boolean = false;
  protected accessor arcFoldersAsWorkspaces_: boolean = false;
  // Selected by default when offered, like history in the standard import.
  protected accessor arcImportHistory_: boolean = true;
  protected accessor arcSelectedProfiles_: string[] = [];

  isComplete(): boolean {
    return this.arcImportStage_ === 'done';
  }

  protected onArcDiscoverClick_() {
    return this.discoverArc_(/*keepChoices=*/ false);
  }

  // The folders-as-workspaces layout changes the plan, its identities and its
  // transaction key. A fresh preview is therefore required for every change.
  private async discoverArc_(keepChoices: boolean) {
    if (this.isArcBusy_()) {
      return;
    }
    const previousProfiles = this.arcSelectedProfiles_;
    const previousSplits = this.arcReconstructSplits_;
    const previousHistory = this.arcImportHistory_;
    this.arcImportStage_ = 'discovering';
    this.arcImportPreview_ = null;
    this.arcImportResult_ = null;
    this.notifyComplete_(false);
    this.notifyBusy_(true);
    try {
      const preview = await sendWithPromise<ArcImportPreviewResponse>(
          'ahoiArcDiscover', this.arcFoldersAsWorkspaces_);
      if (!this.isConnected) {
        return;
      }
      this.arcImportPreview_ = preview;
      if (preview.status === 'ok') {
        // Recovery previews use the default layout; adopt what was planned.
        this.arcFoldersAsWorkspaces_ = !!preview.foldersAsWorkspaces;
      }
      this.arcSelectedProfiles_ = keepChoices ?
          preview.profiles.filter(
              profile => previousProfiles.includes(profile)) :
          [...preview.profiles];
      this.arcReconstructSplits_ =
          preview.stats.splits > 0 && (!keepChoices || previousSplits);
      this.arcImportHistory_ = !keepChoices || previousHistory;
      this.arcImportStage_ = preview.status === 'ok' ?
          'preview' :
          (preview.status === 'sourceInUse' ? 'sourceInUse' : 'error');
    } catch {
      if (this.isConnected) {
        this.arcImportStage_ = 'error';
      }
    } finally {
      if (this.isConnected) {
        this.notifyBusy_(false);
      }
    }
  }

  protected async onArcCommitClick_() {
    const preview = this.arcImportPreview_;
    if (!preview || !this.canCommitArcImport_()) {
      return;
    }
    // This primary action confirms the displayed plan and its mandatory
    // backup together. Lock synchronously so a second click cannot submit it.
    this.arcImportStage_ = 'committing';
    this.notifyBusy_(true);
    try {
      const result = await sendWithPromise<ArcImportCommitResponse>(
          'ahoiArcCommit', preview.snapshotToken, this.arcConflictPolicy_,
          this.arcSelectedProfiles_, this.arcImportSidebar_,
          this.arcReconstructSplits_,
          /*backupConfirmed=*/ true, /*commitConfirmed=*/ true,
          !!preview.foldersAsWorkspaces, this.isArcHistorySelected_());
      if (!this.isConnected) {
        return;
      }
      this.arcImportResult_ = result;
      this.arcImportStage_ =
          result.status === 'ok' || result.status === 'noChanges' ?
          'done' :
          (result.status === 'sourceInUse' ? 'sourceInUse' : 'error');
      this.notifyComplete_(this.arcImportStage_ === 'done');
    } catch {
      if (this.isConnected) {
        this.arcImportStage_ = 'error';
      }
    } finally {
      if (this.isConnected) {
        this.notifyBusy_(false);
      }
    }
  }

  protected isArcBusy_(): boolean {
    return this.arcImportStage_ === 'discovering' ||
        this.arcImportStage_ === 'committing' ||
        this.arcImportStage_ === 'recovering';
  }

  protected showArcRecovery_(): boolean {
    return this.arcImportStage_ === 'recovering' ||
        (this.arcImportStage_ === 'error' &&
         (this.arcImportResult_?.status ?? this.arcImportPreview_?.status) ===
             'recoveryRequired');
  }

  protected async onArcRecoverClick_() {
    if (!this.showArcRecovery_() || this.isArcBusy_()) {
      return;
    }
    this.arcImportStage_ = 'recovering';
    this.notifyComplete_(false);
    this.notifyBusy_(true);
    try {
      const result = await sendWithPromise<ArcImportPreviewResponse>(
          'ahoiArcRecover', /*recoveryConfirmed=*/ true);
      if (!this.isConnected) {
        return;
      }
      this.arcImportResult_ = null;
      this.arcImportPreview_ = result;
      this.arcImportStage_ = result.status === 'ok' ? 'recovered' : 'error';
      // Recovery is not an import. No auto-discovery, auto-retry or "done".
    } catch {
      if (this.isConnected) {
        this.arcImportStage_ = 'error';
      }
    } finally {
      if (this.isConnected) {
        this.notifyBusy_(false);
      }
    }
  }

  protected onArcProfileChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLElement & {checked: boolean};
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
        (event.currentTarget as HTMLElement & {checked: boolean}).checked;
  }

  protected onArcImportHistoryChange_(event: Event) {
    this.arcImportHistory_ =
        (event.currentTarget as HTMLElement & {checked: boolean}).checked;
  }

  protected showArcHistory_(): boolean {
    return !!this.arcImportPreview_?.historyAvailable;
  }

  protected isArcHistorySelected_(): boolean {
    return this.showArcHistory_() && this.arcImportHistory_;
  }

  protected arcHistoryText_(key: string): string {
    if (loadTimeData.valueExists(key)) {
      return loadTimeData.getString(key);
    }
    const fallback = ARC_HISTORY_FALLBACK_TEXT[key];
    if (!fallback) {
      return '';
    }
    return document.documentElement.lang.startsWith('de') ? fallback.de :
                                                             fallback.en;
  }

  protected showArcHistoryResult_(): boolean {
    return (this.arcImportStage_ === 'done' ||
            this.arcImportStage_ === 'error') &&
        !!this.arcImportResult_?.history?.selected;
  }

  protected arcHistoryFailed_(): boolean {
    const status = this.arcImportResult_?.history?.status;
    return status !== undefined && status !== 'ok' && status !== 'noChanges';
  }

  protected arcHistoryFailureText_(): string {
    return this.arcHistoryText_(
        this.arcImportResult_?.history?.status === 'recoveryRequired' ?
            'ahoiArcImportHistoryPending' :
            'ahoiArcImportHistoryFailed');
  }

  protected arcPrivacySublabel_(): string {
    return this.showArcHistory_() ?
        this.arcHistoryText_('ahoiArcImportPrivacySublabelWithHistory') :
        loadTimeData.getString('ahoiArcImportPrivacySublabel');
  }

  protected onArcReconstructSplitsChange_(event: Event) {
    this.arcReconstructSplits_ =
        (event.currentTarget as HTMLElement & {checked: boolean}).checked;
  }

  protected async onArcFoldersAsWorkspacesChange_(event: Event) {
    const checked =
        (event.currentTarget as HTMLElement & {checked: boolean}).checked;
    if (checked === this.arcFoldersAsWorkspaces_) {
      return;
    }
    this.arcFoldersAsWorkspaces_ = checked;
    await this.discoverArc_(/*keepChoices=*/ true);
  }

  protected showArcFoldersAsWorkspaces_(): boolean {
    return (this.arcImportPreview_?.stats.topLevelFolders ?? 0) > 0;
  }

  // TODO: Replace the fallback once the proposed Settings string
  // `ahoiArcImportFoldersAsWorkspaces` is part of the integration patch.
  protected arcFoldersAsWorkspacesLabel_(): string {
    if (loadTimeData.valueExists('ahoiArcImportFoldersAsWorkspaces')) {
      return loadTimeData.getString('ahoiArcImportFoldersAsWorkspaces');
    }
    return document.documentElement.lang.startsWith('de') ?
        'Hauptordner als Workspaces anlegen' :
        'Create workspaces from top-level folders';
  }

  protected canCommitArcImport_(): boolean {
    return this.arcImportStage_ === 'preview' &&
        (this.arcImportSidebar_ || this.isArcHistorySelected_()) &&
        this.arcSelectedProfiles_.length > 0 &&
        this.arcImportPreview_?.status === 'ok' &&
        !!this.arcImportPreview_?.foldersAsWorkspaces ===
        this.arcFoldersAsWorkspaces_;
  }

  protected excludedItemCount_(stats: ArcImportStats): number {
    return stats.unsafeUrls + stats.unsupportedItems + stats.unreachableItems;
  }

  protected deduplicatedItemCount_(stats: ArcImportStats): number {
    return stats.deduplicatedWorkspaces + stats.deduplicatedItems +
        stats.deduplicatedSplits;
  }

  protected arcStatusText_(): string {
    switch (this.arcImportStage_) {
      case 'discovering':
        return loadTimeData.getString('ahoiArcImportDiscovering');
      case 'committing':
        return loadTimeData.getString('ahoiArcImportCommitting');
      case 'recovering':
        return loadTimeData.getString('ahoiArcImportRecovering');
      case 'recovered':
        return loadTimeData.getString('ahoiArcImportRecovered');
      case 'sourceInUse':
        return loadTimeData.getString('ahoiArcImportSourceInUse');
      case 'done':
        if (this.arcImportResult_?.status === 'ok' &&
            !this.arcImportSidebar_ &&
            this.arcImportResult_?.history?.selected) {
          return this.arcHistoryText_('ahoiArcImportHistorySuccess');
        }
        return loadTimeData.getString(
            this.arcImportResult_?.status === 'noChanges' ?
                'ahoiArcImportNoChanges' :
                'ahoiArcImportSuccess');
      case 'error':
        return loadTimeData.getString(this.arcErrorStatusKey_());
      default:
        return '';
    }
  }

  private arcErrorStatusKey_(): string {
    const status =
        this.arcImportResult_?.status ?? this.arcImportPreview_?.status ?? '';
    switch (status) {
      case 'notFound':
        return 'ahoiArcImportNotFound';
      case 'noImportableWorkspaces':
        return 'ahoiArcImportNoSafeProfiles';
      case 'sourceChanged':
      case 'stalePreview':
        return 'ahoiArcImportSourceChanged';
      case 'insufficientDiskSpace':
        return 'ahoiArcImportInsufficientDiskSpace';
      case 'backupQuotaExceeded':
        return 'ahoiArcImportBackupQuotaExceeded';
      case 'recoveryRequired':
        return 'ahoiArcImportRecoveryRequired';
      case 'limitExceeded':
      case 'invalidJson':
      case 'unsupportedSchema':
      case 'missingRequiredField':
      case 'malformedSerializedMap':
      case 'duplicateIdentifier':
      case 'graphViolation':
      case 'invalidText':
        return 'ahoiArcImportUnsupportedData';
      default:
        return 'ahoiArcImportError';
    }
  }

  private notifyBusy_(busy: boolean) {
    this.fire('ahoi-arc-import-busy-changed', {busy});
  }

  private notifyComplete_(complete: boolean) {
    this.fire('ahoi-arc-import-complete', {complete});
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-ahoi-arc-import-section': SettingsAhoiArcImportSectionElement;
  }
}

customElements.define(
    SettingsAhoiArcImportSectionElement.is,
    SettingsAhoiArcImportSectionElement);
