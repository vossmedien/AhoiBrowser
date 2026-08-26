// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_document_actions.h"

#include <array>

namespace ahoi {
namespace {

constexpr std::string_view kToggleCssScript = R"JS(
(() => {
  const root = document.documentElement;
  const disabled = !root.hasAttribute('__ahoi_css_disabled__');
  document.querySelectorAll('style,link[rel="stylesheet"]').forEach((node) => {
    if (disabled) {
      node.disabled = true;
      node.setAttribute('data-ahoi-css-disabled', '');
    } else if (node.hasAttribute('data-ahoi-css-disabled')) {
      node.disabled = false;
      node.removeAttribute('data-ahoi-css-disabled');
    }
  });
  root.toggleAttribute('__ahoi_css_disabled__', disabled);
})();
)JS";

constexpr std::string_view kTogglePasswordFieldsScript = R"JS(
(() => {
  const isRevealed =
      document.querySelector('input[data-ahoi-password-revealed]') !== null;
  if (isRevealed) {
    document.querySelectorAll('input[data-ahoi-password-revealed]').forEach(
        (field) => {
      field.type = 'password';
      field.removeAttribute('data-ahoi-password-revealed');
    });
  } else {
    document.querySelectorAll('input[type="password"]').forEach((field) => {
      field.type = 'text';
      field.setAttribute('data-ahoi-password-revealed', '');
    });
  }
})();
)JS";

constexpr std::string_view kToggleStructureOutlinesScript = R"JS(
(() => {
  const id = '__ahoi_audit_outline_style__';
  const existing = document.getElementById(id);
  if (existing) {
    existing.remove();
    document.documentElement.removeAttribute('__ahoi_audit_outlines__');
    return true;
  }
  const style = document.createElement('style');
  style.id = id;
  style.textContent = `
    html[__ahoi_audit_outlines__] body * {
      outline: 1px solid rgba(255, 145, 0, .45) !important;
    }
    html[__ahoi_audit_outlines__] :is(h1,h2,h3,h4,h5,h6) {
      outline: 2px solid rgb(0, 174, 239) !important;
    }
    html[__ahoi_audit_outlines__]
      :is(header,nav,main,aside,footer,form,[role="banner"],
          [role="navigation"],[role="main"],[role="complementary"],
          [role="contentinfo"],[role="form"],[role="region"],
          [role="search"]) {
      outline: 3px solid rgb(213, 62, 219) !important;
    }
  `;
  document.documentElement.setAttribute('__ahoi_audit_outlines__', '');
  (document.head || document.documentElement).appendChild(style);
  return true;
})();
)JS";

constexpr std::string_view kToggleAltTitleLabelsScript = R"JS(
(() => {
  const marker = 'data-ahoi-audit-label';
  const existing = document.querySelectorAll(`[${marker}]`);
  if (existing.length) {
    existing.forEach((node) => node.remove());
    return true;
  }
  let count = 0;
  document.querySelectorAll('[alt],[title]').forEach((node) => {
    if (count >= 500) return;
    const parts = [];
    if (node.hasAttribute('alt')) parts.push(`alt: ${node.getAttribute('alt')}`);
    if (node.hasAttribute('title')) {
      parts.push(`title: ${node.getAttribute('title')}`);
    }
    if (!parts.length) return;
    const badge = document.createElement('span');
    badge.setAttribute(marker, '');
    badge.setAttribute('role', 'note');
    badge.textContent = parts.join(' · ').slice(0, 512);
    badge.style.cssText =
        'display:inline-block!important;position:relative!important;' +
        'z-index:2147483646!important;padding:2px 5px!important;' +
        'margin:2px!important;border-radius:3px!important;' +
        'font:11px/1.3 system-ui!important;color:#fff!important;' +
        'background:#6a38c2!important;white-space:normal!important;';
    node.insertAdjacentElement('afterend', badge);
    count += 1;
  });
  return true;
})();
)JS";

