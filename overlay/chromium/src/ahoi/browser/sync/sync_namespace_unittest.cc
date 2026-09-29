// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_namespace.h"

#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

constexpr char kWorkspace[] = "0f8b2c4e-6a1d-4e3b-9c5f-7d2a1b3c4e5f";

SyncNamespaceIdentifiers MainIdentifiers() {
  return {.zone_name = kMainSyncZoneName,
          .subscription_identifier = "AhoiBrowserSyncSubscription",
          .keychain_account = "payload-key"};
}

SyncNamespace Separated() {
  return *SyncNamespace::ForSeparatedWorkspace(
      base::Uuid::ParseLowercase(kWorkspace));
}

TEST(SyncNamespaceTest, MainNamespaceKeepsBundleValuesByteForByte) {
  const auto main = ResolveSyncNamespace(SyncNamespace::Main(),
                                         MainIdentifiers());
  ASSERT_TRUE(main);
  EXPECT_EQ(*main, MainIdentifiers());
  EXPECT_EQ(main->zone_name, "AhoiBrowserSyncV3");

  // Even an unusual bundle value is passed through unchanged.
  const SyncNamespaceIdentifiers odd{.zone_name = "AhoiSyncAcceptance-x",
                                     .subscription_identifier = "",
                                     .keychain_account = ""};
  EXPECT_EQ(*ResolveSyncNamespace(SyncNamespace::Main(), odd), odd);
  EXPECT_TRUE(SyncNamespace::Main().is_main());
}

TEST(SyncNamespaceTest, SeparatedWorkspaceGetsItsOwnZoneAndKey) {
  const auto ws = ResolveSyncNamespace(Separated(), MainIdentifiers());
  ASSERT_TRUE(ws);
  EXPECT_EQ(ws->zone_name, std::string("AhoiBrowserSyncV3-ws-") + kWorkspace);
  EXPECT_EQ(ws->subscription_identifier,
            std::string("AhoiBrowserSyncSubscription-ws-") + kWorkspace);
  EXPECT_EQ(ws->keychain_account, std::string("payload-key.ws-") + kWorkspace);
  EXPECT_TRUE(IsValidCloudKitZoneName(ws->zone_name));
}

TEST(SyncNamespaceTest, SeparatedNamespaceNeverResolvesToTheMainZone) {
  const SyncNamespaceIdentifiers bases[] = {
      MainIdentifiers(),
      {.zone_name = kMainSyncZoneName,
       .subscription_identifier = "",
       .keychain_account = "payload-key"},
      {.zone_name = "AhoiSyncAcceptance-scope",
       .subscription_identifier = "AhoiSyncAcceptanceSubscription-scope",
       .keychain_account = "payload-key.acceptance-scope"},
  };
  for (const auto& base : bases) {
    const auto ws = ResolveSyncNamespace(Separated(), base);
    ASSERT_TRUE(ws);
    EXPECT_NE(ws->zone_name, base.zone_name);
    EXPECT_NE(ws->zone_name, kMainSyncZoneName);
    EXPECT_NE(ws->keychain_account, base.keychain_account);
    // Distinct from CKSyncEngine's default id too (empty in the bundle).
    EXPECT_FALSE(ws->subscription_identifier.empty());
    EXPECT_NE(ws->subscription_identifier, base.subscription_identifier);
    // The key bootstrap's auxiliary items `<account>.bootstrap-*` of the main
    // key never collide with a Workspace key account.
    EXPECT_NE(ws->keychain_account.find(".ws-"), std::string::npos);
  }

  // Two Workspaces never share a zone or key.
  const auto other = ResolveSyncNamespace(
      *SyncNamespace::ForSeparatedWorkspace(base::Uuid::GenerateRandomV4()),
      MainIdentifiers());
  const auto ws = ResolveSyncNamespace(Separated(), MainIdentifiers());
  ASSERT_TRUE(other && ws);
  EXPECT_NE(other->zone_name, ws->zone_name);
  EXPECT_NE(other->keychain_account, ws->keychain_account);
  EXPECT_NE(other->subscription_identifier, ws->subscription_identifier);
}

