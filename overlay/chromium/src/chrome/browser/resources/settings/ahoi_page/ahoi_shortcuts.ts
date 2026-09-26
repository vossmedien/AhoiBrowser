// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The keyboard shortcut editor of the Ahoi settings page as its own element:
// searching, recording, unbinding and resetting commands through the
// ahoiGetShortcuts / ahoiShortcutAction handler messages (split from
// ahoi_page.ts, source line budget).

import 'chrome://resources/cr_elements/cr_button/cr_button.js';

import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
import {sendWithPromise} from 'chrome://resources/js/cr.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss} from './ahoi_page.css.js';
import type {ShortcutCommandItem, ShortcutStatusResponse} from './ahoi_page_types.js';
import {getHtml} from './ahoi_shortcuts.html.js';

const SettingsAhoiShortcutsElementBase = WebUiListenerMixinLit(CrLitElement);

export class SettingsAhoiShortcutsElement extends
    SettingsAhoiShortcutsElementBase {
  static get is() {
    return 'settings-ahoi-shortcuts';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
    shortcuts_: {type: Object},
    shortcutQuery_: {type: String},
    shortcutRecordingId_: {type: String},
    shortcutErrorId_: {type: String},
    shortcutPending_: {type: Boolean},
    };
  }

  protected accessor shortcuts_: ShortcutStatusResponse|null = null;
  protected accessor shortcutQuery_: string = '';
  // The command whose new key is being recorded, or ''.
  protected accessor shortcutRecordingId_: string = '';
  // The command the shown error belongs to ('' for the whole editor).
  protected accessor shortcutErrorId_: string = '';
  protected accessor shortcutPending_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();
    this.addWebUiListener(
        'ahoi-shortcuts-changed', (status: ShortcutStatusResponse) => {
          this.shortcuts_ = status;
        });
    void this.refreshShortcuts_();
  }

  private async refreshShortcuts_() {
    try {
      this.shortcuts_ =
          await sendWithPromise<ShortcutStatusResponse>('ahoiGetShortcuts');
    } catch {
      this.shortcuts_ = null;
    }
  }

  private async runShortcutAction_(
      action: string, payload: Record<string, unknown>, scope: string) {
    if (this.shortcutPending_ || !this.shortcuts_?.canChange) {
      return;
    }
    this.shortcutPending_ = true;
    try {
      const status = await sendWithPromise<ShortcutStatusResponse>(
          'ahoiShortcutAction', action, payload);
      this.shortcuts_ = status;
      // A refused key stays explained next to its command.
      this.shortcutErrorId_ = status.errorLabel ? scope : '';
    } catch {
      await this.refreshShortcuts_();
    } finally {
      this.shortcutPending_ = false;
    }
  }

  protected filteredShortcuts_(): ShortcutCommandItem[] {
    const query = this.shortcutQuery_.trim().toLocaleLowerCase();
    const commands = this.shortcuts_?.commands || [];
    if (!query) {
      return commands;
    }
    return commands.filter(
        command => command.title.toLocaleLowerCase().includes(query) ||
            command.categoryLabel.toLocaleLowerCase().includes(query) ||
            command.keys.some(key => key.toLocaleLowerCase().includes(query)));
  }

  protected isShortcutLocked_(): boolean {
    return !this.shortcuts_?.canChange || this.shortcutPending_;
  }

  protected isShortcutError_(id: string): boolean {
    return !!this.shortcuts_?.errorLabel && this.shortcutErrorId_ === id;
  }

  protected isShortcutRecording_(id: string): boolean {
    return this.shortcutRecordingId_ === id;
  }

  protected shortcutKeysText_(command: ShortcutCommandItem): string {
    return command.keys.length ? command.keys.join('  ') :
                                 (this.shortcuts_?.labels.none || '');
  }

  private shortcutIdOf_(event: Event): string {
    return (event.currentTarget as HTMLElement).dataset['commandId'] || '';
  }

  protected onShortcutSearchInput_(event: Event) {
    this.shortcutQuery_ = (event.target as HTMLInputElement).value;
  }

  private setShortcutRecording_(active: boolean) {
    // Ahoi's own shortcuts step aside while a key is recorded; otherwise the
    // browser would run a bound key before the page sees it.
    chrome.send('ahoiSetShortcutRecording', [active]);
  }

  protected onShortcutChangeClick_(event: Event) {
    this.setShortcutRecording_(true);
    this.shortcutRecordingId_ = this.shortcutIdOf_(event);
    this.shortcutErrorId_ = '';
    (event.currentTarget as HTMLElement).focus();
  }

  protected onShortcutRecordKeydown_(event: KeyboardEvent) {
    const id = this.shortcutIdOf_(event);
    if (!id || this.shortcutRecordingId_ !== id) {
      return;
    }
    // The pressed key is taken by the editor, never by the page or browser.
    event.preventDefault();
    event.stopPropagation();
    if (['Shift', 'Control', 'Alt', 'Meta', 'CapsLock'].includes(event.key)) {
      return;
    }
    this.shortcutRecordingId_ = '';
    this.setShortcutRecording_(false);
    if (event.key === 'Escape' && !event.metaKey && !event.ctrlKey &&
        !event.altKey && !event.shiftKey) {
      return;
    }
    void this.runShortcutAction_(
        'set', {
          id,
          keyCode: event.keyCode,
          cmd: event.metaKey,
          ctrl: event.ctrlKey,
          alt: event.altKey,
          shift: event.shiftKey,
        },
        id);
  }

  protected onShortcutRecordBlur_() {
    if (this.shortcutRecordingId_) {
      this.setShortcutRecording_(false);
    }
    this.shortcutRecordingId_ = '';
  }

  protected onShortcutUnbindClick_(event: Event) {
    const id = this.shortcutIdOf_(event);
    void this.runShortcutAction_('unbind', {id}, id);
  }

  protected onShortcutResetClick_(event: Event) {
    const id = this.shortcutIdOf_(event);
    void this.runShortcutAction_('reset', {id}, id);
  }

  protected onShortcutResetAllClick_() {
    void this.runShortcutAction_('resetAll', {}, '');
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-ahoi-shortcuts': SettingsAhoiShortcutsElement;
  }
}

customElements.define(
    SettingsAhoiShortcutsElement.is, SettingsAhoiShortcutsElement);
