// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_device_tab_commands.h"

#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/sync/sync_policy.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sidebar {
namespace {

constexpr char kDeviceId[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSessionId[] = "22222222-2222-4222-8222-222222222222";
constexpr char kTabId[] = "33333333-3333-4333-8333-333333333333";
constexpr char kWorkspaceId[] = "44444444-4444-4444-8444-444444444444";

base::Uuid Uuid(std::string_view value) {
  return base::Uuid::ParseLowercase(value);
}

base::Time Now() {
  return base::Time::UnixEpoch() + base::Days(100);
}

sync::DeviceTabsSnapshot ValidSnapshot() {
  sync::DeviceTabsSnapshot snapshot;
  snapshot.devices.push_back({
      .id = Uuid(kDeviceId),
      .type = sync::DeviceType::kIPhone,
      .display_name = "Ada's iPhone",
      .last_seen = Now() - base::Minutes(1),
  });
  snapshot.sessions.push_back({
      .id = Uuid(kSessionId),
      .device_id = Uuid(kDeviceId),
      .started_at = Now() - base::Hours(1),
      .last_seen = Now() - base::Minutes(1),
      .active = true,
  });
  snapshot.workspaces.push_back({
      .id = Uuid(kWorkspaceId),
      .name = "Research",
  });
  snapshot.remote_tabs.push_back({
      .id = Uuid(kTabId),
      .device_id = Uuid(kDeviceId),
      .session_id = Uuid(kSessionId),
      .workspace_id = Uuid(kWorkspaceId),
      .url = "https://example.test/private",
      .title = "Remote document",
      .opened_at = Now() - base::Minutes(5),
      .last_active = Now() - base::Minutes(1),
  });
  return snapshot;
}

TEST(SidebarDeviceTabCommandsTest, BuildsOpaqueSearchableIdentity) {
  const sync::DeviceTabsSnapshot snapshot = ValidSnapshot();

  const std::vector<CommandItem> items =
      BuildDeviceTabCommandItems(snapshot, Now());

  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items.front().type, CommandItemType::kDeviceTab);
  EXPECT_EQ(items.front().stable_id, std::string(kDeviceId) + ":" + kTabId);
  EXPECT_EQ(items.front().title, u"Remote document");
  EXPECT_EQ(items.front().url, GURL("https://example.test/private"));
  EXPECT_NE(items.front().secondary_text.find(u"Ada's iPhone"),
            std::u16string::npos);
  EXPECT_NE(items.front().secondary_text.find(u"Research"),
            std::u16string::npos);
  EXPECT_EQ(items.front().stable_id.find("example.test"), std::string::npos);
  EXPECT_EQ(items.front().stable_id.find("Remote document"), std::string::npos);

  const sync::RemoteTabRecord* resolved =
      ResolveDeviceTabCommand(snapshot, items.front().stable_id, Now());
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->id, Uuid(kTabId));
}

TEST(SidebarDeviceTabCommandsTest, StaleActivationFailsClosed) {
  sync::DeviceTabsSnapshot snapshot = ValidSnapshot();
  const std::vector<CommandItem> items =
      BuildDeviceTabCommandItems(snapshot, Now());
  ASSERT_EQ(items.size(), 1u);

  snapshot.remote_tabs.front().tombstone = true;
  EXPECT_EQ(ResolveDeviceTabCommand(snapshot, items.front().stable_id, Now()),
            nullptr);

  snapshot = ValidSnapshot();
  snapshot.sessions.front().last_seen =
      Now() - sync::kRemoteSessionVisibleAge - base::Seconds(1);
  EXPECT_EQ(ResolveDeviceTabCommand(snapshot, items.front().stable_id, Now()),
            nullptr);

  snapshot = ValidSnapshot();
  snapshot.devices.front().retired = true;
  EXPECT_EQ(ResolveDeviceTabCommand(snapshot, items.front().stable_id, Now()),
            nullptr);
}

TEST(SidebarDeviceTabCommandsTest, ExcludesPrivateOrCredentialBearingTabs) {
  sync::DeviceTabsSnapshot snapshot = ValidSnapshot();
  snapshot.remote_tabs.front().is_incognito = true;
  EXPECT_TRUE(BuildDeviceTabCommandItems(snapshot, Now()).empty());

  snapshot = ValidSnapshot();
  snapshot.remote_tabs.front().url =
      "https://username:password@example.test/private";
  EXPECT_TRUE(BuildDeviceTabCommandItems(snapshot, Now()).empty());

  snapshot = ValidSnapshot();
  snapshot.sessions.front().device_id =
      Uuid("55555555-5555-4555-8555-555555555555");
  EXPECT_TRUE(BuildDeviceTabCommandItems(snapshot, Now()).empty());
}

TEST(SidebarDeviceTabCommandsTest, BlankProviderTitleFallsBackToSafeUrl) {
  sync::DeviceTabsSnapshot snapshot = ValidSnapshot();
  snapshot.remote_tabs.front().title = "  \t ";

  const std::vector<CommandItem> items =
      BuildDeviceTabCommandItems(snapshot, Now());

  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items.front().title, u"https://example.test/private");
}

TEST(SidebarDeviceTabCommandsTest, DuplicateProviderIdentityFailsAtomically) {
  sync::DeviceTabsSnapshot snapshot = ValidSnapshot();
  snapshot.remote_tabs.push_back(snapshot.remote_tabs.front());

  EXPECT_TRUE(BuildDeviceTabCommandItems(snapshot, Now()).empty());
  EXPECT_EQ(ResolveDeviceTabCommand(
                snapshot, std::string(kDeviceId) + ":" + kTabId, Now()),
            nullptr);
}

}  // namespace
}  // namespace ahoi::sidebar