constexpr std::string_view kToggleDocumentMetadataScript = R"JS(
(() => {
  const id = '__ahoi_metadata_panel__';
  const existing = document.getElementById(id);
  if (existing) {
    existing.remove();
    return true;
  }
  const panel = document.createElement('aside');
  panel.id = id;
  panel.setAttribute('role', 'dialog');
  panel.setAttribute('aria-label', 'Ahoi Dokument-Metadaten');
  panel.style.cssText =
      'position:fixed!important;right:12px!important;bottom:12px!important;' +
      'z-index:2147483647!important;box-sizing:border-box!important;' +
      'width:min(520px,calc(100vw - 24px))!important;' +
      'max-height:min(70vh,640px)!important;overflow:auto!important;' +
      'padding:12px!important;border:1px solid #777!important;' +
      'border-radius:8px!important;background:#17191d!important;' +
      'color:#f7f7f7!important;font:12px/1.45 system-ui!important;';
  const heading = document.createElement('strong');
  heading.textContent = 'Meta · Canonical · OpenGraph · strukturierte Daten';
  panel.appendChild(heading);
  const list = document.createElement('pre');
  list.style.cssText = 'white-space:pre-wrap!important;margin:8px 0 0!important;';
  const entries = [];
  document.querySelectorAll('meta[name],meta[property]').forEach((node) => {
    if (entries.length >= 200) return;
    const key = node.getAttribute('name') || node.getAttribute('property');
    entries.push(`${key}: ${(node.getAttribute('content') || '').slice(0, 512)}`);
  });
  document.querySelectorAll('link[rel~="canonical"]').forEach((node) => {
    if (entries.length < 200) entries.push(`canonical: ${node.href}`);
  });
  document.querySelectorAll('script[type="application/ld+json"]').forEach(
      (node, index) => {
        if (entries.length < 200) {
          entries.push(`json-ld[${index}]: ${(node.textContent || '').slice(0, 512)}`);
        }
      });
  list.textContent = entries.length ? entries.join('\n') : 'Keine Daten gefunden';
  panel.appendChild(list);
  document.documentElement.appendChild(panel);
  return true;
})();
)JS";

constexpr std::string_view kClearSessionStorageScript = R"JS(
(() => {
  window.sessionStorage.clear();
  return true;
})();
)JS";

constexpr std::string_view kResetDocumentModificationsScript = R"JS(
(() => {
  document.querySelectorAll('input[data-ahoi-password-revealed]').forEach(
      (field) => {
    field.type = 'password';
    field.removeAttribute('data-ahoi-password-revealed');
  });
  document.querySelectorAll('[data-ahoi-css-disabled]').forEach((node) => {
    node.disabled = false;
    node.removeAttribute('data-ahoi-css-disabled');
  });
  document.documentElement.removeAttribute('__ahoi_css_disabled__');
  document.documentElement.removeAttribute('__ahoi_audit_outlines__');
  document.getElementById('__ahoi_audit_outline_style__')?.remove();
  document.getElementById('__ahoi_metadata_panel__')?.remove();
  document.querySelectorAll('[data-ahoi-audit-label]').forEach(
      (node) => node.remove());
  document.querySelectorAll('[data-ahoi-asset-style]').forEach(
      (node) => node.remove());
  document.getElementById('__ahoi_saved_css__')?.remove();
  return true;
})();
)JS";

constexpr std::array<DocumentActionScript, 7> kScripts = {{
    {DocumentAction::kToggleCss, kToggleCssScript},
    {DocumentAction::kTogglePasswordFields, kTogglePasswordFieldsScript},
    {DocumentAction::kToggleStructureOutlines, kToggleStructureOutlinesScript},
    {DocumentAction::kToggleAltTitleLabels, kToggleAltTitleLabelsScript},
    {DocumentAction::kToggleDocumentMetadata, kToggleDocumentMetadataScript},
    {DocumentAction::kClearSessionStorage, kClearSessionStorageScript},
    {DocumentAction::kResetDocumentModifications,
     kResetDocumentModificationsScript},
}};

}  // namespace

DocumentActionScript GetDocumentActionScript(DocumentAction action) {
  for (const DocumentActionScript& script : kScripts) {
    if (script.action == action) {
      return script;
    }
  }
  // All enum values are covered above; return the reset action defensively if
  // a malformed value crosses a non-C++ integration boundary.
  return kScripts.back();
}

bool IsFixedDocumentActionScript(std::string_view source) {
  for (const DocumentActionScript& script : kScripts) {
    if (script.source == source) {
      return true;
    }
  }
  return false;
}

}  // namespace ahoi
