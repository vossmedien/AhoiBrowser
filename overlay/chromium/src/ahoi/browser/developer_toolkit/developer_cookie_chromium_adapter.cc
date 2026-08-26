// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_cookie_chromium_adapter.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_cookie_manager.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/origin.h"

namespace ahoi {

std::optional<net::CookiePartitionKey> ResolveDeveloperCookiePartitionKey(
    const GURL& site_url,
    bool partitioned,
    const std::optional<net::CookiePartitionKey>& existing_partition_key) {
  if (!partitioned) {
    return std::nullopt;
  }
  if (existing_partition_key) {
    return existing_partition_key;
  }
  return net::CookiePartitionKey::FromStorageKeyComponents(
      net::SchemefulSite(site_url),
      net::CookiePartitionKey::AncestorChainBit::kSameSite,
      /*nonce=*/std::nullopt);
}

namespace {

net::CookieSameSite ToChromiumSameSite(DeveloperCookieSameSite same_site) {
  switch (same_site) {
    case DeveloperCookieSameSite::kUnspecified:
      return net::CookieSameSite::UNSPECIFIED;
    case DeveloperCookieSameSite::kNone:
      return net::CookieSameSite::NO_RESTRICTION;
    case DeveloperCookieSameSite::kLax:
      return net::CookieSameSite::LAX_MODE;
    case DeveloperCookieSameSite::kStrict:
      return net::CookieSameSite::STRICT_MODE;
  }
}

DeveloperCookieSameSite FromChromiumSameSite(net::CookieSameSite same_site) {
  switch (same_site) {
    case net::CookieSameSite::UNSPECIFIED:
      return DeveloperCookieSameSite::kUnspecified;
    case net::CookieSameSite::NO_RESTRICTION:
      return DeveloperCookieSameSite::kNone;
    case net::CookieSameSite::LAX_MODE:
      return DeveloperCookieSameSite::kLax;
    case net::CookieSameSite::STRICT_MODE:
      return DeveloperCookieSameSite::kStrict;
  }
}

class CookieBatchDeleteState final
    : public base::RefCounted<CookieBatchDeleteState> {
 public:
  CookieBatchDeleteState(size_t pending,
                         DeveloperCookieMutationCallback callback)
      : pending_(pending), total_(pending), callback_(std::move(callback)) {
    CHECK_GT(pending_, 0u);
  }

  void Complete(bool deleted) {
    CHECK_GT(pending_, 0u);
    successful_ += deleted ? 1u : 0u;
    if (--pending_ != 0) {
      return;
    }
    DeveloperCookieError error = DeveloperCookieError::kNone;
    if (successful_ == 0) {
      error = DeveloperCookieError::kRejected;
    } else if (successful_ != total_) {
      error = DeveloperCookieError::kPartiallySucceeded;
    }
    std::move(callback_).Run({error});
  }

 private:
  friend class base::RefCounted<CookieBatchDeleteState>;
  ~CookieBatchDeleteState() = default;

  size_t pending_;
  const size_t total_;
  size_t successful_ = 0;
  DeveloperCookieMutationCallback callback_;
};

base::Time ExpirationForDraft(DeveloperCookieExpiration expiration,
                              const net::CanonicalCookie* existing,
                              base::Time now) {
  switch (expiration) {
    case DeveloperCookieExpiration::kKeep:
      return existing ? existing->ExpiryDate() : base::Time();
    case DeveloperCookieExpiration::kSession:
      return base::Time();
    case DeveloperCookieExpiration::kOneDay:
      return now + base::Days(1);
    case DeveloperCookieExpiration::kSevenDays:
      return now + base::Days(7);
    case DeveloperCookieExpiration::kThirtyDays:
      return now + base::Days(30);
    case DeveloperCookieExpiration::kOneYear:
      return now + base::Days(365);
  }
}

class ChromiumDeveloperCookieAdapter final : public DeveloperCookieAdapter {
 public:
  explicit ChromiumDeveloperCookieAdapter(content::BrowserContext* context)
      : browser_context_(context) {}
  ~ChromiumDeveloperCookieAdapter() override = default;

  bool Load(const GURL& site_url,
            DeveloperCookieLoadCallback callback) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    network::mojom::CookieManager* manager = GetCookieManager(site_url);
    if (!manager || callback.is_null()) {
      return false;
    }
    // Invalidate the previous ID snapshot before starting another load. The
    // native view disables mutations while this asynchronous refresh is in
    // flight, and internal callers cannot reuse stale IDs for another origin.
    cookies_.clear();
    loaded_origin_.reset();
    manager->GetAllCookies(base::BindOnce(
        &ChromiumDeveloperCookieAdapter::OnCookiesLoaded,
        weak_ptr_factory_.GetWeakPtr(), site_url, std::move(callback)));
    return true;
  }

