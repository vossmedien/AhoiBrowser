// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// clang-format off
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import type {BrowserProfile, ImportDataBrowserProxy, SettingsImportDataDialogElement} from 'chrome://settings/lazy_load.js';
import {ImportDataBrowserProxyImpl} from 'chrome://settings/lazy_load.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';
// clang-format on

class TestImportDataBrowserProxy extends TestBrowserProxy implements
    ImportDataBrowserProxy {
  constructor(private readonly profiles_: BrowserProfile[]) {
    super([
      'initializeImportDialog',
      'importFromBookmarksFile',
      'importData',
    ]);
  }

  initializeImportDialog() {
    this.methodCalled('initializeImportDialog');
    return Promise.resolve(this.profiles_.slice());
  }

  importFromBookmarksFile() {
    this.methodCalled('importFromBookmarksFile');
  }

  importData(index: number, types: {[type: string]: boolean}) {
    this.methodCalled('importData', [index, types]);
  }
}

suite('AhoiZenImportAvailability', () => {
  const firefoxProfile: BrowserProfile = {
    available: true,
    autofillFormData: true,
    disabledReason: '',
    favorites: true,
    history: true,
    index: 0,
    name: 'Mozilla Firefox',
    passwords: true,
    present: true,
    profileName: '',
    search: true,
  };
  const bookmarksProfile: BrowserProfile = {
    available: true,
    autofillFormData: false,
    disabledReason: '',
    favorites: true,
    history: false,
    index: 2,
    name: 'Bookmarks HTML File',
    passwords: false,
    present: true,
    profileName: '',
    search: false,
  };

  function zenProfile(available: boolean): BrowserProfile {
    return {
      ahoiImportKind: 'zen',
      available,
      autofillFormData: available,
      disabledReason: available ? '' : 'sourceRunning',
      favorites: available,
      history: available,
      index: 1,
      name: 'Zen',
      passwords: false,
      present: true,
      profileName: 'Personal',
      search: false,
    };
  }

  function createPrefs(): {[key: string]: chrome.settingsPrivate.PrefObject} {
    const prefs: {[key: string]: chrome.settingsPrivate.PrefObject} = {};
    for (const key of [
      'import_dialog_history',
      'import_dialog_bookmarks',
      'import_dialog_saved_passwords',
      'import_dialog_search_engine',
      'import_dialog_autofill_form_data',
    ]) {
      prefs[key] = {
        key,
        type: chrome.settingsPrivate.PrefType.BOOLEAN,
        value: true,
      };
    }
    return prefs;
  }

  async function createDialog(profiles: BrowserProfile[]): Promise<{
    dialog: SettingsImportDataDialogElement,
    proxy: TestImportDataBrowserProxy,
  }> {
    const proxy = new TestImportDataBrowserProxy(profiles);
    ImportDataBrowserProxyImpl.setInstance(proxy);
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    const dialog = document.createElement('settings-import-data-dialog');
    dialog.set('prefs', createPrefs());
    document.body.appendChild(dialog);
    await proxy.whenCalled('initializeImportDialog');
    flush();
    return {dialog, proxy};
  }

  function selectSource(
      dialog: SettingsImportDataDialogElement, optionIndex: number) {
    dialog.$.browserSelect.selectedIndex = optionIndex;
    dialog.$.browserSelect.dispatchEvent(new CustomEvent('change'));
    flush();
  }

  test('runningZenIsVisibleDisabledAndCannotStartAnImport', async () => {
    const {dialog, proxy} = await createDialog([
      firefoxProfile,
      zenProfile(false),
      bookmarksProfile,
    ]);
    const zenOption = dialog.$.browserSelect.options[1]!;
    assertTrue(zenOption.disabled);
    assertTrue(zenOption.textContent.includes(
        loadTimeData.getString('ahoiZenImportCloseSource')));

    selectSource(dialog, 1);
    assertTrue(dialog.$.import.disabled);
    assertFalse(dialog.$.sourceDisabledReason.hidden);
    assertEquals(
        loadTimeData.getString('ahoiZenImportCloseSource'),
        dialog.$.sourceDisabledReason.textContent.trim());

    // Exercise the renderer guard even if a caller removes the visual state.
    dialog.$.import.disabled = false;
    dialog.$.import.click();
    assertEquals(0, proxy.getCallCount('importData'));
    assertEquals(0, proxy.getCallCount('importFromBookmarksFile'));
  });

  test('notInstalledZenDoesNotCreateAPhantomOption', async () => {
    const {dialog} = await createDialog([
      firefoxProfile,
      {...bookmarksProfile, index: 1},
    ]);
    const optionLabels = Array.from(dialog.$.browserSelect.options).map(
        option => option.textContent.trim());
    assertFalse(optionLabels.some(label => label.startsWith('Zen')));
  });

  test('availableZenUsesItsStableBackendIndexAndRealCategories', async () => {
    const {dialog, proxy} = await createDialog([
      firefoxProfile,
      zenProfile(true),
      bookmarksProfile,
    ]);
    const zenOption = dialog.$.browserSelect.options[1]!;
    assertFalse(zenOption.disabled);

    selectSource(dialog, 1);
    assertFalse(dialog.$.import.disabled);
    dialog.$.import.click();
    const [index, types] = await proxy.whenCalled('importData');
    assertEquals(1, index);
    assertTrue(types['import_dialog_history']);
    assertTrue(types['import_dialog_bookmarks']);
    assertTrue(types['import_dialog_autofill_form_data']);
    assertFalse(types['import_dialog_saved_passwords']);
  });
});
