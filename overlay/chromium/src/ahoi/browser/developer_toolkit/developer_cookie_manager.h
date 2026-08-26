// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace ahoi {

// UI-facing values deliberately mirror only the cookie attributes users can
// safely inspect or edit. Chromium's CanonicalCookie remains owned by the
// production adapter so deletion and partition-key identity never have to be
// reconstructed from untrusted text fields.
enum class DeveloperCookieSameSite {
  kUnspecified,
  kNone,
  kLax,
  kStrict,
};

enum class DeveloperCookieExpiration {
  kKeep,
  kSession,
  kOneDay,
  kSevenDays,
  kThirtyDays,
  kOneYear,
};

struct DeveloperCookie {
  uint64_t id = 0;
  std::string name;
  std::string value;
  std::string domain;
  std::string path;
  bool secure = false;
  bool http_only = false;
  bool partitioned = false;
  DeveloperCookieSameSite same_site = DeveloperCookieSameSite::kUnspecified;
  base::Time expiration;

  bool is_session() const { return expiration.is_null(); }
};

struct DeveloperCookieDraft {
  std::string name;
  std::string value;
  // A leading dot means a domain cookie. A bare current host means host-only.
  std::string domain;
  std::string path = "/";
  bool secure = false;
  bool http_only = false;
  DeveloperCookieSameSite same_site = DeveloperCookieSameSite::kUnspecified;
  DeveloperCookieExpiration expiration = DeveloperCookieExpiration::kSession;
};

enum class DeveloperCookieError {
  kNone,
  kUnsupportedTarget,
  kInvalidName,
  kInvalidValue,
  kInvalidDomain,
  kInvalidPath,
  kInvalidSameSite,
  kInvalidPrefix,
  kInvalidExpiration,
  kNotFound,
  kRejected,
  kPartiallySucceeded,
  kUnavailable,
};

struct DeveloperCookieValidation {
  DeveloperCookieError error = DeveloperCookieError::kNone;
  DeveloperCookieDraft normalized;

  bool valid() const { return error == DeveloperCookieError::kNone; }
};

struct DeveloperCookieResult {
  DeveloperCookieError error = DeveloperCookieError::kNone;

  bool succeeded() const { return error == DeveloperCookieError::kNone; }
};

struct DeveloperCookieLoadResult : DeveloperCookieResult {
  std::vector<DeveloperCookie> cookies;
};

using DeveloperCookieLoadCallback =
    base::OnceCallback<void(DeveloperCookieLoadResult)>;
using DeveloperCookieMutationCallback =
    base::OnceCallback<void(DeveloperCookieResult)>;

// Performs synchronous validation before any cookie-store mutation. The
// returned draft contains trimmed/canonical domain and path values.
DeveloperCookieValidation ValidateDeveloperCookieDraft(
    const GURL& site_url,
    DeveloperCookieDraft draft,
    bool allow_keep_expiration);

// Case-insensitive ASCII filter used by the native search field. Cookie
// names, values, domains, and paths are all covered.
bool DeveloperCookieMatchesFilter(const DeveloperCookie& cookie,
                                  std::u16string_view filter);

class DeveloperCookieAdapter {
 public:
  virtual ~DeveloperCookieAdapter() = default;

  // A true return guarantees one asynchronous callback unless this adapter is
  // destroyed. IDs are scoped to the most recently completed Load() result.
  virtual bool Load(const GURL& site_url,
                    DeveloperCookieLoadCallback callback) = 0;
  virtual bool Save(const GURL& site_url,
                    std::optional<uint64_t> existing_cookie_id,
                    DeveloperCookieDraft draft,
                    DeveloperCookieMutationCallback callback) = 0;
  virtual bool Delete(const GURL& site_url,
                      uint64_t cookie_id,
                      DeveloperCookieMutationCallback callback) = 0;
  // Deletes one immutable selection snapshot. Implementations validate every
  // id before dispatch and report partial completion as one result instead of
  // racing one UI callback per row.
  virtual bool DeleteMany(const GURL& site_url,
                          std::vector<uint64_t> cookie_ids,
                          DeveloperCookieMutationCallback callback) = 0;
};

// Uses the default StoragePartition's browser-process CookieManager. It is
// created on demand by the cookie popup and schedules no idle work.
std::unique_ptr<DeveloperCookieAdapter> CreateChromiumDeveloperCookieAdapter(
    content::BrowserContext* browser_context);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_H_
