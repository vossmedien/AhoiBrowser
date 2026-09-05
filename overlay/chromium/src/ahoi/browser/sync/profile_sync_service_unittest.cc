// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/test/base/testing_profile.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

struct StoreCounts {
  int records = 0;
  int outbox = 0;
  int history = 0;
  int tabs = 0;
  int active_tabs = 0;

  friend bool operator==(const StoreCounts&, const StoreCounts&) = default;
};

class FakeProfileSyncUiBridge final : public ProfileSyncUiBridge {
 public:
  base::WeakPtr<ProfileSyncUiBridge> GetWeakPtrForSync() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  base::CallbackListSubscription AddTabTreeSnapshotChangedCallback(
      base::RepeatingCallback<void(const tab_tree::TabTreeSnapshot&)>)
      override {
    return {};
  }

  base::CallbackListSubscription AddRuntimeTabHost(
      base::RepeatingClosure callback) {
    return runtime_tab_hosts_.Add(std::move(callback));
  }

  void RequestLocalTabCapture() override {
    ++capture_request_count_;
    runtime_tab_hosts_.Notify();
  }

  bool ExportTabTreeSnapshot(tab_tree::TabTreeSnapshot*) override {
    return false;
  }

  tab_tree::TabTreeStore::Result ApplySyncedTabTreeSnapshot(
      tab_tree::TabTreeSnapshot) override {
    return tab_tree::TabTreeStore::Result::kOk;
  }

  bool OpenNormalTabFromRemoteCommand(const GURL&,
                                      std::optional<base::Uuid>) override {
    return false;
  }

  bool FocusNormalTabFromRemoteCommand(std::string_view) override {
    return false;
  }

  bool CloseNormalTabFromRemoteCommand(std::string_view) override {
    return false;
  }

  int capture_request_count() const { return capture_request_count_; }

 private:
  int capture_request_count_ = 0;
  base::RepeatingClosureList runtime_tab_hosts_;
  base::WeakPtrFactory<FakeProfileSyncUiBridge> weak_ptr_factory_{this};
};

std::optional<int> QueryCount(sql::Database* database,
                              const std::string& query) {
  sql::Statement statement(database->GetUniqueStatement(query));
  if (!statement.Step()) {
    return std::nullopt;
  }
  return statement.ColumnInt(0);
}

std::optional<StoreCounts> ReadStoreCounts(const base::FilePath& path) {
  sql::Database database(sql::test::kTestTag);
  if (!database.Open(path)) {
    return std::nullopt;
  }
  const std::optional<int> records =
      QueryCount(&database, "SELECT COUNT(*) FROM sync_records");
  const std::optional<int> outbox =
      QueryCount(&database, "SELECT COUNT(*) FROM sync_outbox");
  const std::optional<int> history = QueryCount(
      &database,
      "SELECT COUNT(*) FROM sync_records WHERE entity_type=" +
          std::to_string(static_cast<int>(EntityType::kHistoryEntry)));
  const std::optional<int> tabs = QueryCount(
      &database, "SELECT COUNT(*) FROM sync_records WHERE entity_type=" +
                     std::to_string(static_cast<int>(EntityType::kRemoteTab)));
  const std::optional<int> active_tabs = QueryCount(
      &database, "SELECT COUNT(*) FROM sync_records WHERE entity_type=" +
                     std::to_string(static_cast<int>(EntityType::kRemoteTab)) +
                     " AND tombstone=0");
  if (!records || !outbox || !history || !tabs || !active_tabs) {
    return std::nullopt;
  }
  return StoreCounts{.records = *records,
                     .outbox = *outbox,
                     .history = *history,
                     .tabs = *tabs,
                     .active_tabs = *active_tabs};
}

std::optional<int> ReadActiveRecordPayloadCount(
    const base::FilePath& path,
    EntityType entity_type,
    const std::string& payload_fragment) {
  sql::Database database(sql::test::kTestTag);
  if (!database.Open(path)) {
    return std::nullopt;
  }
  sql::Statement statement(database.GetUniqueStatement(
      "SELECT COUNT(*) FROM sync_records WHERE entity_type=? "
      "AND tombstone=0 AND payload LIKE ?"));
  statement.BindInt(0, static_cast<int>(entity_type));
  statement.BindString(1, "%" + payload_fragment + "%");
  if (!statement.Step()) {
    return std::nullopt;
  }
  return statement.ColumnInt(0);
}

}  // namespace

class ProfileSyncServiceTest : public testing::Test {
 protected:
  std::unique_ptr<TestingProfile> CreateProfile() {
    return TestingProfile::Builder().Build();
  }

  base::FilePath DatabasePath(const TestingProfile& profile) const {
    return profile.GetPath()
        .AppendASCII("Ahoi Sync")
        .AppendASCII("sync.sqlite");
  }

  bool BackendIsNull(const ProfileSyncService& service) const {
    return service.backend_.is_null();
  }

