// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_chromium_adapters.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_document_actions.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "base/check.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/notreached.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/url_constants.h"

namespace ahoi {
namespace {

constexpr uint64_t kAllWebOriginTypes =
    content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
    content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB;

uint64_t ToChromiumRemovalMask(uint32_t data_type_mask, bool include_cookies) {
  uint64_t result = 0;
  if (data_type_mask & ToMask(BrowsingDataType::kCache)) {
    result |= content::BrowsingDataRemover::DATA_TYPE_CACHE;
  }
  if (include_cookies &&
      (data_type_mask & ToMask(BrowsingDataType::kCookies))) {
    result |= content::BrowsingDataRemover::DATA_TYPE_COOKIES;
  }
  if (data_type_mask & ToMask(BrowsingDataType::kLocalStorage)) {
    result |= content::BrowsingDataRemover::DATA_TYPE_LOCAL_STORAGE;
  }
  if (data_type_mask & ToMask(BrowsingDataType::kIndexedDb)) {
    result |= content::BrowsingDataRemover::DATA_TYPE_INDEXED_DB;
  }
  if (data_type_mask & ToMask(BrowsingDataType::kServiceWorkers)) {
    result |= content::BrowsingDataRemover::DATA_TYPE_SERVICE_WORKERS;
  }
  if (data_type_mask & ToMask(BrowsingDataType::kCacheStorage)) {
    result |= content::BrowsingDataRemover::DATA_TYPE_CACHE_STORAGE;
  }
  return result;
}

uint32_t FromChromiumRemovalMask(uint64_t failed_data_types) {
  uint32_t result = 0;
  if (failed_data_types & content::BrowsingDataRemover::DATA_TYPE_CACHE) {
    result |= ToMask(BrowsingDataType::kCache);
  }
  if (failed_data_types & content::BrowsingDataRemover::DATA_TYPE_COOKIES) {
    result |= ToMask(BrowsingDataType::kCookies);
  }
  if (failed_data_types &
      content::BrowsingDataRemover::DATA_TYPE_LOCAL_STORAGE) {
    result |= ToMask(BrowsingDataType::kLocalStorage);
  }
  if (failed_data_types & content::BrowsingDataRemover::DATA_TYPE_INDEXED_DB) {
    result |= ToMask(BrowsingDataType::kIndexedDb);
  }
  if (failed_data_types &
      content::BrowsingDataRemover::DATA_TYPE_SERVICE_WORKERS) {
    result |= ToMask(BrowsingDataType::kServiceWorkers);
  }
  if (failed_data_types &
      content::BrowsingDataRemover::DATA_TYPE_CACHE_STORAGE) {
    result |= ToMask(BrowsingDataType::kCacheStorage);
  }
  return result;
}

class BrowsingDataRemovalCompletion final
    : public base::RefCounted<BrowsingDataRemovalCompletion> {
 public:
  BrowsingDataRemovalCompletion(
      uint32_t requested_data_type_mask,
      size_t pending_tasks,
      BrowsingDataRemovalAdapter::CompletionCallback callback)
      : requested_data_type_mask_(requested_data_type_mask),
        pending_tasks_(pending_tasks),
        callback_(std::move(callback)) {
    CHECK_GT(pending_tasks_, 0u);
    CHECK(!callback_.is_null());
  }

  void Complete(uint32_t failed_data_type_mask) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    CHECK_GT(pending_tasks_, 0u);
    failed_data_type_mask_ |= failed_data_type_mask & requested_data_type_mask_;
    if (--pending_tasks_ != 0) {
      return;
    }
    std::move(callback_).Run(failed_data_type_mask_);
  }

 private:
  friend class base::RefCounted<BrowsingDataRemovalCompletion>;
  ~BrowsingDataRemovalCompletion() = default;

  const uint32_t requested_data_type_mask_;
  size_t pending_tasks_;
  uint32_t failed_data_type_mask_ = 0;
  BrowsingDataRemovalAdapter::CompletionCallback callback_;
};

class BrowsingDataTaskObserver final
    : public content::BrowsingDataRemover::Observer {
 public:
  BrowsingDataTaskObserver(
      content::BrowsingDataRemover* remover,
      scoped_refptr<BrowsingDataRemovalCompletion> completion)
      : completion_(std::move(completion)) {
    observation_.Observe(remover);
  }

  BrowsingDataTaskObserver(const BrowsingDataTaskObserver&) = delete;
  BrowsingDataTaskObserver& operator=(const BrowsingDataTaskObserver&) = delete;

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    observation_.Reset();
    completion_->Complete(FromChromiumRemovalMask(failed_data_types));
    delete this;
  }

