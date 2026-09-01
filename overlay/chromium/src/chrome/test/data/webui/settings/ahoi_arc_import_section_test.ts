// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import type {BrowserProfile, ImportDataBrowserProxy, SettingsImportDataDialogElement} from 'chrome://settings/lazy_load.js';
import {ImportDataBrowserProxyImpl} from 'chrome://settings/lazy_load.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';

type ArcImportStage =
    'idle'|'discovering'|'preview'|'committing'|'sourceInUse'|'done'|'error';

interface ArcImportStatsFixture {
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

interface MutableArcImportSection extends HTMLElement {
  arcImportStage_: ArcImportStage;
  arcImportPreview_: object|null;
  arcImportResult_: object|null;
  arcSelectedProfiles_: string[];
  requestUpdate(): void;
  updateComplete: Promise<boolean>;
}

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

suite('AhoiArcStandardImportSurface', () => {
  const standardProfiles: BrowserProfile[] = [
    {
      autofillFormData: true,
      favorites: true,
      history: true,
      index: 0,
      name: 'Mozilla Firefox',
      passwords: true,
      profileName: '',
      search: true,
    },
    {
      ahoiImportKind: 'arc',
      available: true,
      autofillFormData: false,
      disabledReason: '',
      favorites: true,
      history: false,
      index: 1,
      name: 'Arc',
      passwords: false,
      present: true,
      profileName: '',
      search: false,
    },
    {
      autofillFormData: false,
      favorites: true,
      history: false,
      index: 2,
      name: 'Bookmarks HTML File',
      passwords: false,
      profileName: '',
      search: false,
    },
  ];

  const stats: ArcImportStatsFixture = {
    sourceWorkspaces: 2,
    sourceItems: 12,
    workspaces: 2,
    folders: 3,
    pages: 5,
    splits: 0,
    degradedSplits: 1,
    topApps: 0,
    unsafeUrls: 2,
    unsupportedItems: 1,
    unreachableItems: 4,
    deduplicatedWorkspaces: 1,
    deduplicatedItems: 2,
    deduplicatedSplits: 1,
  };

  let dialog: SettingsImportDataDialogElement;
  let browserProxy: TestImportDataBrowserProxy;

  setup(async () => {
    browserProxy = new TestImportDataBrowserProxy(standardProfiles);
    ImportDataBrowserProxyImpl.setInstance(browserProxy);
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    dialog = document.createElement('settings-import-data-dialog');
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
    dialog.set('prefs', prefs);
    document.body.appendChild(dialog);
    await browserProxy.whenCalled('initializeImportDialog');
    flush();
  });

  function selectSource(index: number) {
    dialog.$.browserSelect.selectedIndex = index;
    dialog.$.browserSelect.dispatchEvent(new CustomEvent('change'));
    flush();
  }

  function getArcSection(): MutableArcImportSection {
    return dialog.shadowRoot!.querySelector<MutableArcImportSection>(
        '#ahoiArcImport')!;
  }

  function preview(splits: number) {
    return {
      alreadyImported: false,
      conflictingWorkspaces: 1,
      profiles: ['Default'],
      snapshotToken: 'fixture-token-without-source-data',
      sourceInUse: false,
      stats: {...stats, splits},
      status: 'ok',
      targetWorkspaces: ['Imported workspace'],
    };
  }

  test('arcUsesTheStandardSourceSelectAndCannotCallStandardImport', async () => {
    const optionLabels =
        Array.from(dialog.$.browserSelect.options).map(option =>
          option.textContent.trim());
    assertEquals(3, optionLabels.length);
    assertTrue(optionLabels[1]!.includes('Arc'));
    assertEquals('Bookmarks HTML File', optionLabels[2]);

    selectSource(1);
    const arcSection = getArcSection();
    assertFalse(arcSection.hidden);
    assertTrue(dialog.$.import.hidden);
    assertTrue(dialog.$.import.disabled);

    dialog.$.import.click();
    assertEquals(0, browserProxy.getCallCount('importData'));
    assertEquals(0, browserProxy.getCallCount('importFromBookmarksFile'));

    selectSource(2);
    assertTrue(arcSection.hidden);
    assertFalse(dialog.$.import.hidden);
    dialog.$.import.click();
    await browserProxy.whenCalled('importFromBookmarksFile');
    assertEquals(0, browserProxy.getCallCount('importData'));
  });

  test('splitChoiceOnlyAppearsForRealPreviewSplitsAndCommitIsConfirmed',
       async () => {
    selectSource(1);
    const arcSection = getArcSection();
    arcSection.arcImportStage_ = 'preview';
    arcSection.arcImportPreview_ = preview(0);
    arcSection.arcSelectedProfiles_ = ['Default'];
    arcSection.requestUpdate();
    await arcSection.updateComplete;

    assertFalse(!!arcSection.shadowRoot!.querySelector(
        '#ahoiArcReconstructSplits'));
    const commit = arcSection.shadowRoot!.querySelector<HTMLElement&{
      disabled: boolean,
    }>('#ahoiArcCommit')!;
    assertTrue(commit.disabled);

    arcSection.arcImportPreview_ = preview(2);
    arcSection.requestUpdate();
    await arcSection.updateComplete;
    assertTrue(!!arcSection.shadowRoot!.querySelector(
        '#ahoiArcReconstructSplits'));

    arcSection.shadowRoot!.querySelector<HTMLElement>(
        '#ahoiArcBackupConfirmation')!.click();
    await arcSection.updateComplete;
    assertTrue(commit.disabled);
    arcSection.shadowRoot!.querySelector<HTMLElement>(
        '#ahoiArcCommitConfirmation')!.click();
    await arcSection.updateComplete;
    assertFalse(commit.disabled);
  });

  test('resultReportsImportedSkippedDegradedExcludedAndFourPane', async () => {
    selectSource(1);
    const arcSection = getArcSection();
    arcSection.arcImportResult_ = {
      approximatedFourPaneRatios: 3,
      mergedWorkspaces: 0,
      reconstructedSplits: 2,
      renamedWorkspaces: 0,
      skippedWorkspaces: 1,
      stats,
      status: 'ok',
    };
    arcSection.arcImportStage_ = 'done';
    arcSection.requestUpdate();
    await arcSection.updateComplete;

    const resultText = (id: string) =>
      arcSection.shadowRoot!.querySelector(id)!.textContent!.trim();
    assertEquals('2', resultText('#ahoiArcResultWorkspaces'));
    assertEquals('1', resultText('#ahoiArcResultSkipped'));
    assertEquals('1', resultText('#ahoiArcResultDegraded'));
    assertEquals('7', resultText('#ahoiArcResultExcluded'));
    assertEquals('4', resultText('#ahoiArcResultDeduplicated'));
    assertEquals('3', resultText('#ahoiArcResultFourPane'));
  });
});
