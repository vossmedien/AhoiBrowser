// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The link routing card of the Ahoi settings page as its own element: the
// default target, ordered rules and the example tester, backed by the
// ahoiGetLinkRouting / ahoiLinkRoutingAction handler messages (split from
// ahoi_page.ts, source line budget).

import 'chrome://resources/cr_elements/cr_button/cr_button.js';

import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
import {sendWithPromise} from 'chrome://resources/js/cr.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getHtml} from './ahoi_link_routing.html.js';
import {getCss} from './ahoi_page.css.js';
import type {LinkRoutingDraft, LinkRoutingExampleResponse, LinkRoutingMode,
  LinkRoutingRule, LinkRoutingStatusResponse} from './ahoi_page_types.js';

const SettingsAhoiLinkRoutingElementBase = WebUiListenerMixinLit(CrLitElement);

export class SettingsAhoiLinkRoutingElement extends
    SettingsAhoiLinkRoutingElementBase {
  static get is() {
    return 'settings-ahoi-link-routing';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
    linkRouting_: {type: Object},
    linkRoutingPending_: {type: Boolean},
    linkRoutingErrorRuleId_: {type: String},
    linkRoutingDraft_: {type: Object},
    linkRoutingExampleInput_: {type: String},
    linkRoutingExample_: {type: Object},
    linkRoutingResetArmed_: {type: Boolean},
    };
  }

  protected accessor linkRouting_: LinkRoutingStatusResponse|null = null;
  protected accessor linkRoutingPending_: boolean = false;
  // Which editor part the shown error belongs to: a rule id, 'new' for the
  // add form, 'default' for the default route, '' for the whole editor.
  protected accessor linkRoutingErrorRuleId_: string = '';
  protected accessor linkRoutingDraft_: LinkRoutingDraft = {
    host: '',
    includeSubdomains: false,
    path: '',
    target: '',
    mode: 'normal_tab',
  };
  protected accessor linkRoutingExampleInput_: string = '';
  protected accessor linkRoutingExample_: LinkRoutingExampleResponse|null =
      null;
  protected accessor linkRoutingResetArmed_: boolean = false;
  private linkRoutingExampleSequence_: number = 0;

  override connectedCallback() {
    super.connectedCallback();
    this.addWebUiListener(
        'ahoi-link-routing-changed', (status: LinkRoutingStatusResponse) => {
          this.applyLinkRoutingStatus_(status);
          void this.resolveLinkRoutingExample_();
        });
    void this.refreshLinkRouting_();
  }

  private applyLinkRoutingStatus_(status: LinkRoutingStatusResponse) {
    this.linkRouting_ = status;
    if (!status.error) {
      this.linkRoutingErrorRuleId_ = '';
    }
    const ids = status.workspaces.map(workspace => workspace.id);
    if (!ids.includes(this.linkRoutingDraft_.target)) {
      this.linkRoutingDraft_ = {...this.linkRoutingDraft_, target: ids[0] || ''};
    }
  }

  private async refreshLinkRouting_() {
    try {
      this.applyLinkRoutingStatus_(
          await sendWithPromise<LinkRoutingStatusResponse>(
              'ahoiGetLinkRouting'));
    } catch {
      this.linkRouting_ = null;
    }
  }

  private async runLinkRoutingAction_(
      action: string, payload: Record<string, unknown>,
      errorScope: string = ''): Promise<boolean> {
    if (this.linkRoutingPending_ || !this.linkRouting_?.canChange) {
      return false;
    }
    this.linkRoutingPending_ = true;
    try {
      const status = await sendWithPromise<LinkRoutingStatusResponse>(
          'ahoiLinkRoutingAction', action, payload);
      this.applyLinkRoutingStatus_(status);
      // A rejected edit is never dropped silently: the reason stays visible
      // next to the part that caused it until the next successful change.
      this.linkRoutingErrorRuleId_ = status.error ? errorScope : '';
      return status.action === 'saved';
    } catch {
      await this.refreshLinkRouting_();
      return false;
    } finally {
      this.linkRoutingPending_ = false;
      void this.resolveLinkRoutingExample_();
    }
  }

  protected linkRoutingRulePayload_(
      rule: LinkRoutingRule,
      overrides: Partial<LinkRoutingRule> = {}): Record<string, unknown> {
    const merged = {...rule, ...overrides};
    return {
      id: merged.id,
      enabled: merged.enabled,
      host: merged.host,
      includeSubdomains: merged.includeSubdomains,
      path: merged.path,
      target: merged.target,
      mode: merged.mode,
    };
  }

  private findLinkRoutingRule_(event: Event): LinkRoutingRule|undefined {
    const id = (event.currentTarget as HTMLElement).dataset['ruleId'];
    return this.linkRouting_?.rules.find(rule => rule.id === id);
  }

  protected onLinkRoutingEnabledChange_(event: Event) {
    const checkbox = event.currentTarget as HTMLInputElement;
    const enabled = checkbox.checked;
    checkbox.checked = this.linkRouting_?.enabled ?? false;
    void this.runLinkRoutingAction_('setEnabled', {enabled});
  }

  protected onLinkRoutingRuleFieldChange_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    const input = event.currentTarget as HTMLInputElement | HTMLSelectElement;
    const field = input.dataset['field'] as keyof LinkRoutingRule | undefined;
    if (!rule || !field) {
      return;
    }
    const value = input instanceof HTMLInputElement && input.type === 'checkbox' ?
        input.checked :
        input.value;
    if (input instanceof HTMLInputElement && input.type === 'checkbox') {
      // Keep the stored state until the backend confirms the change.
      input.checked = rule[field] as boolean;
    }
    void this.runLinkRoutingAction_(
        'updateRule', this.linkRoutingRulePayload_(rule, {[field]: value}),
        rule.id);
  }

  protected onLinkRoutingRuleDeleteClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_('deleteRule', {id: rule.id}, rule.id);
    }
  }

  protected onLinkRoutingRuleMoveUpClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_(
          'moveRule', {id: rule.id, delta: -1}, rule.id);
    }
  }

  protected onLinkRoutingRuleMoveDownClick_(event: Event) {
    const rule = this.findLinkRoutingRule_(event);
    if (rule) {
      void this.runLinkRoutingAction_(
          'moveRule', {id: rule.id, delta: 1}, rule.id);
    }
  }

  protected isLinkRoutingLocked_(): boolean {
    return !this.linkRouting_?.canChange || this.linkRoutingPending_;
  }

  protected isLinkRoutingError_(scope: string): boolean {
    return !!this.linkRouting_?.error &&
        this.linkRoutingErrorRuleId_ === scope;
  }

  protected onLinkRoutingDraftInput_(event: Event) {
    this.onLinkRoutingDraftChange_(event);
  }

  protected onLinkRoutingDraftChange_(event: Event) {
    const input = event.currentTarget as HTMLInputElement | HTMLSelectElement;
    const field = input.dataset['field'] as keyof LinkRoutingDraft | undefined;
    if (!field) {
      return;
    }
    const value = input instanceof HTMLInputElement && input.type === 'checkbox' ?
        input.checked :
        input.value;
    this.linkRoutingDraft_ = {...this.linkRoutingDraft_, [field]: value};
  }

  protected async onLinkRoutingAddClick_() {
    const saved = await this.runLinkRoutingAction_(
        'addRule', {...this.linkRoutingDraft_, enabled: true}, 'new');
    if (saved) {
      this.linkRoutingDraft_ = {
        ...this.linkRoutingDraft_,
        host: '',
        path: '',
        includeSubdomains: false,
      };
    }
  }

  protected onLinkRoutingDefaultChange_(event: Event) {
    const select = event.currentTarget as HTMLSelectElement;
    const current = this.linkRouting_?.defaultRoute;
    if (!current) {
      return;
    }
    const payload = {target: current.target, mode: current.mode};
    if (select.dataset['field'] === 'mode') {
      payload.mode = select.value as LinkRoutingMode;
    } else {
      payload.target = select.value;
    }
    void this.runLinkRoutingAction_('setDefault', payload, 'default');
  }

  protected async onLinkRoutingResetClick_() {
    if (!this.linkRoutingResetArmed_) {
      this.linkRoutingResetArmed_ = true;
      return;
    }
    this.linkRoutingResetArmed_ = false;
    await this.runLinkRoutingAction_('reset', {});
  }

  protected onLinkRoutingExampleInput_(event: Event) {
    this.linkRoutingExampleInput_ =
        (event.currentTarget as HTMLInputElement).value;
    void this.resolveLinkRoutingExample_();
  }

  private async resolveLinkRoutingExample_() {
    const sequence = ++this.linkRoutingExampleSequence_;
    const input = this.linkRoutingExampleInput_;
    if (!input.trim()) {
      this.linkRoutingExample_ = null;
      return;
    }
    try {
      const result = await sendWithPromise<LinkRoutingExampleResponse>(
          'ahoiResolveLinkRoutingExample', input);
      if (sequence === this.linkRoutingExampleSequence_) {
        this.linkRoutingExample_ = result;
      }
    } catch {
      if (sequence === this.linkRoutingExampleSequence_) {
        this.linkRoutingExample_ = null;
      }
    }
  }

  protected linkRoutingWorkspaceLabel_(
      workspace: {name: string, separated: boolean}): string {
    return workspace.separated ?
        `${workspace.name} (${this.linkRouting_?.labels.separated || ''})` :
        workspace.name;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-ahoi-link-routing': SettingsAhoiLinkRoutingElement;
  }
}

customElements.define(
    SettingsAhoiLinkRoutingElement.is, SettingsAhoiLinkRoutingElement);