 private:
  ~BrowsingDataTaskObserver() override = default;

  const scoped_refptr<BrowsingDataRemovalCompletion> completion_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      observation_{this};
};

base::Time BeginTime(BrowsingDataTimeRange range) {
  const base::Time now = base::Time::Now();
  switch (range) {
    case BrowsingDataTimeRange::kLastHour:
      return now - base::Hours(1);
    case BrowsingDataTimeRange::kLast24Hours:
      return now - base::Hours(24);
    case BrowsingDataTimeRange::kLast7Days:
      return now - base::Days(7);
    case BrowsingDataTimeRange::kLast4Weeks:
      return now - base::Days(28);
    case BrowsingDataTimeRange::kAllTime:
      return base::Time();
  }
  NOTREACHED();
}

ContentSettingsType ToChromiumContentSettingType(ContentSettingType type) {
  switch (type) {
    case ContentSettingType::kJavaScript:
      return ContentSettingsType::JAVASCRIPT;
    case ContentSettingType::kImages:
      return ContentSettingsType::IMAGES;
  }
  NOTREACHED();
}

ContentSetting ToChromiumContentSetting(ContentSettingValue value) {
  return value == ContentSettingValue::kAllow ? CONTENT_SETTING_ALLOW
                                              : CONTENT_SETTING_BLOCK;
}

std::optional<ContentSettingValue> FromChromiumContentSetting(
    ContentSetting setting) {
  switch (setting) {
    case CONTENT_SETTING_ALLOW:
      return ContentSettingValue::kAllow;
    case CONTENT_SETTING_BLOCK:
      return ContentSettingValue::kBlock;
    case CONTENT_SETTING_DEFAULT:
      return std::nullopt;
    case CONTENT_SETTING_ASK:
    case CONTENT_SETTING_SESSION_ONLY:
    case CONTENT_SETTING_NUM_SETTINGS:
      return std::nullopt;
  }
  NOTREACHED();
}

}  // namespace

ChromiumBrowsingDataRemovalAdapter::ChromiumBrowsingDataRemovalAdapter(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ChromiumBrowsingDataRemovalAdapter::~ChromiumBrowsingDataRemovalAdapter() =
    default;

bool ChromiumBrowsingDataRemovalAdapter::Remove(
    const BrowsingDataClearRequest& request,
    CompletionCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser_context_ || callback.is_null() || request.data_type_mask == 0 ||
      (request.data_type_mask & ~kAllDeveloperBrowsingDataTypes) != 0 ||
      (request.target == BrowsingDataTarget::kCurrentSite &&
       (!request.origin ||
        !IsSupportedDeveloperTargetUrl(request.origin->GetURL())))) {
    return false;
  }

  content::BrowsingDataRemover* remover =
      browser_context_->GetBrowsingDataRemover();
  if (!remover) {
    return false;
  }

  const base::Time begin = BeginTime(request.time_range);
  const base::Time end = base::Time::Max();
  const bool current_site = request.target == BrowsingDataTarget::kCurrentSite;
  const uint64_t removal_mask = ToChromiumRemovalMask(
      request.data_type_mask, /*include_cookies=*/!current_site);
  const bool clear_current_site_cookies =
      current_site &&
      (request.data_type_mask & ToMask(BrowsingDataType::kCookies));
  content::StoragePartition* partition =
      clear_current_site_cookies
          ? browser_context_->GetDefaultStoragePartition()
          : nullptr;
  const size_t pending_tasks =
      (removal_mask != 0 ? 1u : 0u) +
      (clear_current_site_cookies ? (partition ? 2u : 1u) : 0u);
  if (pending_tasks == 0) {
    return false;
  }

  auto completion = base::MakeRefCounted<BrowsingDataRemovalCompletion>(
      request.data_type_mask, pending_tasks, std::move(callback));
  if (removal_mask != 0) {
    auto* observer = new BrowsingDataTaskObserver(remover, completion);
    if (current_site) {
      auto filter = content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kDelete,
          content::BrowsingDataFilterBuilder::OriginMatchingMode::
              kOriginInAllContexts);
      filter->AddOrigin(*request.origin);
      remover->RemoveWithFilterAndReply(begin, end, removal_mask,
                                        kAllWebOriginTypes, std::move(filter),
                                        observer);
    } else {
      remover->RemoveAndReply(begin, end, removal_mask, kAllWebOriginTypes,
                              observer);
    }
  }

  if (clear_current_site_cookies) {
    // Cookie scope is host/domain based rather than origin based. Chromium's
    // origin filter intentionally refuses cookie deletion. A host_name filter
    // would delete host-only cookies but intentionally excludes domain
    // cookies; URL matching covers both for the active host without widening
    // the operation to sibling subdomains.
    if (partition) {
      const auto clear_cookies_for_url = [&](const GURL& url) {
        auto cookie_filter = network::mojom::CookieDeletionFilter::New();
        cookie_filter->url = url;
        partition->ClearData(
            content::StoragePartition::REMOVE_DATA_MASK_COOKIES,
            /*filter_builder=*/nullptr,
            content::StoragePartition::StorageKeyPolicyMatcherFunction(),
            std::move(cookie_filter), /*perform_storage_cleanup=*/false, begin,
            end,
            base::BindOnce(&BrowsingDataRemovalCompletion::Complete, completion,
                           0u));
      };

      const GURL origin_url = request.origin->GetURL();
      clear_cookies_for_url(origin_url);

      // Cookies do not carry an origin scheme. Clear the alternate HTTP(S)
      // view as well so an HTTPS page does not leave non-secure HTTP cookies
      // (or vice versa) behind for the same host.
      GURL::Replacements replacements;
      replacements.SetSchemeStr(request.origin->scheme() == url::kHttpScheme
                                    ? url::kHttpsScheme
                                    : url::kHttpScheme);
      clear_cookies_for_url(origin_url.ReplaceComponents(replacements));
    } else {
      completion->Complete(ToMask(BrowsingDataType::kCookies));
    }
  }
  return true;
}