  RemoteCommandPolicy CurrentRemoteCommandPolicy(
      const ProfileSyncService& service) const {
    return service.CurrentRemoteCommandPolicy();
  }

  void DrainBackend(ProfileSyncService* service) {
    for (int attempt = 0; attempt < 4; ++attempt) {
      if (!service->backend_.is_null()) {
        service->backend_.FlushPostedTasksForTesting();
      }
      task_environment_.RunUntilIdle();
    }
  }

  void DrainBackendRunner(ProfileSyncService* service) {
    base::RunLoop run_loop;
    service->backend_task_runner_->PostTaskAndReply(
        FROM_HERE, base::DoNothing(), run_loop.QuitClosure());
    run_loop.Run();
    task_environment_.RunUntilIdle();
  }

  void PublishTabsNow(ProfileSyncService* service,
                      std::string stable_key,
                      std::string url) {
    service->PublishWindowTabs("window-1",
                               {{.stable_key = std::move(stable_key),
                                 .url = std::move(url),
                                 .title = "Local tab",
                                 .active = true}});
    service->publish_timer_.Stop();
    service->PublishCombinedLocalTabs();
  }

  void RecordHistoryVisit(ProfileSyncService* service,
                          const GURL& url,
                          int64_t visit_id) {
    history::URLRow row(url);
    row.set_title(u"Local visit");
    row.set_hidden(false);
    history::VisitRow visit;
    visit.visit_id = visit_id;
    visit.visit_time = base::Time::Now();
    visit.transition = ui::PAGE_TRANSITION_LINK;
    visit.source = history::SOURCE_BROWSED;
    service->OnURLVisited(service->history_service_,
                          history::VisitedURLInfo(row, visit));
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(ProfileSyncServiceTest,
       DisabledProfileDoesNotCreateOrCollectIntoLocalStore) {
  std::unique_ptr<TestingProfile> profile = CreateProfile();
  ASSERT_FALSE(profile->GetPrefs()->GetBoolean(kSyncEnabledPref));
  const base::FilePath database_path = DatabasePath(*profile);

  ProfileSyncService service(profile.get());
  EXPECT_TRUE(BackendIsNull(service));
  PublishTabsNow(&service, "disabled-tab", "https://disabled.example/");
  RecordHistoryVisit(&service, GURL("https://disabled.example/history"), 1);
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(profile->GetPrefs()->GetString(kDeviceIdPref).empty());
  EXPECT_FALSE(base::PathExists(database_path));
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest,
       EnableAndReenableRequestCurrentRuntimeTabsWithoutLaterUiMutation) {
  std::unique_ptr<TestingProfile> profile = CreateProfile();
  ProfileSyncService service(profile.get());
  FakeProfileSyncUiBridge bridge;
  auto publish_window = [](ProfileSyncService* service, std::string window_key,
                           const std::string* stable_key,
                           const std::string* url) {
    service->PublishWindowTabs(std::move(window_key),
                               {{.stable_key = *stable_key,
                                 .url = *url,
                                 .title = "Current runtime tab",
                                 .active = true}});
  };
  std::string first_stable_key = "runtime-tab-1";
  std::string first_url = "https://runtime-one.example/";
  std::string second_stable_key = "runtime-tab-2";
  std::string second_url = "https://runtime-two.example/";
  base::CallbackListSubscription first_host =
      bridge.AddRuntimeTabHost(base::BindRepeating(
          publish_window, base::Unretained(&service), "window-1",
          base::Unretained(&first_stable_key), base::Unretained(&first_url)));
  base::CallbackListSubscription second_host =
      bridge.AddRuntimeTabHost(base::BindRepeating(
          publish_window, base::Unretained(&service), "window-2",
          base::Unretained(&second_stable_key), base::Unretained(&second_url)));
  (void)first_host;
  service.AttachUiBridge(&bridge);

  service.SetSyncEnabled(true);
  EXPECT_EQ(1, bridge.capture_request_count());
  task_environment_.FastForwardBy(base::Milliseconds(100));
  DrainBackend(&service);

  const std::optional<StoreCounts> counts =
      ReadStoreCounts(DatabasePath(*profile));
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(2, counts->tabs);
  EXPECT_EQ(2, counts->active_tabs);
  EXPECT_GT(counts->outbox, 0);

  service.SetSyncEnabled(false);
  DrainBackendRunner(&service);
  first_stable_key = "runtime-tab-after-reenable";
  first_url = "https://runtime-after-reenable.example/";
  second_host = {};

  service.SetSyncEnabled(true);
  EXPECT_EQ(2, bridge.capture_request_count());
  task_environment_.FastForwardBy(base::Milliseconds(100));
  DrainBackend(&service);

  const std::optional<StoreCounts> reenabled_counts =
      ReadStoreCounts(DatabasePath(*profile));
  ASSERT_TRUE(reenabled_counts.has_value());
  EXPECT_GT(reenabled_counts->tabs, counts->tabs);
  EXPECT_EQ(1, reenabled_counts->active_tabs);
  EXPECT_EQ(1, ReadActiveRecordPayloadCount(DatabasePath(*profile),
                                            EntityType::kRemoteTab,
                                            "runtime-after-reenable.example"));

  service.DetachUiBridge(&bridge);
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest,
       EnabledWithoutCloudKitCapturesLocallyAndDisableFreezesStore) {
  std::unique_ptr<TestingProfile> profile = CreateProfile();
  profile->GetPrefs()->SetBoolean(kSyncEnabledPref, true);
  const base::FilePath database_path = DatabasePath(*profile);

  ProfileSyncService service(profile.get());
  DrainBackend(&service);
  ASSERT_TRUE(service.initialized());
  EXPECT_TRUE(service.transport_status().enabled);
  EXPECT_FALSE(service.transport_status().provider_available);

  const base::Uuid approved_device = base::Uuid::GenerateRandomV4();
  const base::Uuid unapproved_device = base::Uuid::GenerateRandomV4();
  const std::string public_key_base64 =
      base::Base64Encode(std::string(32, 'k'));
  base::DictValue approved_keys;
  approved_keys.Set(approved_device.AsLowercaseString(), public_key_base64);
  profile->GetPrefs()->SetDict(kApprovedRemoteCommandKeysPref,
                               std::move(approved_keys));

  service.SetRemoteControlEnabled(true);
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(kRemoteControlEnabledPref));
  profile->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, true);
  EXPECT_FALSE(CurrentRemoteCommandPolicy(service).enabled);
  profile->GetPrefs()->SetBoolean(kRemoteControlEnabledPref, false);
  EXPECT_FALSE(
      service.ApproveRemoteControlDevice(unapproved_device, public_key_base64));
  service.RevokeRemoteControlDevice(approved_device);
  const base::DictValue& keys_after_blocked_mutations =
      profile->GetPrefs()->GetDict(kApprovedRemoteCommandKeysPref);
  EXPECT_FALSE(keys_after_blocked_mutations.contains(
      unapproved_device.AsLowercaseString()));
  EXPECT_EQ(public_key_base64, *keys_after_blocked_mutations.FindString(
                                   approved_device.AsLowercaseString()));