TEST(SyncNamespaceTest, SeparatedNamespaceFailsClosed) {
  EXPECT_FALSE(SyncNamespace::ForSeparatedWorkspace(base::Uuid()));
  EXPECT_FALSE(SyncNamespace::ForSeparatedWorkspace(
      base::Uuid::ParseCaseInsensitive("not-a-uuid")));

  SyncNamespaceIdentifiers no_zone = MainIdentifiers();
  no_zone.zone_name.clear();
  EXPECT_FALSE(ResolveSyncNamespace(Separated(), no_zone));

  SyncNamespaceIdentifiers no_key = MainIdentifiers();
  no_key.keychain_account.clear();
  EXPECT_FALSE(ResolveSyncNamespace(Separated(), no_key));

  SyncNamespaceIdentifiers nested = MainIdentifiers();
  nested.zone_name = std::string("AhoiBrowserSyncV3-ws-") + kWorkspace;
  EXPECT_FALSE(ResolveSyncNamespace(Separated(), nested));

  SyncNamespaceIdentifiers bad_chars = MainIdentifiers();
  bad_chars.zone_name = "Ahoi Browser";
  EXPECT_FALSE(ResolveSyncNamespace(Separated(), bad_chars));

  SyncNamespaceIdentifiers too_long = MainIdentifiers();
  too_long.zone_name = std::string(kMaxCloudKitZoneNameLength - 39, 'a');
  EXPECT_FALSE(ResolveSyncNamespace(Separated(), too_long));
}

TEST(SyncNamespaceTest, ZoneNameValidation) {
  EXPECT_TRUE(IsValidCloudKitZoneName("AhoiBrowserSyncV3"));
  EXPECT_TRUE(IsValidCloudKitZoneName("a-b_c9"));
  EXPECT_TRUE(IsValidCloudKitZoneName(std::string(255, 'a')));
  EXPECT_FALSE(IsValidCloudKitZoneName(""));
  EXPECT_FALSE(IsValidCloudKitZoneName(std::string(256, 'a')));
  EXPECT_FALSE(IsValidCloudKitZoneName("_defaultZone"));
  EXPECT_FALSE(IsValidCloudKitZoneName("a.b"));
  EXPECT_FALSE(IsValidCloudKitZoneName("a/b"));
  EXPECT_FALSE(IsValidCloudKitZoneName("\xc3\xa4"));
}

class SyncNamespaceForProfileTest : public testing::Test {
 protected:
  SyncNamespaceForProfileTest() {
    session::RegisterIsolatedProfileLocalState(local_state_.registry());
  }

  std::optional<SyncNamespace> For(const char* dir) {
    return ProfileSyncServiceFactory::SyncNamespaceForProfileDir(&local_state_,
                                                                 dir);
  }

  TestingPrefServiceSimple local_state_;
};

TEST_F(SyncNamespaceForProfileTest, UnlistedProfileUsesMainNamespace) {
  EXPECT_EQ(For("Default"), SyncNamespace::Main());
  EXPECT_EQ(ProfileSyncServiceFactory::SyncNamespaceForProfileDir(nullptr,
                                                                  "Default"),
            SyncNamespace::Main());
}

TEST_F(SyncNamespaceForProfileTest, SeparatedProfileNeverResolvesToMainZone) {
  ASSERT_TRUE(session::AddIsolatedProfile(
      &local_state_, {.profile_dir = "Profile 3",
                      .workspace_id = base::Uuid::ParseLowercase(kWorkspace),
                      .name = u"Work",
                      .icon = u"briefcase"}));
  for (const auto state : {session::IsolatedProfileState::kCreating,
                           session::IsolatedProfileState::kActive}) {
    ASSERT_TRUE(
        session::SetIsolatedProfileState(&local_state_, "Profile 3", state));
    const auto ns = For("Profile 3");
    ASSERT_TRUE(ns);
    EXPECT_FALSE(ns->is_main());
    EXPECT_EQ(ns->workspace_id().AsLowercaseString(), kWorkspace);
    const auto ids = ResolveSyncNamespace(*ns, MainIdentifiers());
    ASSERT_TRUE(ids);
    EXPECT_NE(ids->zone_name, kMainSyncZoneName);
    EXPECT_NE(ids->keychain_account, MainIdentifiers().keychain_account);
  }
  EXPECT_EQ(For("Default"), SyncNamespace::Main());

  // A Profile being deleted gets no sync service at all.
  ASSERT_TRUE(session::SetIsolatedProfileState(
      &local_state_, "Profile 3", session::IsolatedProfileState::kDeleting));
  EXPECT_FALSE(For("Profile 3"));
}

TEST_F(SyncNamespaceForProfileTest, MalformedEntryFailsClosed) {
  base::ListValue list;
  list.Append(base::DictValue()
                  .Set("profile_dir", "Profile 4")
                  .Set("workspace_id", "not-a-uuid")
                  .Set("name", "Broken")
                  .Set("icon", "x")
                  .Set("state", 1));
  local_state_.SetList(session::kIsolatedProfilesPref, std::move(list));
  EXPECT_FALSE(For("Profile 4"));
  EXPECT_EQ(For("Default"), SyncNamespace::Main());
}

}  // namespace
}  // namespace ahoi::sync
