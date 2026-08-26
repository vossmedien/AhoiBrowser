// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_STORE_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_STORE_H_

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/memory/raw_ptr.h"

class PrefService;

namespace ahoi {

// Persistence boundary for per-origin developer profiles. Implementations are
// intentionally small so UI and navigation code can depend on this contract
// without owning PrefService or Chromium request plumbing.
class DeveloperProfileStore {
 public:
  virtual ~DeveloperProfileStore() = default;

  virtual std::optional<DeveloperProfile> Get(
      const url::Origin& origin) const = 0;
  virtual bool Set(const url::Origin& origin,
                   const DeveloperProfile& profile) = 0;
  virtual bool Remove(const url::Origin& origin) = 0;
  virtual std::vector<url::Origin> ListOrigins() const = 0;
};

// Stores schema-versioned dictionaries in a profile's PrefService. An
// off-the-record store is fail-closed unless explicitly opted into by the
// caller; an opted-in OTR PrefService remains ephemeral by Chromium design.
class PrefDeveloperProfileStore final : public DeveloperProfileStore {
 public:
  PrefDeveloperProfileStore(PrefService* prefs,
                            bool is_off_the_record,
                            bool allow_incognito_overrides = false);
  ~PrefDeveloperProfileStore() override;

  std::optional<DeveloperProfile> Get(const url::Origin& origin) const override;
  bool Set(const url::Origin& origin, const DeveloperProfile& profile) override;
  bool Remove(const url::Origin& origin) override;
  std::vector<url::Origin> ListOrigins() const override;

 private:
  bool CanAccess() const;

  const raw_ptr<PrefService> prefs_;
  const bool is_off_the_record_;
  const bool allow_incognito_overrides_;
};

// Deterministic test and embedding implementation. It has the same
// validation, origin and incognito policy as PrefDeveloperProfileStore.
class InMemoryDeveloperProfileStore final : public DeveloperProfileStore {
 public:
  explicit InMemoryDeveloperProfileStore(
      bool is_off_the_record = false,
      bool allow_incognito_overrides = false);
  ~InMemoryDeveloperProfileStore() override;

  std::optional<DeveloperProfile> Get(const url::Origin& origin) const override;
  bool Set(const url::Origin& origin, const DeveloperProfile& profile) override;
  bool Remove(const url::Origin& origin) override;
  std::vector<url::Origin> ListOrigins() const override;

 private:
  bool CanAccess() const;

  using Entry = std::pair<url::Origin, DeveloperProfile>;
  std::map<std::string, Entry> profiles_;
  const bool is_off_the_record_;
  const bool allow_incognito_overrides_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_STORE_H_