  PublishTabsNow(&service, "enabled-tab", "https://enabled.example/");
  RecordHistoryVisit(&service, GURL("https://enabled.example/history"), 2);
  DrainBackend(&service);

  const std::optional<StoreCounts> enabled_counts =
      ReadStoreCounts(database_path);
  ASSERT_TRUE(enabled_counts.has_value());
  EXPECT_GT(enabled_counts->records, 0);
  EXPECT_GT(enabled_counts->outbox, 0);
  EXPECT_EQ(1, enabled_counts->history);
  EXPECT_EQ(1, enabled_counts->tabs);
  EXPECT_EQ(1, enabled_counts->active_tabs);

  // A mutation accepted before the opt-out is ordered ahead of suspension on
  // the backend sequence. It may complete; the durable state after suspension
  // is the cutoff that disabled capture must preserve.
  RecordHistoryVisit(&service, GURL("https://in-flight.example/history"), 3);
  service.SetSyncEnabled(false);
  EXPECT_FALSE(service.sync_enabled());
  EXPECT_TRUE(BackendIsNull(service));
  DrainBackendRunner(&service);
  const std::optional<StoreCounts> cutoff_counts =
      ReadStoreCounts(database_path);
  ASSERT_TRUE(cutoff_counts.has_value());
  EXPECT_GT(cutoff_counts->records, enabled_counts->records);
  EXPECT_GT(cutoff_counts->outbox, enabled_counts->outbox);
  EXPECT_EQ(enabled_counts->history + 1, cutoff_counts->history);
  EXPECT_EQ(enabled_counts->tabs, cutoff_counts->tabs);

  PublishTabsNow(&service, "post-disable-tab", "https://post-disable.example/");
  RecordHistoryVisit(&service, GURL("https://post-disable.example/history"), 4);
  DrainBackendRunner(&service);
  EXPECT_EQ(cutoff_counts, ReadStoreCounts(database_path));

  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest,
       EnableSeedsPermittedSettingSelectedWhileSyncWasDisabled) {
  std::unique_ptr<TestingProfile> profile = CreateProfile();
  const base::FilePath database_path = DatabasePath(*profile);
  profile->GetPrefs()->SetBoolean(appearance::kSidebarPageTintEnabledPref,
                                  true);

  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(
      appearance::kSidebarPageTintEnabledPref, true));
  EXPECT_FALSE(base::PathExists(database_path));

  service.SetSyncEnabled(true);
  DrainBackend(&service);

  EXPECT_EQ(1, ReadActiveRecordPayloadCount(
                   database_path, EntityType::kPermittedSetting,
                   appearance::kSidebarPageTintEnabledPref));
  EXPECT_EQ(1, ReadActiveRecordPayloadCount(database_path,
                                            EntityType::kPermittedSetting,
                                            "\"value_json\":\"true\""));

  service.Shutdown();
}

}  // namespace ahoi::sync
