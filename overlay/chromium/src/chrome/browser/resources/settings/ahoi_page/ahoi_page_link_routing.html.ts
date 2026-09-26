// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The link routing card of the Ahoi settings page: default target, ordered
// rules and the example tester (split from ahoi_page.html.ts, source line
// budget).

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAhoiPageElement} from './ahoi_page.js';

export function getHtml(this: SettingsAhoiPageElement) {
  // clang-format off
  return html`
      <div class="section-heading cr-row hr" ?hidden="${!this.linkRouting_}">
        <div class="flex cr-padded-text">
          <div id="ahoiLinkRoutingTitle">${this.linkRouting_?.labels?.title || ''}</div>
          <div class="secondary">${this.linkRouting_?.labels?.description || ''}</div>
        </div>
      </div>
      <section id="ahoiLinkRouting" class="link-routing-card"
          aria-labelledby="ahoiLinkRoutingTitle"
          aria-busy="${this.linkRoutingPending_}" ?hidden="${!this.linkRouting_}">
        <label class="link-routing-option">
          <input id="ahoiLinkRoutingEnabled" type="checkbox"
              .checked="${this.linkRouting_?.enabled ?? false}"
              ?disabled="${this.isLinkRoutingLocked_()}"
              @change="${this.onLinkRoutingEnabledChange_}">
          <span>${this.linkRouting_?.labels?.enabled || ''}</span>
        </label>
        <div class="link-routing-heading">${this.linkRouting_?.labels?.rules || ''}</div>
        <div id="ahoiLinkRoutingRules" class="link-routing-rules">
          ${this.linkRouting_?.rules.length ? '' : html`
            <div class="secondary">${this.linkRouting_?.labels?.noRules || ''}</div>`}
          ${this.linkRouting_?.rules.map((rule, index) => html`
            <div class="link-routing-rule
                ${rule.id === this.linkRoutingExample_?.ruleId ? 'winning' : ''}
                ${rule.enabled ? '' : 'disabled-rule'}"
                role="group" data-rule-id="${rule.id}"
                aria-label="${rule.host}${rule.path}">
              <label class="link-routing-option">
                <input type="checkbox" data-rule-id="${rule.id}"
                    data-field="enabled" .checked="${rule.enabled}"
                    ?disabled="${this.isLinkRoutingLocked_()}"
                    @change="${this.onLinkRoutingRuleFieldChange_}">
                <span>${this.linkRouting_?.labels?.ruleEnabled || ''}</span>
              </label>
              <div class="link-routing-fields">
                <label class="link-routing-field">
                  <span class="secondary">${this.linkRouting_?.labels?.host || ''}</span>
                  <input type="text" data-rule-id="${rule.id}"
                      data-field="host" .value="${rule.host}"
                      spellcheck="false" autocomplete="off"
                      ?disabled="${this.isLinkRoutingLocked_()}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                  <span class="secondary">
                    ${this.linkRouting_?.labels?.hostPortHint || ''}
                  </span>
                </label>
                <label class="link-routing-field">
                  <span class="secondary">${this.linkRouting_?.labels?.path || ''}</span>
                  <input type="text" data-rule-id="${rule.id}"
                      data-field="path" .value="${rule.path}"
                      spellcheck="false" autocomplete="off"
                      ?disabled="${this.isLinkRoutingLocked_()}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                </label>
              </div>
              <label class="link-routing-option">
                <input type="checkbox" data-rule-id="${rule.id}"
                    data-field="includeSubdomains"
                    .checked="${rule.includeSubdomains}"
                    ?disabled="${this.isLinkRoutingLocked_()}"
                    @change="${this.onLinkRoutingRuleFieldChange_}">
                <span>${this.linkRouting_?.labels?.includeSubdomains || ''}</span>
              </label>
              <div class="link-routing-fields">
                <label class="link-routing-field">
                  <span class="secondary">${this.linkRouting_?.labels?.target || ''}</span>
                  <select class="md-select" data-rule-id="${rule.id}"
                      data-field="target" ?disabled="${this.isLinkRoutingLocked_()}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                    ${rule.targetAvailable ? '' : html`
                      <option value="${rule.target}" .selected="${true}">
                        ${this.linkRouting_?.labels?.unavailableTarget || ''}
                      </option>`}
                    ${this.linkRouting_?.workspaces.map(workspace => html`
                      <option value="${workspace.id}"
                          .selected="${workspace.id === rule.target}">
                        ${this.linkRoutingWorkspaceLabel_(workspace)}
                      </option>`)}
                  </select>
                </label>
                <label class="link-routing-field">
                  <span class="secondary">${this.linkRouting_?.labels?.mode || ''}</span>
                  <select class="md-select" data-rule-id="${rule.id}"
                      data-field="mode" ?disabled="${this.isLinkRoutingLocked_()}"
                      @change="${this.onLinkRoutingRuleFieldChange_}">
                    <option value="normal_tab"
                        .selected="${rule.mode === 'normal_tab'}">
                      ${this.linkRouting_?.labels?.normalTab || ''}
                    </option>
                    <option value="quick_window"
                        .selected="${rule.mode === 'quick_window'}">
                      ${this.linkRouting_?.labels?.quickWindow || ''}
                    </option>
                  </select>
                </label>
              </div>
              <div class="link-routing-warning secondary"
                  ?hidden="${rule.targetAvailable}">
                ${this.linkRouting_?.labels?.rememberHint || ''}
              </div>
              <div class="link-routing-actions">
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${this.isLinkRoutingLocked_() || index === 0}"
                    @click="${this.onLinkRoutingRuleMoveUpClick_}">
                  ${this.linkRouting_?.labels?.moveUp || ''}
                </cr-button>
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${this.isLinkRoutingLocked_() ||
                        index === (this.linkRouting_?.rules.length ?? 0) - 1}"
                    @click="${this.onLinkRoutingRuleMoveDownClick_}">
                  ${this.linkRouting_?.labels?.moveDown || ''}
                </cr-button>
                <cr-button data-rule-id="${rule.id}"
                    ?disabled="${this.isLinkRoutingLocked_()}"
                    @click="${this.onLinkRoutingRuleDeleteClick_}">
                  ${this.linkRouting_?.labels?.delete || ''}
                </cr-button>
              </div>
              <div class="link-routing-error" role="alert"
                  ?hidden="${!this.isLinkRoutingError_(rule.id)}">
                ${this.linkRouting_?.errorLabel || ''}
              </div>
            </div>`)}
        </div>

        <div id="ahoiLinkRoutingAddRule" class="link-routing-add" role="group"
            aria-label="${this.linkRouting_?.labels?.add || ''}">
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.host || ''}</span>
              <input id="ahoiLinkRoutingAddHost" type="text" data-field="host"
                  .value="${this.linkRoutingDraft_.host}"
                  spellcheck="false" autocomplete="off"
                  aria-describedby="ahoiLinkRoutingAddPortHint"
                  ?disabled="${this.isLinkRoutingLocked_()}"
                  @input="${this.onLinkRoutingDraftInput_}">
              <span id="ahoiLinkRoutingAddPortHint" class="secondary">
                ${this.linkRouting_?.labels?.hostPortHint || ''}
              </span>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.path || ''}</span>
              <input id="ahoiLinkRoutingAddPath" type="text" data-field="path"
                  .value="${this.linkRoutingDraft_.path}"
                  spellcheck="false" autocomplete="off"
                  ?disabled="${this.isLinkRoutingLocked_()}"
                  @input="${this.onLinkRoutingDraftInput_}">
            </label>
          </div>
          <label class="link-routing-option">
            <input id="ahoiLinkRoutingAddSubdomains" type="checkbox"
                data-field="includeSubdomains"
                .checked="${this.linkRoutingDraft_.includeSubdomains}"
                ?disabled="${this.isLinkRoutingLocked_()}"
                @change="${this.onLinkRoutingDraftChange_}">
            <span>${this.linkRouting_?.labels?.includeSubdomains || ''}</span>
          </label>
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.target || ''}</span>
              <select id="ahoiLinkRoutingAddTarget" class="md-select"
                  data-field="target" ?disabled="${this.isLinkRoutingLocked_()}"
                  @change="${this.onLinkRoutingDraftChange_}">
                ${this.linkRouting_?.workspaces.map(workspace => html`
                  <option value="${workspace.id}"
                      .selected="${workspace.id === this.linkRoutingDraft_.target}">
                    ${this.linkRoutingWorkspaceLabel_(workspace)}
                  </option>`)}
              </select>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.mode || ''}</span>
              <select id="ahoiLinkRoutingAddMode" class="md-select"
                  data-field="mode" ?disabled="${this.isLinkRoutingLocked_()}"
                  @change="${this.onLinkRoutingDraftChange_}">
                <option value="normal_tab"
                    .selected="${this.linkRoutingDraft_.mode === 'normal_tab'}">
                  ${this.linkRouting_?.labels?.normalTab || ''}
                </option>
                <option value="quick_window"
                    .selected="${this.linkRoutingDraft_.mode === 'quick_window'}">
                  ${this.linkRouting_?.labels?.quickWindow || ''}
                </option>
              </select>
            </label>
          </div>
          <div class="link-routing-actions">
            <cr-button id="ahoiLinkRoutingAdd" class="action-button"
                ?disabled="${this.isLinkRoutingLocked_() ||
                    !this.linkRoutingDraft_.host.trim() ||
                    !this.linkRoutingDraft_.target}"
                @click="${this.onLinkRoutingAddClick_}">
              ${this.linkRouting_?.labels?.add || ''}
            </cr-button>
          </div>
          <div class="link-routing-error" role="alert"
              ?hidden="${!this.isLinkRoutingError_('new')}">
            ${this.linkRouting_?.errorLabel || ''}
          </div>
        </div>

        <div id="ahoiLinkRoutingDefault" class="link-routing-default"
            role="group" aria-label="${this.linkRouting_?.labels?.defaultRoute || ''}">
          <div class="link-routing-heading">
            ${this.linkRouting_?.labels?.defaultRoute || ''}
          </div>
          <div class="secondary">${this.linkRouting_?.labels?.defaultRouteHint || ''}</div>
          <div class="link-routing-fields">
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.target || ''}</span>
              <select id="ahoiLinkRoutingDefaultTarget" class="md-select"
                  data-field="target" ?disabled="${this.isLinkRoutingLocked_()}"
                  @change="${this.onLinkRoutingDefaultChange_}">
                <option value="last_active"
                    .selected="${this.linkRouting_?.defaultRoute.target === 'last_active'}">
                  ${this.linkRouting_?.labels?.lastActive || ''}
                </option>
                ${this.linkRouting_?.defaultRoute.targetAvailable ? '' : html`
                  <option value="${this.linkRouting_?.defaultRoute.target || ''}"
                      .selected="${true}">
                    ${this.linkRouting_?.labels?.unavailableTarget || ''}
                  </option>`}
                ${this.linkRouting_?.workspaces.map(workspace => html`
                  <option value="${workspace.id}"
                      .selected="${workspace.id === this.linkRouting_?.defaultRoute.target}">
                    ${this.linkRoutingWorkspaceLabel_(workspace)}
                  </option>`)}
              </select>
            </label>
            <label class="link-routing-field">
              <span class="secondary">${this.linkRouting_?.labels?.mode || ''}</span>
              <select id="ahoiLinkRoutingDefaultMode" class="md-select"
                  data-field="mode" ?disabled="${this.isLinkRoutingLocked_()}"
                  @change="${this.onLinkRoutingDefaultChange_}">
                <option value="normal_tab"
                    .selected="${this.linkRouting_?.defaultRoute.mode === 'normal_tab'}">
                  ${this.linkRouting_?.labels?.normalTab || ''}
                </option>
                <option value="quick_window"
                    .selected="${this.linkRouting_?.defaultRoute.mode === 'quick_window'}">
                  ${this.linkRouting_?.labels?.quickWindow || ''}
                </option>
              </select>
            </label>
          </div>
          <div class="link-routing-error" role="alert"
              ?hidden="${!this.isLinkRoutingError_('default')}">
            ${this.linkRouting_?.errorLabel || ''}
          </div>
        </div>

        <div class="link-routing-example">
          <label class="link-routing-field">
            <span class="link-routing-heading">
              ${this.linkRouting_?.labels?.example || ''}
            </span>
            <input id="ahoiLinkRoutingExample" type="text"
                .value="${this.linkRoutingExampleInput_}"
                placeholder="${this.linkRouting_?.labels?.examplePlaceholder || ''}"
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
              ?disabled="${this.isLinkRoutingLocked_()}"
              @click="${this.onLinkRoutingResetClick_}">
            ${this.linkRoutingResetArmed_ ? this.linkRouting_?.labels?.resetConfirm :
                                            this.linkRouting_?.labels?.reset}
          </cr-button>
        </div>
        <div id="ahoiLinkRoutingResetHint" class="secondary">
          ${this.linkRouting_?.labels?.resetHint || ''}
        </div>
        <div id="ahoiLinkRoutingError" class="link-routing-error" role="alert"
            ?hidden="${!this.isLinkRoutingError_('')}">
          ${this.linkRouting_?.errorLabel || ''}
        </div>
      </section>
  `;
  // clang-format on
}
