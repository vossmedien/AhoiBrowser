// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/sync/browser_settings_sync_types.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
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
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_change_registrar.h"
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
  if (!statement.is_valid() || !statement.Step()) {
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
  if (!statement.is_valid()) {
    return std::nullopt;
  }
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
  PermittedSettingRecord RemoteSetting(std::string id, std::string value) {
    return {.id = BrowserSettingRecordId(id),
            .setting_id = std::move(id),
            .value_json = std::move(value),
            .version = {.stamp = {
                .physical_time_us = 1000000,
                .logical = 1,
                .device_tiebreak = "11111111-1111-4111-8111-111111111111"}}};
  }

  void ApplySettingProjection(ProfileSyncService* service,
                              std::vector<PermittedSettingRecord> records) {
    BrowserSettingsProjection projection;
    projection.authorization = base::BindRepeating([] { return true; });
    projection.initial_fetch_complete = false;
    for (const auto& record : records) {
      projection.record_authorizations.emplace(record.id,
                                               projection.authorization);
    }
    projection.records = std::move(records);
    service->OnBrowserSettingsRead(service->browser_settings_generation_,
                                    std::move(projection));
  }

  std::optional<PermittedSettingRecord> PendingSetting(
      const TestingProfile& profile, std::string_view id) {
    const auto* payload = profile.GetPrefs()->GetDict(kBrowserSettingIntentsPref)
                              .FindString(id);
    SyncRecord record;
    if (!payload || !DeserializeRecord(EntityType::kPermittedSetting, *payload,
                                        &record)) {
      return std::nullopt;
    }
    return std::get<PermittedSettingRecord>(std::move(record));
  }

  bool HasRemoteSettingEchoScope(const ProfileSyncService& service) const {
    return service.applying_browser_setting_.has_value();
  }

  std::unique_ptr<TestingProfile> CreateProfile() {
    TestingProfile::Builder builder;
    // Each test owns the one service. An eagerly created keyed service would
    // start a second backend on the same database when Sync is enabled and
    // race the test's service for SQLite's exclusive lock.
    builder.AddTestingFactory(
        ProfileSyncServiceFactory::GetInstance(),
        base::BindRepeating(
            [](content::BrowserContext*) -> std::unique_ptr<KeyedService> {
              return {};
            }));
    return builder.Build();
  }

  // The open backend holds SQLite's exclusive lock, so a live store is read
  // on the backend sequence; a stopped one is read from the closed file.
  std::optional<StoreCounts> ReadCounts(ProfileSyncService* service,
                                        const base::FilePath& path) {
    if (service->backend_.is_null()) {
      return ReadStoreCounts(path);
    }
    std::optional<StoreCounts> counts;
    service->backend_.PostTaskWithThisObject(
        base::BindOnce(&ProfileSyncServiceTest::CountLiveStore, &counts));
    service->backend_.FlushPostedTasksForTesting();
    return counts;
  }

  std::optional<int> ReadActivePayloadCount(ProfileSyncService* service,
                                            const base::FilePath& path,
                                            EntityType entity_type,
                                            const std::string& fragment) {
    if (service->backend_.is_null()) {
      return ReadActiveRecordPayloadCount(path, entity_type, fragment);
    }
    std::optional<int> count;
    service->backend_.PostTaskWithThisObject(
        base::BindOnce(&ProfileSyncServiceTest::CountLivePayloads, entity_type,
                       fragment, &count));
    service->backend_.FlushPostedTasksForTesting();
    return count;
  }

  static void CountLiveStore(std::optional<StoreCounts>* out,
                             const ProfileSyncBackend& backend) {
    const SyncStore* store = backend.store_.get();
    if (!store) {
      return;
    }
    StoreCounts counts;
    for (int raw = static_cast<int>(EntityType::kDevice);
         raw <= static_cast<int>(EntityType::kTabArchiveEntry); ++raw) {
      const EntityType type = static_cast<EntityType>(raw);
      std::vector<SyncRecord> records;
      if (store->GetRecords(type, &records) != SyncStore::Result::kOk) {
        return;
      }
      const int size = static_cast<int>(records.size());
      counts.records += size;
      if (type == EntityType::kHistoryEntry) {
        counts.history = size;
      } else if (type == EntityType::kRemoteTab) {
        counts.tabs = size;
        counts.active_tabs = static_cast<int>(std::ranges::count_if(
            records,
            [](const SyncRecord& record) { return !IsTombstone(record); }));
      }
    }
    const int64_t outbox = store->PendingOutboxCount();
    if (outbox < 0) {
      return;
    }
    counts.outbox = static_cast<int>(outbox);
    *out = counts;
  }

  static void CountLivePayloads(EntityType entity_type,
                                const std::string& fragment,
                                std::optional<int>* out,
                                const ProfileSyncBackend& backend) {
    const SyncStore* store = backend.store_.get();
    std::vector<SyncRecord> records;
    if (!store ||
        store->GetRecords(entity_type, &records) != SyncStore::Result::kOk) {
      return;
    }
    int count = 0;
    for (const SyncRecord& record : records) {
      std::string payload;
      if (IsTombstone(record)) {
        continue;
      }
      if (!SerializeRecord(record, &payload)) {
        return;
      }
      if (payload.find(fragment) != std::string::npos) {
        ++count;
      }
    }
    *out = count;
  }

  base::FilePath DatabasePath(const TestingProfile& profile) const {
    return profile.GetPath()
        .AppendASCII("Ahoi Sync")
        .AppendASCII("sync-format3.sqlite");
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
      ReadCounts(&service, DatabasePath(*profile));
  ASSERT_TRUE(counts.has_value());
  // ADR 0009: shared-tab writes wait for the provider-acknowledged
  // capability bootstrap. Without CloudKit the capture request is issued but
  // no Presence is authored; the obsolete PublishWindowTabs() vector is
  // ignored.
  EXPECT_EQ(0, counts->tabs);
  EXPECT_EQ(0, counts->active_tabs);
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
      ReadCounts(&service, DatabasePath(*profile));
  ASSERT_TRUE(reenabled_counts.has_value());
  EXPECT_EQ(0, reenabled_counts->tabs);
  EXPECT_EQ(0, reenabled_counts->active_tabs);
  EXPECT_EQ(0, ReadActivePayloadCount(&service, DatabasePath(*profile),
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
  // Revocation is local and fail-closed, so it is never blocked.
  EXPECT_FALSE(keys_after_blocked_mutations.contains(
      approved_device.AsLowercaseString()));

  PublishTabsNow(&service, "enabled-tab", "https://enabled.example/");
  RecordHistoryVisit(&service, GURL("https://enabled.example/history"), 2);
  DrainBackend(&service);

  const std::optional<StoreCounts> enabled_counts =
      ReadCounts(&service, database_path);
  ASSERT_TRUE(enabled_counts.has_value());
  EXPECT_GT(enabled_counts->records, 0);
  EXPECT_GT(enabled_counts->outbox, 0);
  EXPECT_EQ(1, enabled_counts->history);
  // No Presence before the provider-acknowledged shared-tab bootstrap.
  EXPECT_EQ(0, enabled_counts->tabs);
  EXPECT_EQ(0, enabled_counts->active_tabs);

  // The opt-out revokes the profile scope synchronously, before the backend
  // runs a write that was posted earlier, so the in-flight visit is dropped
  // and the durable state at opt-out is the cutoff.
  RecordHistoryVisit(&service, GURL("https://in-flight.example/history"), 3);
  service.SetSyncEnabled(false);
  EXPECT_FALSE(service.sync_enabled());
  EXPECT_TRUE(BackendIsNull(service));
  DrainBackendRunner(&service);
  const std::optional<StoreCounts> cutoff_counts =
      ReadStoreCounts(database_path);
  ASSERT_TRUE(cutoff_counts.has_value());
  EXPECT_EQ(enabled_counts, cutoff_counts);

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

  // The seed waits for the provider's completed initial fetch, so a local
  // value never overwrites remote state it has not seen yet. Without CloudKit
  // the selection is retained but nothing is authored.
  EXPECT_TRUE(std::ranges::contains(
      service.permitted_setting_ids(),
      std::string(appearance::kSidebarPageTintEnabledPref)));
  EXPECT_EQ(0, ReadActivePayloadCount(
                   &service, database_path, EntityType::kPermittedSetting,
                   appearance::kSidebarPageTintEnabledPref));

  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest,
       ReentrantDifferentSettingRemainsLocalAndSkipsLaterRemoteOverwrite) {
  auto profile = CreateProfile();
  auto* prefs = profile->GetPrefs();
  prefs->SetBoolean(appearance::kGlassEnabledPref, false);
  prefs->SetBoolean(appearance::kSidebarPageTintEnabledPref, false);
  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kGlassEnabledPref, true));
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kSidebarPageTintEnabledPref, true));
  service.SetSyncEnabled(true);
  DrainBackend(&service);
  PrefChangeRegistrar local;
  local.Init(prefs);
  local.Add(appearance::kGlassEnabledPref, base::BindRepeating(
      [](PrefService* prefs) {
        prefs->SetBoolean(appearance::kSidebarPageTintEnabledPref, true);
      }, prefs));
  const auto later = RemoteSetting(appearance::kSidebarPageTintEnabledPref, "false");
  ApplySettingProjection(&service, {RemoteSetting(appearance::kGlassEnabledPref, "true"), later});
  EXPECT_TRUE(prefs->GetBoolean(appearance::kGlassEnabledPref));
  EXPECT_TRUE(prefs->GetBoolean(appearance::kSidebarPageTintEnabledPref));
  EXPECT_FALSE(PendingSetting(*profile, appearance::kGlassEnabledPref));
  const auto intent = PendingSetting(*profile, appearance::kSidebarPageTintEnabledPref);
  ASSERT_TRUE(intent);
  EXPECT_EQ("true", intent->value_json);
  EXPECT_GT(intent->version, later.version);
  EXPECT_FALSE(HasRemoteSettingEchoScope(service));
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest, ReentrantSameSettingIsNotMistakenForRemoteEcho) {
  auto profile = CreateProfile();
  auto* prefs = profile->GetPrefs();
  prefs->SetBoolean(appearance::kGlassEnabledPref, false);
  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kGlassEnabledPref, true));
  service.SetSyncEnabled(true);
  DrainBackend(&service);
  PrefChangeRegistrar local;
  local.Init(prefs);
  local.Add(appearance::kGlassEnabledPref, base::BindRepeating(
      [](PrefService* prefs) {
        if (prefs->GetBoolean(appearance::kGlassEnabledPref)) {
          prefs->SetBoolean(appearance::kGlassEnabledPref, false);
        }
      }, prefs));
  const auto remote = RemoteSetting(appearance::kGlassEnabledPref, "true");
  ApplySettingProjection(&service, {remote});
  EXPECT_FALSE(prefs->GetBoolean(appearance::kGlassEnabledPref));
  const auto intent = PendingSetting(*profile, appearance::kGlassEnabledPref);
  ASSERT_TRUE(intent);
  EXPECT_EQ("false", intent->value_json);
  EXPECT_GT(intent->version, remote.version);
  EXPECT_FALSE(HasRemoteSettingEchoScope(service));
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest, NormalizedNumericRemoteValueDoesNotAuthorAnEcho) {
  auto profile = CreateProfile();
  auto* prefs = profile->GetPrefs();
  prefs->SetInteger(appearance::kFloatingNavigationAutoHideDelayMsPref, 500);
  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kFloatingNavigationAutoHideDelayMsPref, true));
  service.SetSyncEnabled(true);
  DrainBackend(&service);
  ApplySettingProjection(&service,
      {RemoteSetting(appearance::kFloatingNavigationAutoHideDelayMsPref, "1000.0")});
  EXPECT_EQ(1000, prefs->GetInteger(appearance::kFloatingNavigationAutoHideDelayMsPref));
  EXPECT_FALSE(PendingSetting(*profile, appearance::kFloatingNavigationAutoHideDelayMsPref));
  EXPECT_FALSE(HasRemoteSettingEchoScope(service));
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest, ReentrantLocalValueBounceRetainsItsLatestIntent) {
  auto profile = CreateProfile();
  auto* prefs = profile->GetPrefs();
  prefs->SetBoolean(appearance::kGlassEnabledPref, false);
  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kGlassEnabledPref, true));
  service.SetSyncEnabled(true);
  DrainBackend(&service);
  bool edited = false;
  PrefChangeRegistrar local;
  local.Init(prefs);
  local.Add(appearance::kGlassEnabledPref, base::BindRepeating(
      [](PrefService* prefs, bool* edited) {
        if (*edited) {
          return;
        }
        *edited = true;
        prefs->SetBoolean(appearance::kGlassEnabledPref, false);
        prefs->SetBoolean(appearance::kGlassEnabledPref, true);
      }, prefs, &edited));
  const auto remote = RemoteSetting(appearance::kGlassEnabledPref, "true");
  ApplySettingProjection(&service, {remote});
  EXPECT_TRUE(prefs->GetBoolean(appearance::kGlassEnabledPref));
  const auto intent = PendingSetting(*profile, appearance::kGlassEnabledPref);
  ASSERT_TRUE(intent);
  EXPECT_EQ("true", intent->value_json);
  EXPECT_GT(intent->version, remote.version);
  EXPECT_FALSE(HasRemoteSettingEchoScope(service));
  service.Shutdown();
}

TEST_F(ProfileSyncServiceTest, ReentrantSyncRevocationDoesNotRestoreOldEchoScope) {
  auto profile = CreateProfile();
  auto* prefs = profile->GetPrefs();
  prefs->SetBoolean(appearance::kGlassEnabledPref, false);
  ProfileSyncService service(profile.get());
  ASSERT_TRUE(service.SetPermittedSettingSyncEnabled(appearance::kGlassEnabledPref, true));
  service.SetSyncEnabled(true);
  DrainBackend(&service);
  PrefChangeRegistrar local;
  local.Init(prefs);
  local.Add(appearance::kGlassEnabledPref, base::BindRepeating(
      [](ProfileSyncService* service) { service->SetSyncEnabled(false); }, &service));
  ApplySettingProjection(&service, {RemoteSetting(appearance::kGlassEnabledPref, "true")});
  EXPECT_FALSE(HasRemoteSettingEchoScope(service));
  EXPECT_FALSE(PendingSetting(*profile, appearance::kGlassEnabledPref));
  service.Shutdown();
}

}  // namespace ahoi::sync
