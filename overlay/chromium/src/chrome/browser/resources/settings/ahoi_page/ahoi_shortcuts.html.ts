// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Template of <settings-ahoi-shortcuts> (split from ahoi_page.html.ts,
// source line budget).

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAhoiShortcutsElement} from './ahoi_shortcuts.js';

export function getHtml(this: SettingsAhoiShortcutsElement) {
  // clang-format off
  return html`
      <div class="section-heading cr-row hr" ?hidden="${!this.shortcuts_}">
        <div class="flex cr-padded-text">
          <div id="ahoiShortcutsTitle">${this.shortcuts_?.labels.title || ''}</div>
          <div class="secondary">${this.shortcuts_?.labels.description || ''}</div>
        </div>
      </div>
      <section id="ahoiShortcuts" class="link-routing-card"
          aria-labelledby="ahoiShortcutsTitle"
          aria-busy="${this.shortcutPending_}" ?hidden="${!this.shortcuts_}">
        <input id="ahoiShortcutSearch" class="shortcut-search" type="search"
            placeholder="${this.shortcuts_?.labels.search || ''}"
            aria-label="${this.shortcuts_?.labels.search || ''}"
            .value="${this.shortcutQuery_}"
            spellcheck="false" autocomplete="off"
            @input="${this.onShortcutSearchInput_}">
        <div id="ahoiShortcutList" class="shortcut-list" role="list">
          ${this.filteredShortcuts_().length ? '' : html`
            <div class="secondary">${this.shortcuts_?.labels.noMatch || ''}</div>`}
          ${this.filteredShortcuts_().map(command => html`
            <div class="shortcut-row" role="listitem"
                data-command-id="${command.id}">
              <div class="shortcut-name">
                <span>${command.title}</span>
                <span class="secondary">${command.categoryLabel}</span>
              </div>
              <button class="shortcut-keys
                  ${this.isShortcutRecording_(command.id) ? 'recording' : ''}"
                  data-command-id="${command.id}"
                  aria-label="${command.title}: ${this.shortcutKeysText_(command)}"
                  ?disabled="${!command.rebindable || this.isShortcutLocked_()}"
                  @click="${this.onShortcutChangeClick_}"
                  @keydown="${this.onShortcutRecordKeydown_}"
                  @blur="${this.onShortcutRecordBlur_}">
                ${this.isShortcutRecording_(command.id) ?
                    (this.shortcuts_?.labels.recording || '') :
                    this.shortcutKeysText_(command)}
              </button>
              <span class="secondary" ?hidden="${command.rebindable}">
                ${this.shortcuts_?.labels.fixed || ''}
              </span>
              <cr-button data-command-id="${command.id}"
                  ?hidden="${!command.rebindable}"
                  ?disabled="${this.isShortcutLocked_() || !command.keys.length}"
                  @click="${this.onShortcutUnbindClick_}">
                ${this.shortcuts_?.labels.unbind || ''}
              </cr-button>
              <cr-button data-command-id="${command.id}"
                  ?hidden="${!command.rebindable}"
                  ?disabled="${this.isShortcutLocked_() || !command.customized}"
                  title="${this.shortcuts_?.labels.defaultIs || ''} ${command.defaultKeys.join('  ')}"
                  @click="${this.onShortcutResetClick_}">
                ${this.shortcuts_?.labels.reset || ''}
              </cr-button>
              <div class="link-routing-error shortcut-error" role="alert"
                  ?hidden="${!this.isShortcutError_(command.id)}">
                ${this.shortcuts_?.errorLabel || ''}
              </div>
            </div>`)}
        </div>
        <cr-button id="ahoiShortcutResetAll"
            ?disabled="${this.isShortcutLocked_()}"
            @click="${this.onShortcutResetAllClick_}">
          ${this.shortcuts_?.labels.resetAll || ''}
        </cr-button>
        <div class="link-routing-error" role="alert"
            ?hidden="${!this.isShortcutError_('')}">
          ${this.shortcuts_?.errorLabel || ''}
        </div>
      </section>
  `;
  // clang-format on
}
