// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_discovery_model.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/test/task_environment.h"
#include "base/uuid.h"
#include "components/sessions/core/mock_tab_restore_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/tab_restore_types.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {
namespace {

constexpr char kPageId[] = "11111111-1111-4111-8111-111111111111";
constexpr char kWorkspaceId[] = "22222222-2222-4222-8222-222222222222";

base::Uuid Uuid(std::string_view value) {
  return base::Uuid::ParseLowercase(value);
}

CommandItem MakeCommand(CommandItemType type,
                        std::string stable_id,
                        std::u16string title,
                        GURL url = GURL("https://example.test/document")) {
  CommandItem item{
      .type = type,
      .stable_id = std::move(stable_id),
      .title = std::move(title),
  };
  if (type == CommandItemType::kOpenTab ||
      type == CommandItemType::kSavedPage ||
      type == CommandItemType::kDeviceTab) {
    item.url = std::move(url);
  }
  return item;
}

std::unique_ptr<sessions::tab_restore::Tab> MakeClosedTab() {
  auto tab = std::make_unique<sessions::tab_restore::Tab>();
  sessions::SerializedNavigationEntry navigation;
  navigation.set_index(0);
  navigation.set_virtual_url(GURL("https://example.test/closed"));
  navigation.set_title(u"Closed document");
  tab->navigations.push_back(std::move(navigation));
  tab->current_navigation_index = 0;
  return tab;
}

void SetAhoiMetadata(sessions::tab_restore::Tab* tab,
                     std::optional<base::Uuid> tree_node_id) {
  ASSERT_NE(tab, nullptr);
  const std::optional<std::string> encoded = session::EncodeTabSessionMetadata({
      .workspace_id = Uuid(kWorkspaceId),
      .tree_node_id = std::move(tree_node_id),
  });
  ASSERT_TRUE(encoded.has_value());
  tab->extra_data[session::kTabSessionMetadataExtraDataKey] = *encoded;
}

class CountingDiscoveryObserver final : public SidebarDiscoveryModelObserver {
 public:
  void OnSidebarDiscoveryModelChanged() override { ++change_count; }

  int change_count = 0;
};

TEST(SidebarDiscoveryModelTest, DeduplicatesDurableEntityAcrossSources) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  SidebarDiscoveryModel model(&service, nullptr);
  CommandItem sleeping =
      MakeCommand(CommandItemType::kOpenTab, kPageId, u"Project document");
  sleeping.sleeping = true;
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kOpenTab, {sleeping}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kSavedPage,
                           {MakeCommand(CommandItemType::kSavedPage, kPageId,
                                        u"Project document")}));

  const std::vector<SidebarDiscoveryItem> results =
      model.Search(u"project", 10u);

  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front().stable_id, std::string("page:") + kPageId);
  EXPECT_EQ(results.front().kind, SidebarDiscoveryItemKind::kSleepingTab);
  ASSERT_TRUE(results.front().command.has_value());
  EXPECT_EQ(results.front().command->type, CommandItemType::kOpenTab);
}

TEST(SidebarDiscoveryModelTest, PreservesDistinctTemporaryTabsWithSameUrl) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  SidebarDiscoveryModel model(&service, nullptr);
  const GURL shared_url("https://example.test/shared");
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kOpenTab,
                           {MakeCommand(CommandItemType::kOpenTab, "runtime:1",
                                        u"Shared first", shared_url),
                            MakeCommand(CommandItemType::kOpenTab, "runtime:2",
                                        u"Shared second", shared_url)}));

  const std::vector<SidebarDiscoveryItem> results =
      model.Search(u"shared", 10u);

  ASSERT_EQ(results.size(), 2u);
  EXPECT_NE(results[0].stable_id, results[1].stable_id);
  EXPECT_EQ(results[0].url, shared_url);
  EXPECT_EQ(results[1].url, shared_url);
}

