// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAhoiPageElement} from './ahoi_page.js';

export function getHtml(this: SettingsAhoiPageElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<cr-view-manager id="viewManager" class="cr-centered-card-container">
  <div slot="view" id="parent" route-path="${this.routes_.AHOI.path}">
    <settings-section page-title="$i18n{ahoiPageTitle}">
      <div class="section-heading cr-row">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiAppearanceSection}</div>
          <div class="secondary">$i18n{ahoiAppearanceSectionSublabel}</div>
        </div>
      </div>
<if expr="not is_chromeos">
      <div id="ahoiAccentPickerRow" class="cr-row hr"
          style="align-items: start; flex-direction: column;
              padding-bottom: 16px; padding-top: 12px;">
        <div class="cr-padded-text">
          <div>$i18n{ahoiGlobalPrimaryColor}</div>
          <div class="secondary">$i18n{ahoiGlobalPrimaryColorSublabel}</div>
        </div>
        <cr-theme-color-picker columns="6"
            style="margin-top: 12px; width: 100%;">
        </cr-theme-color-picker>
      </div>
</if>
      <settings-toggle-button id="ahoiGlassEnabled"
          pref-key="ahoi.appearance.glass_enabled"
          label="$i18n{ahoiGlassEnabled}"
          sub-label="$i18n{ahoiGlassEnabledSublabel}">
      </settings-toggle-button>
      <settings-toggle-button id="ahoiSidebarPageTintEnabled"
          pref-key="ahoi.appearance.sidebar_page_tint_enabled"
          label="$i18n{ahoiSidebarPageTintEnabled}"
          sub-label="$i18n{ahoiSidebarPageTintEnabledSublabel}">
      </settings-toggle-button>
      <div class="cr-row hr">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiFloatingNavigation}</div>
          <div class="secondary">$i18n{ahoiFloatingNavigationSublabel}</div>
        </div>
      </div>
      <div class="list-frame indented-toggles">
        <settings-toggle-button id="ahoiNavigationAutoHide"
            pref-key="ahoi.navigation.floating_auto_hide_enabled"
            label="$i18n{ahoiNavigationAutoHide}">
        </settings-toggle-button>
        <settings-toggle-button id="ahoiNavigationRevealNotch"
            ?hidden="${!this.floatingNavigationAutoHideEnabledPref_?.value}"
            pref-key="ahoi.navigation.floating_reveal_notch_enabled"
            label="$i18n{ahoiNavigationRevealNotch}">
        </settings-toggle-button>
        <div class="cr-row continuation"
            ?hidden="${!this.floatingNavigationAutoHideEnabledPref_?.value}">
          <div class="flex cr-padded-text" aria-hidden="true">
            $i18n{ahoiNavigationAutoHideDelay}
          </div>
          <settings-dropdown-menu id="ahoiNavigationAutoHideDelay"
              label="$i18n{ahoiNavigationAutoHideDelay}"
              pref-key="ahoi.navigation.floating_auto_hide_delay_ms"
              .menuOptions="${this.floatingNavigationDelayOptions_}">
          </settings-dropdown-menu>
        </div>
      </div>

      <div class="section-heading cr-row hr">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiArcImportSection}</div>
          <div class="secondary">$i18n{ahoiArcImportSectionSublabel}</div>
        </div>
      </div>
      <section id="ahoiArcImportAssistant" class="arc-import-card"
          aria-labelledby="ahoiArcImportHeading">
        <div id="ahoiArcImportHeading" class="arc-import-title">
          $i18n{ahoiArcImportTitle}
        </div>
        <div class="secondary">$i18n{ahoiArcImportDescription}</div>
        <cr-button id="ahoiArcDiscover" class="action-button"
            ?disabled="${this.arcImportStage_ === 'discovering' ||
                this.arcImportStage_ === 'committing'}"
            @click="${this.onArcDiscoverClick_}">
          $i18n{ahoiArcImportDiscover}
        </cr-button>

        <div id="ahoiArcImportStatus" class="arc-import-status"
            role="status" aria-live="polite">
          ${this.arcStatusText_()}
        </div>

        ${this.arcImportStage_ === 'sourceInUse' ? html`
          <div class="arc-import-warning" role="alert">
            <div>$i18n{ahoiArcImportCloseArcTitle}</div>
            <div class="secondary">$i18n{ahoiArcImportCloseArcSublabel}</div>
          </div>
        ` : ''}

        ${this.arcImportPreview_ && this.arcImportStage_ === 'preview' ? html`
          <section class="arc-import-preview"
              aria-labelledby="ahoiArcImportPreviewHeading">
            <div id="ahoiArcImportPreviewHeading"
                class="arc-import-subheading">
              $i18n{ahoiArcImportPreview}
            </div>
            <dl class="arc-import-counts">
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
                <dd>${this.arcImportPreview_.stats.unsafeUrls +
                    this.arcImportPreview_.stats.unsupportedItems}</dd></div>
            </dl>

            <div class="arc-import-subheading">$i18n{ahoiArcImportTargets}</div>
            <ul class="arc-import-targets">
              ${this.arcImportPreview_.targetWorkspaces.map(
                  workspace => html`<li>${workspace}</li>`)}
            </ul>

            <fieldset class="arc-import-options">
              <legend>$i18n{ahoiArcImportProfiles}</legend>
              ${this.arcImportPreview_.profiles.map(profile => html`
                <cr-checkbox data-profile="${profile}"
                    .checked="${this.arcSelectedProfiles_.includes(profile)}"
                    @change="${this.onArcProfileChange_}">
                  ${profile}
                </cr-checkbox>
              `)}
            </fieldset>

            <fieldset class="arc-import-options">
              <legend>$i18n{ahoiArcImportCategories}</legend>
              <cr-checkbox .checked="${this.arcImportSidebar_}"
                  @change="${this.onArcImportSidebarChange_}">
                $i18n{ahoiArcImportSidebarCategory}
              </cr-checkbox>
              <cr-checkbox .checked="${this.arcReconstructSplits_}"
                  @change="${this.onArcReconstructSplitsChange_}">
                $i18n{ahoiArcImportSplitCategory}
              </cr-checkbox>
            </fieldset>

            <label class="arc-import-select-label" for="ahoiArcConflictPolicy">
              $i18n{ahoiArcImportConflicts}
            </label>
            <select id="ahoiArcConflictPolicy"
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

            <div class="arc-import-exclusions" role="note">
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

        ${this.arcImportStage_ === 'done' ? html`
          <div class="arc-import-result" role="status">
            <div>${this.arcStatusText_()}</div>
            <div class="secondary">
              $i18n{ahoiArcImportResultSplits}
              ${this.arcImportResult_?.reconstructedSplits ?? 0}
            </div>
          </div>
        ` : ''}
      </section>

      <div class="section-heading cr-row hr">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiSyncSection}</div>
          <div class="secondary">$i18n{ahoiSyncSectionSublabel}</div>
        </div>
      </div>
      <div id="ahoiCloudKitProvider" class="sync-explanation cr-row"
          ?hidden="${!this.cloudKitAvailable_}">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiSyncProviderTitle}</div>
          <div class="secondary">$i18n{ahoiSyncProviderSublabel}</div>
        </div>
      </div>
      <div id="ahoiCloudKitUnavailableStatus"
          class="sync-explanation cr-row" role="status" aria-live="polite"
          ?hidden="${this.cloudKitAvailable_}">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiCloudKitUnavailableTitle}</div>
          <div class="secondary">$i18n{ahoiCloudKitUnavailableSublabel}</div>
        </div>
      </div>
      <settings-toggle-button id="ahoiSyncEnabled"
          pref-key="ahoi.sync.enabled"
          label="$i18n{ahoiSyncEnabled}"
          sub-label="$i18n{ahoiSyncEnabledSublabel}">
      </settings-toggle-button>
      <div class="list-frame indented-toggles">
        <settings-toggle-button id="ahoiRemoteControlEnabled"
            no-set-pref
            ?disabled="${!this.remoteControlStatus_?.canEnable ||
                this.remoteControlActionPending_}"
            pref-key="ahoi.sync.remote_control.enabled"
            .checked="${this.remoteControlStatus_?.enabled ?? false}"
            label="$i18n{ahoiRemoteControlEnabled}"
            sub-label="${this.remoteControlPrerequisiteText_()}"
            @change="${this.onRemoteControlEnabledChange_}">
        </settings-toggle-button>
        <section id="ahoiRemoteControlPairing" class="remote-pairing-card"
            aria-labelledby="ahoiRemoteControlPairingTitle">
          <div id="ahoiRemoteControlPairingTitle" class="pairing-title">
            $i18n{ahoiRemoteControlPairingTitle}
          </div>
          <div class="secondary">
            $i18n{ahoiRemoteControlPairingSublabel}
          </div>
          <div class="pairing-inputs">
            <cr-input id="ahoiRemoteControlDeviceId"
                label="$i18n{ahoiRemoteControlDeviceId}"
                .value="${this.remoteControlDeviceId_}"
                ?disabled="${!this.remoteControlStatus_?.canPair ||
                    this.remoteControlActionPending_}"
                @input="${this.onRemoteControlDeviceIdInput_}">
            </cr-input>
            <cr-input id="ahoiRemoteControlPublicKey"
                label="$i18n{ahoiRemoteControlPublicKey}"
                .value="${this.remoteControlPublicKey_}"
                ?disabled="${!this.remoteControlStatus_?.canPair ||
                    this.remoteControlActionPending_}"
                @input="${this.onRemoteControlPublicKeyInput_}">
            </cr-input>
          </div>
          <cr-button id="ahoiRemoteControlApprove" class="action-button"
              ?disabled="${!this.canApproveRemoteControlDevice_()}"
              @click="${this.onApproveRemoteControlDevice_}">
            $i18n{ahoiRemoteControlApprove}
          </cr-button>
          <div class="pairing-status" role="status" aria-live="polite">
            ${this.remoteControlActionText_() ||
                this.remoteControlPrerequisiteText_()}
          </div>
          ${this.remoteControlStatus_?.approvedDeviceIds.length ? html`
            <div class="approved-devices"
                aria-label="$i18n{ahoiRemoteControlApprovedDevices}">
              ${this.remoteControlStatus_.approvedDeviceIds.map(deviceId =>
                  html`<div class="approved-device-row">
                    <span title="${deviceId}">
                      ${this.shortRemoteControlDeviceId_(deviceId)}
                    </span>
                    <cr-button data-device-id="${deviceId}"
                        ?disabled="${this.remoteControlActionPending_}"
                        @click="${this.onRevokeRemoteControlDevice_}">
                      $i18n{ahoiRemoteControlRevoke}
                    </cr-button>
                  </div>`)}
            </div>
          ` : ''}
        </section>
        <div class="cr-row continuation">
          <div class="flex cr-padded-text" aria-hidden="true">
            <div>$i18n{ahoiHistoryRetention}</div>
            <div class="secondary">$i18n{ahoiHistoryRetentionSublabel}</div>
          </div>
          <settings-dropdown-menu id="ahoiHistoryRetention"
              ?disabled="${!this.syncEnabledPref_?.value}"
              label="$i18n{ahoiHistoryRetention}"
              pref-key="ahoi.sync.history_retention_days"
              .menuOptions="${this.historyRetentionOptions_}">
          </settings-dropdown-menu>
        </div>
      </div>

      <div class="section-heading cr-row hr">
        <div class="flex cr-padded-text">
          <div>$i18n{ahoiDeveloperSection}</div>
          <div class="secondary">$i18n{ahoiDeveloperSectionSublabel}</div>
        </div>
      </div>
      <settings-toggle-button id="ahoiDeveloperToolkitEnabled"
          pref-key="ahoi.developer_toolkit.enabled"
          label="$i18n{ahoiDeveloperToolkit}"
          sub-label="$i18n{ahoiDeveloperToolkitSublabel}"
          @change="${this.onAhoiDeveloperToolkitEnabledChange_}">
      </settings-toggle-button>
      <div class="list-frame indented-toggles">
        <settings-toggle-button id="ahoiDeveloperToolbarCookies"
            ?disabled="${!this.developerToolkitEnabledPref_?.value}"
            pref-key="ahoi.developer_toolbar.show_cookie_button"
            label="$i18n{ahoiDeveloperToolbarCookies}">
        </settings-toggle-button>
        <settings-toggle-button id="ahoiDeveloperToolbarCache"
            ?disabled="${!this.developerToolkitEnabledPref_?.value}"
            pref-key="ahoi.developer_toolbar.show_cache_button"
            label="$i18n{ahoiDeveloperToolbarCache}">
        </settings-toggle-button>
        <settings-toggle-button id="ahoiDeveloperToolbarHelpers"
            ?disabled="${!this.developerToolkitEnabledPref_?.value}"
            pref-key="ahoi.developer_toolbar.show_toolkit_button"
            label="$i18n{ahoiDeveloperToolbarHelpers}">
        </settings-toggle-button>
      </div>
    </settings-section>
  </div>
</cr-view-manager>
<!--_html_template_end_-->`;
  // clang-format on
}
