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
          <dl class="counts">
            <div><dt>$i18n{ahoiArcImportWorkspaces}</dt>
              <dd>${this.arcImportPreview_.stats.workspaces}</dd></div>
            <div><dt>$i18n{ahoiArcImportFolders}</dt>
              <dd>${this.arcImportPreview_.stats.folders}</dd></div>
            <div><dt>$i18n{ahoiArcImportPages}</dt>
              <dd>${this.arcImportPreview_.stats.pages}</dd></div>
            <div><dt>$i18n{ahoiArcImportSplits}</dt>
              <dd>${this.arcImportPreview_.stats.splits}</dd></div>
            <div><dt>$i18n{ahoiArcImportDegradations}</dt>
              <dd>${this.arcImportPreview_.stats.degradedSplits}</dd></div>
            <div><dt>$i18n{ahoiArcImportExcluded}</dt>
              <dd>${this.excludedItemCount_(this.arcImportPreview_.stats)}</dd>
            </div>
          </dl>

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
          <dl class="counts result-counts">
            <div><dt>$i18n{ahoiArcImportWorkspaces}</dt>
              <dd id="ahoiArcResultWorkspaces">
                ${this.arcImportResult_.stats.workspaces}
              </dd></div>
            <div><dt>$i18n{ahoiArcImportFolders}</dt>
              <dd>${this.arcImportResult_.stats.folders}</dd></div>
            <div><dt>$i18n{ahoiArcImportPages}</dt>
              <dd>${this.arcImportResult_.stats.pages}</dd></div>
            <div><dt>$i18n{ahoiArcImportResultSkippedWorkspaces}</dt>
              <dd id="ahoiArcResultSkipped">
                ${this.arcImportResult_.skippedWorkspaces}
              </dd></div>
            <div><dt>$i18n{ahoiArcImportDegradations}</dt>
              <dd id="ahoiArcResultDegraded">
                ${this.arcImportResult_.stats.degradedSplits}
              </dd></div>
            <div><dt>$i18n{ahoiArcImportExcluded}</dt>
              <dd id="ahoiArcResultExcluded">
                ${this.excludedItemCount_(this.arcImportResult_.stats)}
              </dd>
            </div>
            <div><dt>$i18n{ahoiArcImportResultSplits}</dt>
              <dd>${this.arcImportResult_.reconstructedSplits}</dd></div>
            <div><dt>$i18n{ahoiArcImportResultFourPane}</dt>
              <dd id="ahoiArcResultFourPane">
                ${this.arcImportResult_.approximatedFourPaneRatios}
              </dd></div>
          </dl>
        </section>
      ` : ''}
    </section>
  <!--_html_template_end_-->`;
}