TEST(SidebarDiscoveryModelTest, IncludesExplicitDeviceTabProjection) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  SidebarDiscoveryModel model(&service, nullptr);
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kDeviceTab,
                           {MakeCommand(CommandItemType::kDeviceTab,
                                        "device:tab", u"Remote research")}));

  const std::vector<SidebarDiscoveryItem> results =
      model.Search(u"remote", 10u);

  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front().kind, SidebarDiscoveryItemKind::kDeviceTab);
  EXPECT_EQ(results.front().stable_id, "device-tab:device:tab");
}

TEST(SidebarDiscoveryModelTest, CoalescesSequentialSourceRefreshes) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  SidebarDiscoveryModel model(&service, nullptr);
  CountingDiscoveryObserver observer;
  model.AddObserver(&observer);

  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kOpenTab,
      {MakeCommand(CommandItemType::kOpenTab, "runtime:1", u"Open")}));
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kSavedPage,
      {MakeCommand(CommandItemType::kSavedPage, kPageId, u"Saved")}));
  EXPECT_EQ(observer.change_count, 0);

  task_environment.RunUntilIdle();
  EXPECT_EQ(observer.change_count, 1);
  model.RemoveObserver(&observer);
}

TEST(SidebarDiscoveryModelTest, FiltersSavedPagesFromRecentlyClosedEntries) {
  std::unique_ptr<sessions::tab_restore::Tab> tab = MakeClosedTab();
  EXPECT_TRUE(internal::IsEligibleRecentlyClosedEntry(*tab));

  SetAhoiMetadata(tab.get(), std::nullopt);
  EXPECT_TRUE(internal::IsEligibleRecentlyClosedEntry(*tab));

  SetAhoiMetadata(tab.get(), Uuid(kPageId));
  EXPECT_FALSE(internal::IsEligibleRecentlyClosedEntry(*tab));

  tab->extra_data[session::kTabSessionMetadataExtraDataKey] = "malformed";
  EXPECT_FALSE(internal::IsEligibleRecentlyClosedEntry(*tab));

  sessions::tab_restore::Window window;
  window.tabs.push_back(MakeClosedTab());
  std::unique_ptr<sessions::tab_restore::Tab> saved = MakeClosedTab();
  SetAhoiMetadata(saved.get(), Uuid(kPageId));
  window.tabs.push_back(std::move(saved));
  EXPECT_FALSE(internal::IsEligibleRecentlyClosedEntry(window));
}

TEST(SidebarDiscoveryModelTest, RevalidatesRestoreAgainstCurrentEntry) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  testing::NiceMock<MockTabRestoreService> restore_service;
  sessions::TabRestoreService::Entries entries;
  entries.push_back(MakeClosedTab());
  const SessionID entry_id = entries.front()->id;
  ON_CALL(restore_service, entries())
      .WillByDefault(testing::ReturnRef(entries));
  SidebarDiscoveryModel model(&service, &restore_service);

  ASSERT_EQ(model.RecentlyClosed(10u).size(), 1u);
  SetAhoiMetadata(
      static_cast<sessions::tab_restore::Tab*>(entries.front().get()),
      Uuid(kPageId));
  EXPECT_CALL(restore_service,
              RestoreEntryById(testing::_, testing::_, testing::_))
      .Times(0);
  EXPECT_FALSE(model.RestoreRecentlyClosed(entry_id, nullptr));
}

TEST(SidebarDiscoveryModelTest, DropsDestroyedRestoreServiceWithoutReentry) {
  base::test::TaskEnvironment task_environment;
  CommandService service;
  testing::NiceMock<MockTabRestoreService> restore_service;
  sessions::TabRestoreService::Entries entries;
  ON_CALL(restore_service, entries())
      .WillByDefault(testing::ReturnRef(entries));
  EXPECT_CALL(restore_service, RemoveObserver(testing::_)).Times(0);

  {
    SidebarDiscoveryModel model(&service, &restore_service);
    model.TabRestoreServiceDestroyed(&restore_service);
    EXPECT_TRUE(model.RecentlyClosed(10u).empty());
    task_environment.RunUntilIdle();
  }
}

}  // namespace
}  // namespace ahoi::sidebar
