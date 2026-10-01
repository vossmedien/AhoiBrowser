// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Resolves a profile from the stored pref shape rather than from a
// DeveloperProfile struct: the installed-browser journey
// (tools/desktop_e2e/devtoolkit-journey.sh) seeds exactly this dictionary
// into a fresh profile. One invalid asset drops the whole profile, so a
// seed or validation drift turns every developer rule off at once.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi {
namespace {

constexpr char kSiteOrigin[] = "http://127.0.0.1:8808";

// Mirrors one origin entry of the journey seed.
constexpr char kSeededProfileJson[] = R"json({
  "name": "Journey",
  "assets": [
    {"id": "journey-css", "name": "journey-css", "kind": "style",
     "language": "css", "enabled": true,
     "source": "body{outline:7px solid rgb(1, 2, 3)}",
     "compiled_css": "", "compiled_style_version": 0,
     "scope": {"kind": "origin", "value": "http://127.0.0.1:8808"},
     "domain_scope_warning_accepted": false, "lifetime": "restart",
     "sync_enabled": false, "world": "isolated",
     "main_world_warning_accepted": false},
    {"id": "journey-less", "name": "journey-less", "kind": "style",
     "language": "less", "enabled": true,
     "source": "@c: rgb(4, 5, 6); p { color: @c; }",
     "compiled_css": "p{color:rgb(4, 5, 6)}", "compiled_style_version": 1,
     "scope": {"kind": "origin", "value": "http://127.0.0.1:8808"},
     "domain_scope_warning_accepted": false, "lifetime": "restart",
     "sync_enabled": false, "world": "isolated",
     "main_world_warning_accepted": false},
    {"id": "journey-js", "name": "journey-js", "kind": "javascript",
     "language": "css", "enabled": true,
     "source": "window.ahoiDevGlobal = 1;",
     "compiled_css": "", "compiled_style_version": 0,
     "scope": {"kind": "origin", "value": "http://127.0.0.1:8808"},
     "domain_scope_warning_accepted": false, "lifetime": "restart",
     "sync_enabled": false, "world": "isolated",
     "main_world_warning_accepted": false}
  ],
  "user_agent": {"enabled": false, "value": ""},
  "headers": {"enabled": true, "sync_enabled": false,
              "rules": [{"name": "X-Ahoi-Dev", "action": "set",
                         "value": "journey"}]},
  "response_headers": {"enabled": true, "sync_enabled": false,
                       "advanced_mode_acknowledged": false,
                       "rules": [{"name": "X-Ahoi-Resp", "action": "set",
                                  "value": "yes"}]},
  "cache_disabled": true
})json";

class DeveloperProfilePrefSeedTest : public testing::Test {
 protected:
  void SetUp() override {
    developer_profile_prefs::RegisterProfilePrefs(prefs_.registry());
  }

  // Stores the seeded profile with the LESS asset's compiler version
  // replaced by `less_version`.
  void Seed(int less_version) {
    std::optional<base::DictValue> profile =
        base::JSONReader::ReadDict(kSeededProfileJson, base::JSON_PARSE_RFC);
    ASSERT_TRUE(profile);
    base::ListValue* assets = profile->FindList("assets");
    ASSERT_TRUE(assets);
    ASSERT_EQ(assets->size(), 3u);
    (*assets)[1].GetDict().Set("compiled_style_version", less_version);
    base::DictValue origins;
    origins.Set(kSiteOrigin, std::move(*profile));
    prefs_.SetDict(kDeveloperProfilesPref,
                   base::DictValue()
                       .Set("version", kDeveloperProfileSchemaVersion)
                       .Set("origins", std::move(origins)));
  }

  std::vector<std::string> AssetIdsFor(const char* url) {
    PrefDeveloperProfileStore store(&prefs_, /*is_off_the_record=*/false);
    std::vector<std::string> ids;
    for (const DeveloperAsset& asset :
         GetDeveloperAssetsForNavigation(store, GURL(url))) {
      ids.push_back(asset.id);
    }
    return ids;
  }

  std::optional<DeveloperProfile> ProfileFor(const char* url) {
    PrefDeveloperProfileStore store(&prefs_, /*is_off_the_record=*/false);
    return GetDeveloperProfileForNavigation(store, GURL(url));
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(DeveloperProfilePrefSeedTest, SeededProfileResolvesForItsSiteOnly) {
  Seed(static_cast<int>(kDeveloperStyleCompilerVersion));

  const std::optional<DeveloperProfile> profile =
      ProfileFor("http://127.0.0.1:8808/page");
  ASSERT_TRUE(profile);
  EXPECT_TRUE(profile->header_rules_enabled);
  ASSERT_EQ(profile->header_rules.size(), 1u);
  EXPECT_EQ(profile->header_rules[0].value, "journey");
  EXPECT_TRUE(profile->response_header_rules_enabled);
  EXPECT_TRUE(profile->cache_disabled);
  EXPECT_EQ(AssetIdsFor("http://127.0.0.1:8808/page"),
            (std::vector<std::string>{"journey-css", "journey-less",
                                      "journey-js"}));

  // localhost on the same port is another origin: the journey's control.
  EXPECT_FALSE(ProfileFor("http://localhost:8808/page"));
  EXPECT_TRUE(AssetIdsFor("http://localhost:8808/page").empty());
}

TEST_F(DeveloperProfilePrefSeedTest,
       CompiledOutputWithoutVersionDropsTheWholeProfile) {
  // The seed that failed every installed DEV check: LESS output stored with
  // version 0 is malformed, so not even the plain CSS or headers apply.
  Seed(0);
  EXPECT_FALSE(ProfileFor("http://127.0.0.1:8808/page"));
  EXPECT_TRUE(AssetIdsFor("http://127.0.0.1:8808/page").empty());
}

TEST_F(DeveloperProfilePrefSeedTest,
       OtherCompilerVersionKeepsTheRestOfTheProfile) {
  // After a compiler update (or a sync from another device) the stored
  // output is stale. It must stay inert without disabling the other rules.
  Seed(static_cast<int>(kDeveloperStyleCompilerVersion) + 1);

  const std::optional<DeveloperProfile> profile =
      ProfileFor("http://127.0.0.1:8808/page");
  ASSERT_TRUE(profile);
  EXPECT_TRUE(profile->header_rules_enabled);
  EXPECT_TRUE(profile->cache_disabled);
  const std::vector<std::string> ids =
      AssetIdsFor("http://127.0.0.1:8808/page");
  EXPECT_EQ(ids, (std::vector<std::string>{"journey-css", "journey-less",
                                           "journey-js"}));
}

}  // namespace
}  // namespace ahoi