ChromiumContentSettingsAdapter::ChromiumContentSettingsAdapter(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ChromiumContentSettingsAdapter::~ChromiumContentSettingsAdapter() = default;

std::optional<ContentSettingValue> ChromiumContentSettingsAdapter::Get(
    const url::Origin& origin,
    ContentSettingType type) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser_context_ || !IsSupportedDeveloperTargetUrl(origin.GetURL())) {
    return std::nullopt;
  }

  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(browser_context_);
  if (!settings_map) {
    return std::nullopt;
  }

  const GURL origin_url = origin.GetURL();
  return FromChromiumContentSetting(settings_map->GetContentSetting(
      origin_url, origin_url, ToChromiumContentSettingType(type)));
}

bool ChromiumContentSettingsAdapter::Set(const url::Origin& origin,
                                         ContentSettingType type,
                                         ContentSettingValue value) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser_context_ || !IsSupportedDeveloperTargetUrl(origin.GetURL())) {
    return false;
  }

  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(browser_context_);
  if (!settings_map) {
    return false;
  }

  const GURL origin_url = origin.GetURL();
  settings_map->SetContentSettingDefaultScope(
      origin_url, origin_url, ToChromiumContentSettingType(type),
      ToChromiumContentSetting(value));
  return true;
}

bool ChromiumContentSettingsAdapter::Reset(const url::Origin& origin,
                                           ContentSettingType type) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser_context_ || !IsSupportedDeveloperTargetUrl(origin.GetURL())) {
    return false;
  }
  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(browser_context_);
  if (!settings_map) {
    return false;
  }
  const GURL origin_url = origin.GetURL();
  settings_map->SetContentSettingDefaultScope(
      origin_url, origin_url, ToChromiumContentSettingType(type),
      CONTENT_SETTING_DEFAULT);
  return true;
}

bool ChromiumDocumentActionExecutor::Execute(content::WebContents* web_contents,
                                             DocumentAction action) const {
  return ExecuteAndReply(web_contents, action, base::DoNothing());
}

bool ChromiumDocumentActionExecutor::ExecuteAndReply(
    content::WebContents* web_contents,
    DocumentAction action,
    CompletionCallback callback) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!IsSupportedDeveloperTarget(web_contents)) {
    return false;
  }

  content::RenderFrameHost* frame = web_contents->GetPrimaryMainFrame();
  if (!frame) {
    return false;
  }

  const DocumentActionScript script = GetDocumentActionScript(action);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script.source),
      callback.is_null()
          ? content::RenderFrameHost::JavaScriptResultCallback()
          : base::BindOnce(
                [](CompletionCallback completed_callback, base::Value result) {
                  std::move(completed_callback)
                      .Run(result.is_bool() && result.GetBool());
                },
                std::move(callback)),
      kDeveloperToolkitIsolatedWorldId);
  return true;
}

std::unique_ptr<BrowsingDataRemovalAdapter>
CreateChromiumBrowsingDataRemovalAdapter(content::BrowserContext* context) {
  return std::make_unique<ChromiumBrowsingDataRemovalAdapter>(context);
}

std::unique_ptr<ContentSettingsAdapter> CreateChromiumContentSettingsAdapter(
    content::BrowserContext* context) {
  return std::make_unique<ChromiumContentSettingsAdapter>(context);
}

}  // namespace ahoi
