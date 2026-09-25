// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import {loadTimeData} from '../i18n_setup.js';
import type {SettingsAhoiPageElement} from './ahoi_page.js';

export function getHtml(this: SettingsAhoiPageElement) {
  const routing = this.linkRouting_;
  const routingLabels = routing?.labels;
  const routingLocked = !routing?.canChange || this.linkRoutingPending_;
  const routingError = (scope: string) =>
      !!routing?.error && this.linkRoutingErrorRuleId_ === scope;
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

      <div class="section-heading cr-row hr" ?hidden="${!routing}">
        <div class="flex cr-padded-text">
          <div id="ahoiLinkRoutingTitle">${routingLabels?.title || ''}</div>
          <div class="secondary">${routingLabels?.description || ''}</div>
        </div>
      </div>
      <section id="ahoiLinkRouting" class="link-routing-card"
          aria-labelledby="ahoiLinkRoutingTitle"
          aria-busy="${this.linkRoutingPending_}" ?hidden="${!routing}">
        <label class="link-routing-option">
          <input id="ahoiLinkRoutingEnabled" type="checkbox"
              .checked="${routing?.enabled ?? false}"
              ?disabled="${routingLocked}"
              @change="${this.onLinkRoutingEnabledChange_}">
          <span>${routingLabels?.enabled || ''}</span>
        </label>
        <div class="link-routing-heading">${routingLabels?.rules || ''}</div>
        <div id="ahoiLinkRoutingRules" class="link-routing-rules">
          ${routing?.rules.length ? '' : html`
            <div class="secondary">${routingLabels?.noRules || ''}</div>`}
          ${routing?.rules.map((rule, index) => html`
            <div class="link-routing-rule
                ${rule.id === this.linkRoutingExample_?.ruleId ? 'winning' : ''}
                ${rule.enabled ? '' : 'disabled-rule'}"
                role="group" data-rule-id="${rule.id}"
                aria-label="${rule.host}${rule.path}">
              <label class="link-routing-option">
                <input type="checkbox" data-rule-id="${rule.id}"
                    data-field="enabled" .checked="${rule.enabled}"
                    ?disabled="${routingLocked}"
                    @change="${this.onLinkRoutingRuleFieldChange_}">
                <span>${routingLabels?.ruleEnabled || ''}</span>
              </label>
              <div class="link-routing-fields">
                <label class="link-routing-field">
                  <span class="secondary">${routingLabels?.host || ''}</span>
                  <input type="text" data-rule-id="${rule.id}"
                      data-field="host" .value="${rule.host}"
                      spellcheck="false" autocomplete="off"
                      ?disabled="${routingLocked}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                  <span class="secondary">
                    ${routingLabels?.hostPortHint || ''}
                  </span>
                </label>
                <label class="link-routing-field">
                  <span class="secondary">${routingLabels?.path || ''}</span>
                  <input type="text" data-rule-id="${rule.id}"
                      data-field="path" .value="${rule.path}"
                      spellcheck="false" autocomplete="off"
                      ?disabled="${routingLocked}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                </label>
              </div>
              <label class="link-routing-option">
                <input type="checkbox" data-rule-id="${rule.id}"
                    data-field="includeSubdomains"
                    .checked="${rule.includeSubdomains}"
                    ?disabled="${routingLocked}"
                    @change="${this.onLinkRoutingRuleFieldChange_}">
                <span>${routingLabels?.includeSubdomains || ''}</span>
              </label>
              <div class="link-routing-fields">
                <label class="link-routing-field">
                  <span class="secondary">${routingLabels?.target || ''}</span>
                  <select class="md-select" data-rule-id="${rule.id}"
                      data-field="target" ?disabled="${routingLocked}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                    ${rule.targetAvailable ? '' : html`
                      <option value="${rule.target}" .selected="${true}">
                        ${routingLabels?.unavailableTarget || ''}
                      </option>`}
                    ${routing?.workspaces.map(workspace => html`
                      <option value="${workspace.id}"
                          .selected="${workspace.id === rule.target}">
                        ${this.linkRoutingWorkspaceLabel_(workspace)}
                      </option>`)}
                  </select>
                </label>
                <label class="link-routing-field">
                  <span class="secondary">${routingLabels?.mode || ''}</span>
                  <select class="md-select" data-rule-id="${rule.id}"
                      data-field="mode" ?disabled="${routingLocked}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                    <option value="normal_tab"
                        .selected="${rule.mode === 'normal_tab'}">
                      ${routingLabels?.normalTab || ''}
                    </option>
                    <option value="quick_window"
                        .selected="${rule.mode === 'quick_window'}">
                      ${routingLabels?.quickWindow || ''}
                    </option>
                  </select>
                </label>
              </div>
              <div class="link-routing-warning secondary"
                  ?hidden="${rule.targetAvailable}">
                ${routingLabels?.rememberHint || ''}
              </div>
              <div class="link-routing-actions">
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${routingLocked || index === 0}"
                    @click="${this.onLinkRoutingRuleMoveUp_}">
                  ${routingLabels?.moveUp || ''}
                </cr-button>
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${routingLocked ||
                        index === (routing?.rules.length ?? 0) - 1}"
                    @click="${this.onLinkRoutingRuleMoveDown_}">
                  ${routingLabels?.moveDown || ''}
                </cr-button>
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${routingLocked}"
                    @click="${this.onLinkRoutingRuleDelete_}">
                  ${routingLabels?.delete || ''}
                </cr-button>
              </div>
              <div class="link-routing-error" role="alert"
                  ?hidden="${!routingError(rule.id)}">
                ${routing?.errorLabel || ''}
              </div>
            </div>`)}
        </div>

        <div id="ahoiLinkRoutingAddRule" class="link-routing-add" role="group"
            aria-label="${routingLabels?.add || ''}">
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.host || ''}</span>
              <input id="ahoiLinkRoutingAddHost" type="text" data-field="host"
                  .value="${this.linkRoutingDraft_.host}"
                  spellcheck="false" autocomplete="off"
                  aria-describedby="ahoiLinkRoutingAddPortHint"
                  ?disabled="${routingLocked}"
                  @input="${this.onLinkRoutingDraftChange_}">
              <span id="ahoiLinkRoutingAddPortHint" class="secondary">
                ${routingLabels?.hostPortHint || ''}
              </span>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.path || ''}</span>
              <input id="ahoiLinkRoutingAddPath" type="text" data-field="path"
                  .value="${this.linkRoutingDraft_.path}"
                  spellcheck="false" autocomplete="off"
                  ?disabled="${routingLocked}"
                  @input="${this.onLinkRoutingDraftChange_}">
            </label>
          </div>
          <label class="link-routing-option">
            <input id="ahoiLinkRoutingAddSubdomains" type="checkbox"
                data-field="includeSubdomains"
                .checked="${this.linkRoutingDraft_.includeSubdomains}"
                ?disabled="${routingLocked}"
                @change="${this.onLinkRoutingDraftChange_}">
            <span>${routingLabels?.includeSubdomains || ''}</span>
          </label>
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.target || ''}</span>
              <select id="ahoiLinkRoutingAddTarget" class="md-select"
                  data-field="target" ?disabled="${routingLocked}"
                  @change="${this.onLinkRoutingDraftChange_}">
                ${routing?.workspaces.map(workspace => html`
                  <option value="${workspace.id}"
                      .selected="${workspace.id === this.linkRoutingDraft_.target}">
                    ${this.linkRoutingWorkspaceLabel_(workspace)}
                  </option>`)}
              </select>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.mode || ''}</span>
              <select id="ahoiLinkRoutingAddMode" class="md-select"
                  data-field="mode" ?disabled="${routingLocked}"
                  @change="${this.onLinkRoutingDraftChange_}">
                <option value="normal_tab"
                    .selected="${this.linkRoutingDraft_.mode === 'normal_tab'}">
                  ${routingLabels?.normalTab || ''}
                </option>
                <option value="quick_window"
                    .selected="${this.linkRoutingDraft_.mode === 'quick_window'}">
                  ${routingLabels?.quickWindow || ''}
                </option>
              </select>
            </label>
          </div>
          <div class="link-routing-actions">
            <cr-button id="ahoiLinkRoutingAdd" class="action-button"
                ?disabled="${routingLocked ||
                    !this.linkRoutingDraft_.host.trim() ||
                    !this.linkRoutingDraft_.target}"
                @click="${this.onLinkRoutingAddClick_}">
              ${routingLabels?.add || ''}
            </cr-button>
          </div>
          <div class="link-routing-error" role="alert"
              ?hidden="${!routingError('new')}">
            ${routing?.errorLabel || ''}
          </div>
        </div>

        <div id="ahoiLinkRoutingDefault" class="link-routing-default"
            role="group" aria-label="${routingLabels?.defaultRoute || ''}">
          <div class="link-routing-heading">
            ${routingLabels?.defaultRoute || ''}
          </div>
          <div class="secondary">${routingLabels?.defaultRouteHint || ''}</div>
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.target || ''}</span>
              <select id="ahoiLinkRoutingDefaultTarget" class="md-select"
                  data-field="target" ?disabled="${routingLocked}"
                  @change="${this.onLinkRoutingDefaultChange_}">
                <option value="last_active"
                    .selected="${routing?.defaultRoute.target === 'last_active'}">
                  ${routingLabels?.lastActive || ''}
                </option>
                ${routing?.defaultRoute.targetAvailable ? '' : html`
                  <option value="${routing?.defaultRoute.target || ''}"
                      .selected="${true}">
                    ${routingLabels?.unavailableTarget || ''}
                  </option>`}
                ${routing?.workspaces.map(workspace => html`
                  <option value="${workspace.id}"
                      .selected="${workspace.id === routing?.defaultRoute.target}">
                    ${this.linkRoutingWorkspaceLabel_(workspace)}
                  </option>`)}
              </select>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${routingLabels?.mode || ''}</span>
              <select id="ahoiLinkRoutingDefaultMode" class="md-select"
                  data-field="mode" ?disabled="${routingLocked}"
                  @change="${this.onLinkRoutingDefaultChange_}">
                <option value="normal_tab"
                    .selected="${routing?.defaultRoute.mode === 'normal_tab'}">
                  ${routingLabels?.normalTab || ''}
                </option>
                <option value="quick_window"
                    .selected="${routing?.defaultRoute.mode === 'quick_window'}">
                  ${routingLabels?.quickWindow || ''}
                </option>
              </select>
            </label>
          </div>
          <div class="link-routing-error" role="alert"
              ?hidden="${!routingError('default')}">
            ${routing?.errorLabel || ''}
          </div>
        </div>

        <div class="link-routing-example">
          <label class="link-routing-field">
            <span class="link-routing-heading">
              ${routingLabels?.example || ''}
            </span>
            <input id="ahoiLinkRoutingExample" type="text"
                .value="${this.linkRoutingExampleInput_}"
                placeholder="${routingLabels?.examplePlaceholder || ''}"
                spellcheck="false" autocomplete="off"
                aria-describedby="ahoiLinkRoutingExampleResult"
                @input="${this.onLinkRoutingExampleInput_}">
          </label>
          <div id="ahoiLinkRoutingExampleResult" class="secondary"
              role="status" aria-live="polite">
            ${this.linkRoutingExample_?.text || ''}
          </div>
        </div>

        <div class="link-routing-actions" style="margin-top: 12px;">
          <cr-button id="ahoiLinkRoutingReset"
              aria-describedby="ahoiLinkRoutingResetHint"
              ?disabled="${routingLocked}"
              @click="${this.onLinkRoutingResetClick_}">
            ${this.linkRoutingResetArmed_ ? routingLabels?.resetConfirm :
                                            routingLabels?.reset}
          </cr-button>
        </div>
        <div id="ahoiLinkRoutingResetHint" class="secondary">
          ${routingLabels?.resetHint || ''}
        </div>
        <div id="ahoiLinkRoutingError" class="link-routing-error" role="alert"
            ?hidden="${!routingError('')}">
          ${routing?.errorLabel || ''}
        </div>
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
          ?hidden="${this.remoteControlStatus_ === null || this.cloudKitAvailable_}">
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
        <section id="ahoiSyncControls" class="sync-control-card"
            aria-label="$i18n{ahoiSyncSection}"
            aria-busy="${this.syncControlsActionPending_}">
          <div class="sync-control-status" role="status" aria-live="polite">
            ${this.syncControlsStatus_?.statusLabel ||
                loadTimeData.getString('ahoiBrowserSettingsSyncLoading')}
          </div>
          <div class="sync-control-actions">
            <cr-button id="ahoiSyncNow"
                ?disabled="${!this.syncControlsStatus_?.canSyncNow ||
                    this.syncControlsActionPending_}"
                @click="${this.onSyncNowClick_}">
              ${this.syncControlsStatus_?.labels.syncNow || ''}
            </cr-button>
            <cr-button id="ahoiRetrySyncKey"
                ?hidden="${!this.syncControlsStatus_?.keySetupIssue}"
                ?disabled="${!this.syncControlsStatus_?.canRetryKey ||
                    this.syncControlsActionPending_}"
                @click="${this.onRetrySyncKeyClick_}">
              ${this.syncControlsStatus_?.labels.retryKey || ''}
            </cr-button>
          </div>
          <div class="sync-control-options">
            <div class="sync-control-heading">
              ${this.syncControlsStatus_?.labels.bookmarks || ''}
            </div>
            <div id="ahoiBookmarkConsentHint" class="secondary">
              ${this.syncControlsStatus_?.bookmarkSyncEnabled ?
                  this.syncControlsStatus_?.labels.bookmarkStopHint :
                  this.syncControlsStatus_?.labels.bookmarkConsentHint}
            </div>
            <div class="sync-control-actions">
              <cr-button id="ahoiBookmarkSyncConsent"
                  ?disabled="${!this.syncControlsStatus_?.canChangeBookmarkConsent ||
                      this.syncControlsActionPending_}"
                  aria-describedby="ahoiBookmarkConsentHint"
                  @click="${this.onBookmarkSyncClick_}">
                ${this.syncControlsStatus_?.bookmarkSyncEnabled ?
                    this.syncControlsStatus_?.labels.stopBookmarks :
                    this.syncControlsStatus_?.labels.approveBookmarks}
              </cr-button>
            </div>
            <div class="secondary" role="status" aria-live="polite"
                ?hidden="${!this.syncControlsStatus_?.bookmarkIssueLabel}">
              ${this.syncControlsStatus_?.bookmarkIssueLabel || ''}
            </div>
          </div>
          <div class="sync-control-options">
            <div class="sync-control-heading">
              ${this.syncControlsStatus_?.labels.extensions || ''}
            </div>
            <label class="sync-control-option">
              <input id="ahoiExtensionSetupSync" type="checkbox"
                  .checked="${this.syncControlsStatus_?.extensionSetupEnabled ?? false}"
                  ?disabled="${!this.syncControlsStatus_?.canChangeExtensionConsent ||
                      this.syncControlsActionPending_}"
                  @change="${this.onExtensionSetupChange_}">
              <span>${this.syncControlsStatus_?.labels.extensionSetup || ''}</span>
            </label>
            <label class="sync-control-option">
              <input id="ahoiExtensionSettingsSync" type="checkbox"
                  .checked="${this.syncControlsStatus_?.extensionSettingsEnabled ?? false}"
                  ?disabled="${!this.syncControlsStatus_?.canChangeExtensionConsent ||
                      this.syncControlsActionPending_}"
                  @change="${this.onExtensionSettingsChange_}">
              <span>
                ${this.syncControlsStatus_?.labels.extensionSettings || ''}
                <span class="secondary">
                  ${this.syncControlsStatus_?.labels.extensionSettingsHint || ''}
                </span>
              </span>
            </label>
            ${this.syncControlsStatus_?.extensionResults.map(item => html`
              <div class="sync-extension-result">
                <span title="${item.id}">${item.id.slice(0, 8)} · ${item.status}</span>
                <cr-button data-extension-id="${item.id}"
                    ?hidden="${!item.canRetry}"
                    ?disabled="${this.syncControlsActionPending_}"
                    @click="${this.onExtensionRetryClick_}">
                  ${item.needsConfirmation ?
                      this.syncControlsStatus_?.labels.reviewExtension :
                      this.syncControlsStatus_?.labels.retryExtension}
                </cr-button>
              </div>`)}
          </div>
          <div class="sync-recovery-card"
              ?hidden="${!this.syncControlsStatus_?.accountTransitionPending &&
                  !this.syncControlsStatus_?.zoneRecoveryPending}">
            <div class="sync-control-heading">
              ${this.syncControlsStatus_?.labels.recovery || ''}
            </div>
            <div class="secondary"
                ?hidden="${!this.syncControlsStatus_?.accountTransitionPending}">
              ${this.syncControlsStatus_?.labels.accountRecoveryHint || ''}
            </div>
            <div class="sync-control-actions">
              <cr-button ?hidden="${!this.syncControlsStatus_?.accountTransitionPending}"
                  ?disabled="${this.syncControlsActionPending_}"
                  @click="${this.onAccountRecoveryUploadClick_}">
                ${this.syncControlsStatus_?.labels.uploadLocal || ''}
              </cr-button>
              <cr-button ?hidden="${!this.syncControlsStatus_?.accountTransitionPending}"
                  ?disabled="${this.syncControlsActionPending_}"
                  @click="${this.onAccountRecoveryWithoutUploadClick_}">
                ${this.syncControlsStatus_?.labels.withoutUpload || ''}
              </cr-button>
              <cr-button ?hidden="${!this.syncControlsStatus_?.zoneRecoveryPending}"
                  ?disabled="${this.syncControlsActionPending_}"
                  @click="${this.onZoneRecoveryClick_}">
                ${this.syncControlsStatus_?.labels.recoverZone || ''}
              </cr-button>
            </div>
          </div>
          <div class="secondary" role="status" aria-live="polite"
              ?hidden="${!this.syncControlsActionFailed_}">
            $i18n{ahoiBrowserSettingsSyncFailed}
          </div>
        </section>
        <section id="ahoiBrowserSettingsSyncSection"
            class="browser-settings-sync-card"
            aria-labelledby="ahoiBrowserSettingsSyncTitle"
            aria-busy="${this.browserSettingsSyncActionPending_}">
          <label class="browser-settings-sync-option">
            <input id="ahoiBrowserSettingsSyncEnabled" type="checkbox"
                .checked="${this.browserSettingsSyncStatus_?.selection === 'all'}"
                .indeterminate="${this.browserSettingsSyncStatus_?.selection === 'some'}"
                ?disabled="${!this.browserSettingsSyncStatus_?.canChange ||
                    this.browserSettingsSyncActionPending_}"
                aria-labelledby="ahoiBrowserSettingsSyncTitle"
                aria-describedby="ahoiBrowserSettingsSyncDescription ahoiBrowserSettingsSyncStatus"
                @change="${this.onBrowserSettingsSyncChange_}">
            <span class="browser-settings-sync-copy">
              <span id="ahoiBrowserSettingsSyncTitle"
                  class="browser-settings-sync-title">
                $i18n{ahoiBrowserSettingsSync}
              </span>
              <span id="ahoiBrowserSettingsSyncDescription" class="secondary">
                $i18n{ahoiBrowserSettingsSyncDescription}
              </span>
            </span>
          </label>
          <div id="ahoiBrowserSettingsSyncStatus"
              class="browser-settings-sync-status secondary"
              role="status" aria-live="polite">
            ${this.browserSettingsSyncStatusText_()}
            <div ?hidden="${!this.browserSettingsSyncActionFailed_}">
              $i18n{ahoiBrowserSettingsSyncFailed}
            </div>
            <div ?hidden="${!this.browserSettingsSyncStatus_ ||
                this.browserSettingsSyncStatus_.syncEnabled ||
                this.browserSettingsSyncStatus_.selectedCount === 0}">
              $i18n{ahoiBrowserSettingsSyncPaused}
            </div>
          </div>
        </section>
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
              @click="${this.onRemoteControlApproveClick_}">
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
                        @click="${this.onRemoteControlRevokeClick_}">
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

      <div class="section-heading cr-row hr"
          ?hidden="${!this.portableExportOptions_}">
        <div class="flex cr-padded-text">
          <div id="ahoiPortableExportTitle">
            ${this.portableExportOptions_?.labels.title || ''}
          </div>
          <div class="secondary">
            ${this.portableExportOptions_?.labels.description || ''}
          </div>
        </div>
      </div>
      <section id="ahoiPortableExport" class="portable-export-card"
          aria-labelledby="ahoiPortableExportTitle"
          aria-busy="${this.portableExportPending_}"
          ?hidden="${!this.portableExportOptions_}">
        <div class="portable-workspace-list">
          ${this.portableExportOptions_?.workspaces.map(workspace => html`
            <label class="portable-workspace-option">
              <input type="checkbox" data-workspace-id="${workspace.id}"
                  .checked="${this.portableSelectedWorkspaceIds_.includes(workspace.id)}"
                  ?disabled="${this.portableExportPending_}"
                  @change="${this.onPortableWorkspaceChange_}">
              <span>${workspace.name}</span>
            </label>`)}
        </div>
        <div class="portable-export-categories">
          <label class="portable-workspace-option">
            <input type="checkbox" .checked="${this.portableIncludeTemporary_}"
                ?disabled="${this.portableExportPending_}"
                @change="${this.onPortableTemporaryChange_}">
            <span>${this.portableExportOptions_?.labels.temporary || ''}</span>
          </label>
          <label class="portable-workspace-option">
            <input type="checkbox" .checked="${this.portableIncludeArchives_}"
                ?disabled="${this.portableExportPending_}"
                @change="${this.onPortableArchivesChange_}">
            <span>${this.portableExportOptions_?.labels.archives || ''}</span>
          </label>
        </div>
        <div class="portable-export-actions">
          <cr-button id="ahoiPortablePreview"
              ?disabled="${!this.portableExportOptions_?.available ||
                  this.portableSelectedWorkspaceIds_.length === 0 ||
                  this.portableExportPending_}"
              @click="${this.onPortablePreviewClick_}">
            ${this.portableExportOptions_?.labels.prepare || ''}
          </cr-button>
        </div>
        ${this.portableExportPreview_ ? html`
          <div class="portable-export-preview" role="status">
            <span>${this.portableExportOptions_?.labels.workspaces}:
              ${this.portableExportPreview_.workspaces}</span>
            <span>${this.portableExportOptions_?.labels.pages}:
              ${this.portableExportPreview_.pages}</span>
            <span>${this.portableExportOptions_?.labels.splits}:
              ${this.portableExportPreview_.splits}</span>
            <span>${this.portableExportOptions_?.labels.archivesCount}:
              ${this.portableExportPreview_.archives}</span>
            <span>${this.portableExportOptions_?.labels.excluded}:
              ${this.portableExportPreview_.excluded}</span>
          </div>
          <p id="ahoiPortableExportWarning" class="secondary">
            ${this.portableExportOptions_?.labels.unencrypted || ''}
          </p>
          <cr-button id="ahoiPortableSave" class="action-button"
              aria-describedby="ahoiPortableExportWarning"
              ?disabled="${this.portableExportPending_}"
              @click="${this.onPortableSaveClick_}">
            ${this.portableExportOptions_?.labels.save || ''}
          </cr-button>` : ''}
        <div class="portable-export-status secondary" role="status"
            aria-live="polite">
          ${this.portableExportStatusText_()}
        </div>
        <div class="portable-import-section">
          <cr-button id="ahoiPortableImportPreview"
              ?disabled="${this.portableImportPending_ ||
                  this.portableExportPending_}"
              @click="${this.onPortableImportClick_}">
            ${this.portableExportOptions_?.labels.importFile || ''}
          </cr-button>
          <div class="secondary" role="status" aria-live="polite">
            ${this.portableImportStatusText_()}
          </div>
          ${this.portableImportPreview_ ? html`
            <div class="portable-import-preview" role="group"
                aria-label="${this.portableExportOptions_?.labels.importDestination || ''}">
              <div>${this.portableExportOptions_?.labels.importDestination}</div>
              <p class="secondary">
                ${this.portableExportOptions_?.labels.importSelectionHint}
              </p>
              ${this.portableImportPreview_.workspaces?.map(workspace => html`
                <label class="portable-workspace-option">
                  <input type="checkbox" data-workspace-id="${workspace.id}"
                      .checked="${this.portableImportSelectedWorkspaceIds_.includes(workspace.id)}"
                      ?disabled="${this.portableImportPending_}"
                      @change="${this.onPortableImportWorkspaceChange_}">
                  <span>${workspace.name} ·
                    ${workspace.destination === 'new' ?
                        this.portableExportOptions_?.labels.importNew :
                        workspace.destination === 'identical' ?
                        this.portableExportOptions_?.labels.importIdentical :
                        this.portableExportOptions_?.labels.importConflict}</span>
                </label>
              `)}
              <div>${this.portableExportOptions_?.labels.pages}:
                ${this.portableImportPreview_.pages}</div>
              <div>${this.portableExportOptions_?.labels.splits}:
                ${this.portableImportPreview_.splits}</div>
              <div>${this.portableExportOptions_?.labels.archivesCount}:
                ${this.portableImportPreview_.archives}</div>
              <div>${this.portableExportOptions_?.labels.importNew}:
                ${this.portableImportPreview_.newItems}</div>
              <div>${this.portableExportOptions_?.labels.importIdentical}:
                ${this.portableImportPreview_.identicalItems}</div>
              <div>${this.portableExportOptions_?.labels.importConflict}:
                ${this.portableImportPreview_.conflictingItems}</div>
              <cr-button id="ahoiPortableImportCommit" class="action-button"
                  ?disabled="${!this.canCommitPortableImport_()}"
                  @click="${this.onPortableCommitClick_}">
                ${this.portableExportOptions_?.labels.importCommit || ''}
              </cr-button>
            </div>` : ''}
        </div>
      </section>

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
