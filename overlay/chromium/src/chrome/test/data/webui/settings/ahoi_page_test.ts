// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import type {SettingsAhoiPageElement, SettingsDropdownMenuElement, SettingsMenuElement, SettingsToggleButtonElement} from 'chrome://settings/settings.js';
import {PrefsBrowserProxy, PrefService, routes} from 'chrome://settings/settings.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {microtasksFinished} from 'chrome://webui-test/test_util.js';

import {TestPrefsBrowserProxy} from './test_prefs_browser_proxy.js';

let page: SettingsAhoiPageElement;
let prefService: PrefService;

const AHOI_PREFS: chrome.settingsPrivate.PrefObject[] = [
  {
    key: 'ahoi.appearance.glass_enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: true,
  },
  {
    key: 'ahoi.appearance.sidebar_page_tint_enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.navigation.floating_auto_hide_enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: true,
  },
  {
    key: 'ahoi.navigation.floating_reveal_notch_enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: true,
  },
  {
    key: 'ahoi.navigation.floating_auto_hide_delay_ms',
    type: chrome.settingsPrivate.PrefType.NUMBER,
    value: 650,
  },
  {
    key: 'ahoi.sync.enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.sync.remote_control.enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.sync.history_retention_days',
    type: chrome.settingsPrivate.PrefType.NUMBER,
    value: 90,
  },
  {
    key: 'ahoi.developer_toolkit.enabled',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.developer_toolbar.show_cookie_button',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.developer_toolbar.show_cache_button',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: false,
  },
  {
    key: 'ahoi.developer_toolbar.show_toolkit_button',
    type: chrome.settingsPrivate.PrefType.BOOLEAN,
    value: true,
  },
];

async function createPage(cloudKitAvailable = false) {
  loadTimeData.overrideValues({ahoiCloudKitAvailable: cloudKitAvailable});
  const prefsBrowserProxy = new TestPrefsBrowserProxy(AHOI_PREFS);
  PrefsBrowserProxy.setInstance(prefsBrowserProxy);
  PrefService.resetInstanceForTesting();
  await PrefService.getInstance().whenInitialized();
  prefService = PrefService.getInstance();

  page = document.createElement('settings-ahoi-page');
  document.body.appendChild(page);
  await microtasksFinished();
}

