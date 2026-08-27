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
          ?disabled="${!this.cloudKitAvailable_}"
          pref-key="ahoi.sync.enabled"
          label="$i18n{ahoiSyncEnabled}"
          sub-label="$i18n{ahoiSyncEnabledSublabel}">
      </settings-toggle-button>
      <div class="list-frame indented-toggles">
        <settings-toggle-button id="ahoiRemoteControlEnabled"
            ?disabled="${!this.cloudKitAvailable_ ||
                !this.syncEnabledPref_?.value}"
            pref-key="ahoi.sync.remote_control.enabled"
            label="$i18n{ahoiRemoteControlEnabled}"
            sub-label="$i18n{ahoiRemoteControlEnabledSublabel}">
        </settings-toggle-button>
        <div class="cr-row continuation">
          <div class="flex cr-padded-text" aria-hidden="true">
            <div>$i18n{ahoiHistoryRetention}</div>
            <div class="secondary">$i18n{ahoiHistoryRetentionSublabel}</div>
          </div>
          <settings-dropdown-menu id="ahoiHistoryRetention"
              ?disabled="${!this.cloudKitAvailable_ ||
                  !this.syncEnabledPref_?.value}"
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
