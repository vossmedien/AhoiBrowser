// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CHROMIUM_ADAPTERS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CHROMIUM_ADAPTERS_H_

#include <memory>

#include "ahoi/browser/developer_toolkit/developer_toolkit_browsing_data.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_content_settings.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/memory/raw_ptr.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace ahoi {

// Production boundary around content::BrowsingDataRemover. The supplied
// BrowserContext must be the active tab's context so off-the-record tabs stay
// inside their own in-memory profile.
class ChromiumBrowsingDataRemovalAdapter final
    : public BrowsingDataRemovalAdapter {
 public:
  explicit ChromiumBrowsingDataRemovalAdapter(
      content::BrowserContext* browser_context);
  ChromiumBrowsingDataRemovalAdapter(
      const ChromiumBrowsingDataRemovalAdapter&) = delete;
  ChromiumBrowsingDataRemovalAdapter& operator=(
      const ChromiumBrowsingDataRemovalAdapter&) = delete;
  ~ChromiumBrowsingDataRemovalAdapter() override;

  bool Remove(const BrowsingDataClearRequest& request,
              CompletionCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Production boundary around HostContentSettingsMap. HostContentSettingsMap
// is obtained from the same BrowserContext, preserving incognito inheritance
// and keeping JavaScript/images rules origin-scoped.
class ChromiumContentSettingsAdapter final : public ContentSettingsAdapter {
 public:
  explicit ChromiumContentSettingsAdapter(
      content::BrowserContext* browser_context);
  ChromiumContentSettingsAdapter(const ChromiumContentSettingsAdapter&) =
      delete;
  ChromiumContentSettingsAdapter& operator=(
      const ChromiumContentSettingsAdapter&) = delete;
  ~ChromiumContentSettingsAdapter() override;

  std::optional<ContentSettingValue> Get(
      const url::Origin& origin,
      ContentSettingType type) const override;
  bool Set(const url::Origin& origin,
           ContentSettingType type,
           ContentSettingValue value) override;
  bool Reset(const url::Origin& origin, ContentSettingType type) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Executes only the fixed payload returned by GetDocumentActionScript(). The
// world id is product-owned and is never supplied by a UI caller.
class ChromiumDocumentActionExecutor {
 public:
  using CompletionCallback = base::OnceCallback<void(bool)>;

  ChromiumDocumentActionExecutor() = default;
  ChromiumDocumentActionExecutor(const ChromiumDocumentActionExecutor&) =
      delete;
  ChromiumDocumentActionExecutor& operator=(
      const ChromiumDocumentActionExecutor&) = delete;
  ~ChromiumDocumentActionExecutor() = default;

  bool Execute(content::WebContents* web_contents, DocumentAction action) const;
  bool ExecuteAndReply(content::WebContents* web_contents,
                       DocumentAction action,
                       CompletionCallback callback) const;
};

// Creates the two browser-context adapters used by DeveloperToolkit. The
// caller owns the returned objects and invokes them only on the UI sequence.
std::unique_ptr<BrowsingDataRemovalAdapter>
CreateChromiumBrowsingDataRemovalAdapter(content::BrowserContext* context);
std::unique_ptr<ContentSettingsAdapter> CreateChromiumContentSettingsAdapter(
    content::BrowserContext* context);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CHROMIUM_ADAPTERS_H_
