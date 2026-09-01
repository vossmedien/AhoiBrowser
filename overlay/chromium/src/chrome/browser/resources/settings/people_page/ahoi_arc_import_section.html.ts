// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAhoiArcImportSectionElement} from './ahoi_arc_import_section.js';

export function getHtml(this: SettingsAhoiArcImportSectionElement) {
  return html`<!--_html_template_start_-->
    <section id="ahoiArcImportSurface"
        aria-labelledby="ahoiArcImportHeading">
      <div id="ahoiArcImportHeading" class="section-title">
        $i18n{ahoiArcImportTitle}
      </div>
      <div class="secondary">$i18n{ahoiArcImportDescription}</div>
      <cr-button id="ahoiArcDiscover" class="action-button"
          ?disabled="${this.arcImportStage_ === 'discovering' ||
              this.arcImportStage_ === 'committing'}"
          @click="${this.onArcDiscoverClick_}">
        $i18n{ahoiArcImportDiscover}
      </cr-button>

      <div id="ahoiArcImportStatus" class="status"
          role="status" aria-live="polite">
        ${this.arcStatusText_()}
      </div>

      ${this.arcImportStage_ === 'sourceInUse' ? html`
        <div class="warning" role="alert">
          <div>$i18n{ahoiArcImportCloseArcTitle}</div>
          <div class="secondary">$i18n{ahoiArcImportCloseArcSublabel}</div>
        </div>
      ` : ''}

      ${this.arcImportPreview_ && this.arcImportStage_ === 'preview' ? html`
        <section class="preview" aria-labelledby="ahoiArcPreviewHeading">
          <div id="ahoiArcPreviewHeading" class="subheading">
            $i18n{ahoiArcImportPreview}
          </div>
          <ul class="counts">
            <li><span class="count-label">$i18n{ahoiArcImportWorkspaces}</span>
              <span class="count-value">
                ${this.arcImportPreview_.stats.workspaces}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportFolders}</span>
              <span class="count-value">
                ${this.arcImportPreview_.stats.folders}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportPages}</span>
              <span class="count-value">
                ${this.arcImportPreview_.stats.pages}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportSplits}</span>
              <span class="count-value">
                ${this.arcImportPreview_.stats.splits}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportDegradations}</span>
              <span class="count-value">
                ${this.arcImportPreview_.stats.degradedSplits}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportExcluded}</span>
              <span class="count-value">
                ${this.excludedItemCount_(this.arcImportPreview_.stats)}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportDeduplicated}</span>
              <span class="count-value">
                ${this.deduplicatedItemCount_(this.arcImportPreview_.stats)}
              </span></li>
          </ul>

          <div class="subheading">$i18n{ahoiArcImportTargets}</div>
          <ul class="targets">
            ${this.arcImportPreview_.targetWorkspaces.map(
                workspace => html`<li>${workspace}</li>`)}
          </ul>

          <fieldset class="options">
            <legend>$i18n{ahoiArcImportProfiles}</legend>
            ${this.arcImportPreview_.profiles.map(profile => html`
              <cr-checkbox data-profile="${profile}"
                  .checked="${this.arcSelectedProfiles_.includes(profile)}"
                  @change="${this.onArcProfileChange_}">
                ${profile}
              </cr-checkbox>
            `)}
          </fieldset>

          <fieldset class="options">
            <legend>$i18n{ahoiArcImportCategories}</legend>
            <cr-checkbox id="ahoiArcImportSidebar"
                .checked="${this.arcImportSidebar_}"
                @change="${this.onArcImportSidebarChange_}">
              $i18n{ahoiArcImportSidebarCategory}
            </cr-checkbox>
            ${this.arcImportPreview_.stats.splits > 0 ? html`
              <cr-checkbox id="ahoiArcReconstructSplits"
                  .checked="${this.arcReconstructSplits_}"
                  @change="${this.onArcReconstructSplitsChange_}">
                $i18n{ahoiArcImportSplitCategory}
              </cr-checkbox>
            ` : ''}
          </fieldset>

          <label class="select-label" for="ahoiArcConflictPolicy">
            $i18n{ahoiArcImportConflicts}
          </label>
          <select id="ahoiArcConflictPolicy" class="md-select"
              .value="${this.arcConflictPolicy_}"
              @change="${this.onArcConflictPolicyChange_}">
            <option value="rename">$i18n{ahoiArcImportConflictRename}</option>
            <option value="skip">$i18n{ahoiArcImportConflictSkip}</option>
            <option value="merge">$i18n{ahoiArcImportConflictMerge}</option>
          </select>
          <div class="secondary">
            $i18n{ahoiArcImportConflictCount}
            ${this.arcImportPreview_.conflictingWorkspaces}
          </div>

          <div class="exclusions" role="note">
            <div>$i18n{ahoiArcImportPrivacyTitle}</div>
            <div class="secondary">$i18n{ahoiArcImportPrivacySublabel}</div>
          </div>
          <cr-checkbox id="ahoiArcBackupConfirmation"
              .checked="${this.arcBackupConfirmed_}"
              @change="${this.onArcBackupConfirmedChange_}">
            $i18n{ahoiArcImportBackupConfirmation}
          </cr-checkbox>
          <cr-checkbox id="ahoiArcCommitConfirmation"
              .checked="${this.arcCommitConfirmed_}"
              @change="${this.onArcCommitConfirmedChange_}">
            $i18n{ahoiArcImportCommitConfirmation}
          </cr-checkbox>
          <cr-button id="ahoiArcCommit" class="action-button"
              ?disabled="${!this.canCommitArcImport_()}"
              @click="${this.onArcCommitClick_}">
            $i18n{ahoiArcImportCommit}
          </cr-button>
        </section>
      ` : ''}

      ${this.arcImportStage_ === 'done' && this.arcImportResult_ ? html`
        <section class="result" role="status"
            aria-labelledby="ahoiArcImportResultHeading">
          <div id="ahoiArcImportResultHeading" class="subheading">
            ${this.arcStatusText_()}
          </div>
          <ul class="counts result-counts">
            <li><span class="count-label">$i18n{ahoiArcImportWorkspaces}</span>
              <span id="ahoiArcResultWorkspaces" class="count-value">
                ${this.arcImportResult_.stats.workspaces}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportFolders}</span>
              <span class="count-value">
                ${this.arcImportResult_.stats.folders}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportPages}</span>
              <span class="count-value">
                ${this.arcImportResult_.stats.pages}
              </span></li>
            <li>
              <span class="count-label">
                $i18n{ahoiArcImportResultSkippedWorkspaces}
              </span>
              <span id="ahoiArcResultSkipped" class="count-value">
                ${this.arcImportResult_.skippedWorkspaces}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportDegradations}</span>
              <span id="ahoiArcResultDegraded" class="count-value">
                ${this.arcImportResult_.stats.degradedSplits}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportExcluded}</span>
              <span id="ahoiArcResultExcluded" class="count-value">
                ${this.excludedItemCount_(this.arcImportResult_.stats)}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportDeduplicated}</span>
              <span id="ahoiArcResultDeduplicated" class="count-value">
                ${this.deduplicatedItemCount_(this.arcImportResult_.stats)}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportResultSplits}</span>
              <span class="count-value">
                ${this.arcImportResult_.reconstructedSplits}
              </span></li>
            <li><span class="count-label">$i18n{ahoiArcImportResultFourPane}</span>
              <span id="ahoiArcResultFourPane" class="count-value">
                ${this.arcImportResult_.approximatedFourPaneRatios}
              </span></li>
          </ul>
        </section>
      ` : ''}
    </section>
  <!--_html_template_end_-->`;
}
