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

type ArcImportStage =
    'idle'|'discovering'|'preview'|'committing'|'sourceInUse'|'done'|'error';

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
      arcSelectedProfiles_: {type: Array},
      arcBackupConfirmed_: {type: Boolean},
      arcCommitConfirmed_: {type: Boolean},
    };
  }

  protected accessor arcImportStage_: ArcImportStage = 'idle';
  protected accessor arcImportPreview_: ArcImportPreviewResponse|null = null;
  protected accessor arcImportResult_: ArcImportCommitResponse|null = null;
  protected accessor arcConflictPolicy_: string = 'rename';
  protected accessor arcImportSidebar_: boolean = true;
  protected accessor arcReconstructSplits_: boolean = false;
  protected accessor arcSelectedProfiles_: string[] = [];
  protected accessor arcBackupConfirmed_: boolean = false;
  protected accessor arcCommitConfirmed_: boolean = false;

  isComplete(): boolean {
    return this.arcImportStage_ === 'done';
  }

  protected async onArcDiscoverClick_() {
    this.arcImportStage_ = 'discovering';
    this.arcImportPreview_ = null;
    this.arcImportResult_ = null;
    this.arcBackupConfirmed_ = false;
    this.arcCommitConfirmed_ = false;
    this.notifyComplete_(false);
    this.notifyBusy_(true);
    try {
      const preview = await sendWithPromise<ArcImportPreviewResponse>(
          'ahoiArcDiscover');
      if (!this.isConnected) {
        return;
      }
      this.arcImportPreview_ = preview;
      this.arcSelectedProfiles_ = [...preview.profiles];
      this.arcReconstructSplits_ = preview.stats.splits > 0;
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
    this.arcImportStage_ = 'committing';
    this.notifyBusy_(true);
    try {
      const result = await sendWithPromise<ArcImportCommitResponse>(
          'ahoiArcCommit', preview.snapshotToken, this.arcConflictPolicy_,
          this.arcSelectedProfiles_, this.arcImportSidebar_,
          this.arcReconstructSplits_, this.arcBackupConfirmed_,
          this.arcCommitConfirmed_);
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

  protected onArcProfileChange_(event: Event) {
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
      case 'sourceInUse':
        return loadTimeData.getString('ahoiArcImportSourceInUse');
      case 'done':
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
    const status = this.arcImportResult_?.status ??
        this.arcImportPreview_?.status ?? '';
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