suite('AhoiPage', () => {
  setup(async () => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    await createPage();
  });

  teardown(() => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    PrefService.resetInstanceForTesting();
  });

  test('ownsAhoiAppearanceNavigationSyncAndDeveloperControls', () => {
    const ids = [
      '#ahoiGlassEnabled',
      '#ahoiSidebarPageTintEnabled',
      '#ahoiNavigationAutoHide',
      '#ahoiNavigationRevealNotch',
      '#ahoiNavigationAutoHideDelay',
      '#ahoiSyncEnabled',
      '#ahoiRemoteControlEnabled',
      '#ahoiHistoryRetention',
      '#ahoiDeveloperToolkitEnabled',
      '#ahoiDeveloperToolbarCookies',
      '#ahoiDeveloperToolbarCache',
      '#ahoiDeveloperToolbarHelpers',
    ];
    for (const id of ids) {
      assertTrue(!!page.shadowRoot.querySelector(id), id);
    }
  });

  test('hasDedicatedRouteAndTopLevelMenuItem', async () => {
    assertEquals('/ahoi', routes.AHOI.path);
    assertEquals('ahoi', routes.AHOI.section);

    const menu: SettingsMenuElement = document.createElement('settings-menu');
    document.body.appendChild(menu);
    await microtasksFinished();

    const item = menu.shadowRoot!.querySelector<HTMLAnchorElement>('#ahoi');
    assertTrue(!!item);
    assertEquals('/ahoi', item.getAttribute('href'));
  });

  test('arcPreviewRequiresBothExplicitConfirmationsBeforeCommit', async () => {
    const mutablePage = page as unknown as {
      arcImportStage_: string,
      arcImportPreview_: object,
      arcSelectedProfiles_: string[],
    };
    mutablePage.arcImportStage_ = 'preview';
    mutablePage.arcImportPreview_ = {
      status: 'ok',
      snapshotToken: 'test-token-without-source-data',
      stats: {
        sourceWorkspaces: 1,
        sourceItems: 2,
        workspaces: 1,
        folders: 0,
        pages: 2,
        splits: 1,
        degradedSplits: 0,
        topApps: 0,
        unsafeUrls: 0,
        unsupportedItems: 0,
      },
      conflictingWorkspaces: 0,
      alreadyImported: false,
      sourceInUse: false,
      targetWorkspaces: ['Imported workspace'],
      profiles: ['Default'],
    };
    mutablePage.arcSelectedProfiles_ = ['Default'];
    page.requestUpdate();
    await microtasksFinished();

    const backup = page.shadowRoot.querySelector<HTMLElement&{checked: boolean}>(
        '#ahoiArcBackupConfirmation')!;
    const commit = page.shadowRoot.querySelector<HTMLElement&{checked: boolean}>(
        '#ahoiArcCommitConfirmation')!;
    const button = page.shadowRoot.querySelector<HTMLElement&{disabled: boolean}>(
        '#ahoiArcCommit')!;
    assertTrue(!!backup);
    assertTrue(!!commit);
    assertTrue(button.disabled);

    backup.click();
    await microtasksFinished();
    assertTrue(button.disabled);

    commit.click();
    await microtasksFinished();
    assertFalse(button.disabled);
  });

  test('syncOptInCanPrepareLocalStateWithoutCloudKitTransport', async () => {
    const sync = page.shadowRoot.querySelector<SettingsToggleButtonElement>(
        '#ahoiSyncEnabled')!;
    const remote = page.shadowRoot.querySelector<SettingsToggleButtonElement>(
        '#ahoiRemoteControlEnabled')!;
    const retention =
        page.shadowRoot.querySelector<SettingsDropdownMenuElement>(
            '#ahoiHistoryRetention')!;

    const unavailable = page.shadowRoot.querySelector<HTMLElement>(
        '#ahoiCloudKitUnavailableStatus')!;

    assertFalse(unavailable.hidden);
    assertFalse(sync.checked);
    assertFalse(sync.disabled);
    assertTrue(remote.disabled);
    assertTrue(retention.disabled);

    sync.click();
    await microtasksFinished();

    assertTrue(prefService.getPref<boolean>('ahoi.sync.enabled').value);
    assertTrue(sync.checked);
    assertFalse(sync.disabled);
    assertTrue(remote.disabled);
    assertFalse(retention.disabled);
  });

  test('sidebarPageTintIsOptionalAndPersistsUserChoice', async () => {
    const tint = page.shadowRoot.querySelector<SettingsToggleButtonElement>(
        '#ahoiSidebarPageTintEnabled')!;

    assertFalse(tint.checked);
    tint.click();
    await microtasksFinished();

    assertTrue(
        prefService
            .getPref<boolean>('ahoi.appearance.sidebar_page_tint_enabled')
            .value);
  });

  test('futureCloudKitAvailabilityUnlocksSafeSyncPrefs', async () => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    await createPage(true);

    const sync = page.shadowRoot.querySelector<SettingsToggleButtonElement>(
        '#ahoiSyncEnabled')!;
    const remote = page.shadowRoot.querySelector<SettingsToggleButtonElement>(
        '#ahoiRemoteControlEnabled')!;
    const retention =
        page.shadowRoot.querySelector<SettingsDropdownMenuElement>(
            '#ahoiHistoryRetention')!;
    const unavailable = page.shadowRoot.querySelector<HTMLElement>(
        '#ahoiCloudKitUnavailableStatus')!;

    assertTrue(unavailable.hidden);
    assertFalse(sync.disabled);
    sync.click();
    await microtasksFinished();

    assertTrue(prefService.getPref<boolean>('ahoi.sync.enabled').value);
    assertFalse(remote.disabled);
    assertFalse(retention.disabled);
    assertEquals(
        90,
        prefService.getPref<number>('ahoi.sync.history_retention_days').value);
  });

  test('enablingToolkitKeepsOneRecoverableAddressBarEntry', async () => {
    await prefService.setPrefValue(
        'ahoi.developer_toolbar.show_toolkit_button', false);
    const developer =
        page.shadowRoot.querySelector<SettingsToggleButtonElement>(
            '#ahoiDeveloperToolkitEnabled')!;

    developer.click();
    await microtasksFinished();

    assertTrue(
        prefService.getPref<boolean>('ahoi.developer_toolkit.enabled').value);
    assertTrue(
        prefService
            .getPref<boolean>('ahoi.developer_toolbar.show_toolkit_button')
            .value);
  });
});