  bool Save(const GURL& site_url,
            std::optional<uint64_t> existing_cookie_id,
            DeveloperCookieDraft draft,
            DeveloperCookieMutationCallback callback) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    network::mojom::CookieManager* manager = GetCookieManager(site_url);
    if (!manager || callback.is_null()) {
      return false;
    }

    const auto existing_it = existing_cookie_id
                                 ? cookies_.find(*existing_cookie_id)
                                 : cookies_.end();
    if (existing_cookie_id &&
        (!IsCurrentSnapshot(site_url) || existing_it == cookies_.end())) {
      std::move(callback).Run({DeveloperCookieError::kNotFound});
      return true;
    }
    const net::CanonicalCookie* existing =
        existing_it == cookies_.end() ? nullptr : &existing_it->second;
    DeveloperCookieValidation validation = ValidateDeveloperCookieDraft(
        site_url, std::move(draft), existing != nullptr);
    if (!validation.valid()) {
      std::move(callback).Run({validation.error});
      return true;
    }

    const base::Time now = base::Time::Now();
    net::CookieInclusionStatus creation_status;
    const bool host_only = !validation.normalized.domain.starts_with('.');
    // Preserve an existing key byte-for-byte. In particular, do not collapse
    // a cross-site ancestor bit merely because the editor is currently shown
    // in a first-party top-level tab. A newly partitioned cookie is explicitly
    // scoped to the current top-level site through Chromium's supported
    // storage-key factory.
    std::optional<net::CookiePartitionKey> partition_key =
        ResolveDeveloperCookiePartitionKey(
            site_url, validation.normalized.partitioned,
            existing ? existing->PartitionKey() : std::nullopt);
    if (validation.normalized.partitioned && !partition_key) {
      std::move(callback).Run({DeveloperCookieError::kRejected});
      return true;
    }
    std::unique_ptr<net::CanonicalCookie> cookie =
        net::CanonicalCookie::CreateSanitizedCookie(
            site_url, validation.normalized.name, validation.normalized.value,
            host_only ? std::string() : validation.normalized.domain,
            validation.normalized.path,
            existing ? existing->CreationDate() : now,
            ExpirationForDraft(validation.normalized.expiration, existing, now),
            now, validation.normalized.secure, validation.normalized.http_only,
            ToChromiumSameSite(validation.normalized.same_site),
            existing ? existing->Priority() : net::COOKIE_PRIORITY_MEDIUM,
            std::move(partition_key), &creation_status);
    if (!cookie || !creation_status.IsInclude()) {
      std::move(callback).Run({DeveloperCookieError::kRejected});
      return true;
    }

    net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
    net::CanonicalCookie replacement = *cookie;
    manager->SetCanonicalCookie(
        *cookie, site_url, options,
        base::BindOnce(&ChromiumDeveloperCookieAdapter::OnCookieSet,
                       weak_ptr_factory_.GetWeakPtr(), site_url,
                       existing ? std::make_optional(*existing) : std::nullopt,
                       std::move(replacement), std::move(callback)));
    return true;
  }

