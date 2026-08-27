// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import 'chrome://resources/cr_elements/cr_view_manager/cr_view_manager.js';
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

export interface SettingsAhoiPageElement {
  $: {
    viewManager: CrViewManagerElement,
  };
}

const SettingsAhoiPageElementBase =
    SearchableViewContainerMixinLit(PrefServiceObserverMixinLit(CrLitElement));

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
    this.mirrorPrefs({
      'ahoi.developer_toolkit.enabled': 'developerToolkitEnabledPref_',
      'ahoi.navigation.floating_auto_hide_enabled':
          'floatingNavigationAutoHideEnabledPref_',
      'ahoi.sync.enabled': 'syncEnabledPref_',
    });
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