  bool Delete(const GURL& site_url,
              uint64_t cookie_id,
              DeveloperCookieMutationCallback callback) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    network::mojom::CookieManager* manager = GetCookieManager(site_url);
    if (!manager || callback.is_null()) {
      return false;
    }
    if (!IsCurrentSnapshot(site_url)) {
      std::move(callback).Run({DeveloperCookieError::kNotFound});
      return true;
    }
    const auto found = cookies_.find(cookie_id);
    if (found == cookies_.end()) {
      std::move(callback).Run({DeveloperCookieError::kNotFound});
      return true;
    }
    manager->DeleteCanonicalCookie(
        found->second,
        base::BindOnce(&ChromiumDeveloperCookieAdapter::OnCookieDeleted,
                       weak_ptr_factory_.GetWeakPtr(), cookie_id,
                       std::move(callback)));
    return true;
  }

  bool DeleteMany(const GURL& site_url,
                  std::vector<uint64_t> cookie_ids,
                  DeveloperCookieMutationCallback callback) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    network::mojom::CookieManager* manager = GetCookieManager(site_url);
    if (!manager || callback.is_null() || cookie_ids.empty()) {
      return false;
    }
    if (!IsCurrentSnapshot(site_url)) {
      std::move(callback).Run({DeveloperCookieError::kNotFound});
      return true;
    }

    std::ranges::sort(cookie_ids);
    cookie_ids.erase(std::unique(cookie_ids.begin(), cookie_ids.end()),
                     cookie_ids.end());
    std::vector<std::pair<uint64_t, net::CanonicalCookie>> targets;
    targets.reserve(cookie_ids.size());
    for (uint64_t cookie_id : cookie_ids) {
      const auto found = cookies_.find(cookie_id);
      if (found == cookies_.end()) {
        std::move(callback).Run({DeveloperCookieError::kNotFound});
        return true;
      }
      targets.emplace_back(cookie_id, found->second);
    }

    auto state = base::MakeRefCounted<CookieBatchDeleteState>(
        targets.size(), std::move(callback));
    for (const auto& [cookie_id, cookie] : targets) {
      manager->DeleteCanonicalCookie(
          cookie,
          base::BindOnce(
              &ChromiumDeveloperCookieAdapter::OnCookieBatchEntryDeleted,
              weak_ptr_factory_.GetWeakPtr(), state, cookie_id));
    }
    return true;
  }

 private:
  bool IsCurrentSnapshot(const GURL& site_url) const {
    return loaded_origin_ && *loaded_origin_ == url::Origin::Create(site_url);
  }

  network::mojom::CookieManager* GetCookieManager(const GURL& site_url) const {
    if (!browser_context_ || !IsSupportedDeveloperTargetUrl(site_url)) {
      return nullptr;
    }
    content::StoragePartition* partition =
        browser_context_->GetDefaultStoragePartition();
    return partition ? partition->GetCookieManagerForBrowserProcess() : nullptr;
  }

  void OnCookiesLoaded(const GURL& site_url,
                       DeveloperCookieLoadCallback callback,
                       const std::vector<net::CanonicalCookie>& cookies) {
    cookies_.clear();
    loaded_origin_ = url::Origin::Create(site_url);
    std::vector<DeveloperCookie> result;
    for (const net::CanonicalCookie& cookie : cookies) {
      if (!cookie.IsDomainMatch(site_url.host())) {
        continue;
      }
      const uint64_t id = next_cookie_id_++;
      cookies_.emplace(id, cookie);
      result.push_back(DeveloperCookie{
          .id = id,
          .name = cookie.Name(),
          .value = cookie.Value(),
          .domain = cookie.Domain(),
          .path = cookie.Path(),
          .secure = cookie.SecureAttribute(),
          .http_only = cookie.IsHttpOnly(),
          .partitioned = cookie.IsPartitioned(),
          .same_site = FromChromiumSameSite(cookie.SameSite()),
          .expiration = cookie.ExpiryDate(),
      });
    }
    std::ranges::sort(
        result, [](const DeveloperCookie& lhs, const DeveloperCookie& rhs) {
          return std::tie(lhs.name, lhs.domain, lhs.path) <
                 std::tie(rhs.name, rhs.domain, rhs.path);
        });
    std::move(callback).Run(DeveloperCookieLoadResult{
        {DeveloperCookieError::kNone}, std::move(result)});
  }

  void OnCookieSet(const GURL& site_url,
                   std::optional<net::CanonicalCookie> existing,
                   net::CanonicalCookie replacement,
                   DeveloperCookieMutationCallback callback,
                   net::CookieAccessResult access_result) {
    if (!access_result.status.IsInclude()) {
      std::move(callback).Run({DeveloperCookieError::kRejected});
      return;
    }
    if (!existing || existing->IsEquivalent(replacement)) {
      std::move(callback).Run({DeveloperCookieError::kNone});
      return;
    }

    network::mojom::CookieManager* manager = GetCookieManager(site_url);
    if (!manager) {
      std::move(callback).Run({DeveloperCookieError::kPartiallySucceeded});
      return;
    }
    manager->DeleteCanonicalCookie(
        *existing,
        base::BindOnce(
            [](DeveloperCookieMutationCallback completed, bool deleted) {
              std::move(completed).Run(
                  {deleted ? DeveloperCookieError::kNone
                           : DeveloperCookieError::kPartiallySucceeded});
            },
            std::move(callback)));
  }

  void OnCookieDeleted(uint64_t cookie_id,
                       DeveloperCookieMutationCallback callback,
                       bool deleted) {
    if (deleted) {
      cookies_.erase(cookie_id);
    }
    std::move(callback).Run({deleted ? DeveloperCookieError::kNone
                                     : DeveloperCookieError::kRejected});
  }

  void OnCookieBatchEntryDeleted(scoped_refptr<CookieBatchDeleteState> state,
                                 uint64_t cookie_id,
                                 bool deleted) {
    if (deleted) {
      cookies_.erase(cookie_id);
    }
    state->Complete(deleted);
  }

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::map<uint64_t, net::CanonicalCookie> cookies_;
  std::optional<url::Origin> loaded_origin_;
  uint64_t next_cookie_id_ = 1;
  base::WeakPtrFactory<ChromiumDeveloperCookieAdapter> weak_ptr_factory_{this};
};

}  // namespace

std::unique_ptr<DeveloperCookieAdapter> CreateChromiumDeveloperCookieAdapter(
    content::BrowserContext* browser_context) {
  return std::make_unique<ChromiumDeveloperCookieAdapter>(browser_context);
}

}  // namespace ahoi
