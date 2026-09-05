// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"
#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/check.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "chrome/test/base/testing_profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

constexpr char kDevice[] = "d2000000-0000-4000-8000-000000000001";
constexpr char kSession[] = "d2000000-0000-4000-8000-000000000002";
constexpr char kNativeGuid[] = "d2000000-0000-4000-8000-000000000003";
constexpr char kObservation[] = "d2000000-0000-4000-8000-000000000004";

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

struct ProviderAuthorizationState {
  std::atomic<uint64_t> generation{1};
  std::atomic<bool> allowed{true};

  void Revoke() {
    allowed.store(false, std::memory_order_release);
    generation.fetch_add(1, std::memory_order_acq_rel);
  }

  void Reapprove() {
    generation.fetch_add(1, std::memory_order_acq_rel);
    allowed.store(true, std::memory_order_release);
  }
};

BookmarkSyncAuthorization CaptureProviderAuthorization(
    std::shared_ptr<ProviderAuthorizationState> state) {
  const uint64_t generation = state->generation.load(std::memory_order_acquire);
  return base::BindRepeating(
      [](std::shared_ptr<ProviderAuthorizationState> state,
         uint64_t generation) {
        return state->allowed.load(std::memory_order_acquire) &&
               state->generation.load(std::memory_order_acquire) == generation;
      },
      std::move(state), generation);
}

class InertProvider : public SyncProvider {
 public:
  void Upload(std::vector<SyncChange>, UploadCallback callback) override {
    ADD_FAILURE() << "Authorization tests must not start a transport upload";
    std::move(callback).Run(false, {}, "inert test provider");
  }
  void Download(std::string, DownloadCallback callback) override {
    ADD_FAILURE() << "Authorization tests must not start a transport download";
    std::move(callback).Run(false, {}, "inert test provider");
  }
  void SetBookmarkSyncEnabled(bool) override {}
};

class ScopedProvider final : public InertProvider {
 public:
  explicit ScopedProvider(std::shared_ptr<ProviderAuthorizationState> state)
      : state_(std::move(state)) {}

  BookmarkSyncAuthorization GetBookmarkSyncAuthorization() override {
    return CaptureProviderAuthorization(state_);
  }
  bool IsBookmarkConsentRevoked() override {
    return !state_->allowed.load(std::memory_order_acquire);
  }

 private:
  // This fake deliberately keeps guards alive across provider destruction and
  // ignores the category setter. Backend cancellation must work independently
  // of the real Mac provider's separately tested lifetime/consent cancellation.
  std::shared_ptr<ProviderAuthorizationState> state_;
};

NativeBookmarkSnapshot NativePageSnapshot() {
  return {
      .entries = {{.native_key = NativeBookmarkKey(Id(kNativeGuid), false),
                   .root = BookmarkRoot::kBookmarkBar,
                   .kind = BookmarkKind::kUrl,
                   .title = "Scoped bookmark",
                   .url = "https://example.test/authorization",
                   .created_at = base::Time::UnixEpoch() + base::Seconds(1)}},
      .observation_session = kObservation};
}

NativeBookmarkSnapshot AppliedSnapshot(
    const BookmarkSyncProjection& projection) {
  EXPECT_EQ(1u, projection.records.size());
  EXPECT_EQ(1u, projection.bindings.size());
  if (projection.records.size() != 1u || projection.bindings.size() != 1u) {
    return {};
  }
  const auto& record = projection.records.front();
  const auto& binding = projection.bindings.front();
  EXPECT_EQ(record.id, binding.logical_id);
  EXPECT_FALSE(binding.apply_receipt.empty());
  return {.entries = {{.native_key = binding.native_key,
                       .root = record.root_kind,
                       .kind = record.kind,
                       .title = record.title,
                       .url = record.url,
                       .created_at = record.created_at,
                       .apply_receipt = binding.apply_receipt}},
          .observation_session = kObservation};
}

using Rows = std::vector<std::vector<std::string>>;
using DurableState = std::vector<Rows>;

}  // namespace

// Kept in ahoi::sync, rather than the anonymous namespace, for the production
// friend declarations. Generated TEST_F subclasses use only these fixture
// seams.
class BookmarkSyncAuthorizationTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  void TearDown() override {
    if (service_) {
      service_->Shutdown();
      service_.reset();
    }
    task_environment_.RunUntilIdle();
    merged_surface_.reset();
    model_.reset();
    profile_.reset();
    backend_.reset();
    task_environment_.RunUntilIdle();
  }

  void CreateBackend(std::unique_ptr<SyncProvider> provider) {
    backend_ = std::make_unique<ProfileSyncBackend>(
        DatabasePath(), Id(kDevice), Id(kSession), "Authorization test",
        /*transport_enabled=*/true, /*history_retention_days=*/90,
        /*bookmark_sync_enabled=*/true);
    // Initialize only the store. No device/session publication, real provider,
    // or SyncPump is needed to exercise the real bookmark backend methods.
    backend_->store_ = std::make_unique<SyncStore>();
    ASSERT_TRUE(backend_->store_->Initialize(DatabasePath()));
    backend_->provider_ = std::move(provider);
  }

  ProfileSyncBackend& backend() { return *backend_; }
  bool BackendCategoryCacheIsApproved() const {
    return backend_->bookmark_sync_enabled_;
  }

  base::FilePath DatabasePath() const {
    return directory_.GetPath().AppendASCII("authorization.sqlite");
  }

  std::optional<DurableState> ReadDurableState() const {
    if (!backend_ || !backend_->store_) {
      return std::nullopt;
    }
    SyncStore& store = *backend_->store_;
    DCHECK_CALLED_ON_VALID_SEQUENCE(store.sequence_checker_);
    DurableState result;
    // Include native baselines and receipt plans, not only wire records: a
    // refused ACK/read must not silently mutate that local journal either.
    for (const char* query :
         {"SELECT * FROM sync_records ORDER BY entity_type,entity_id",
          "SELECT * FROM sync_outbox ORDER BY mutation_id",
          "SELECT * FROM sync_bookmark_bindings ORDER BY native_key",
          "SELECT * FROM sync_bookmark_apply_receipts ORDER BY receipt_id"}) {
      sql::Statement statement(store.db_.GetUniqueStatement(query));
      Rows rows;
      while (statement.Step()) {
        std::vector<std::string> row;
        for (int column = 0; column < statement.ColumnCount(); ++column) {
          row.push_back(statement.ColumnString(column));
        }
        rows.push_back(std::move(row));
      }
      if (!statement.Succeeded()) {
        return std::nullopt;
      }
      result.push_back(std::move(rows));
    }
    return result;
  }

  void CreateStaleApprovedService() {
    TestingProfile::Builder builder;
    // The fixture owns the one service. Prevent an eager second service from
    // starting a real backend when the deliberately stale prefs are installed.
    builder.AddTestingFactory(
        ProfileSyncServiceFactory::GetInstance(),
        base::BindRepeating(
            [](content::BrowserContext*) -> std::unique_ptr<KeyedService> {
              return {};
            }));
    profile_ = builder.Build();
    service_ = std::make_unique<ProfileSyncService>(profile_.get());
    service_->sync_pref_registrar_.RemoveAll();
    profile_->GetPrefs()->SetBoolean(kSyncEnabledPref, true);
    profile_->GetPrefs()->SetBoolean(kBookmarkSyncEnabledPref, true);
    service_->sync_enabled_ = true;
    service_->backend_ready_ = true;
    service_->bookmarks_seeded_ = true;
    service_->backend_.emplace(
        service_->backend_task_runner_,
        directory_.GetPath().AppendASCII("unused-service-backend.sqlite"),
        Id(kDevice), Id(kSession), "Inert UI backend",
        /*transport_enabled=*/true, /*history_retention_days=*/90,
        /*bookmark_sync_enabled=*/true);
    // The SequenceBound backend exists but is never Initialize()d. Rejected
    // replies must fail because of authorization, not because it is absent.
    model_ = std::make_unique<bookmarks::BookmarkModel>(
        std::make_unique<bookmarks::TestBookmarkClient>());
    merged_surface_ =
        std::make_unique<BookmarkMergedSurfaceService>(model_.get(), nullptr);
    model_->LoadEmptyForTest();
    merged_surface_->LoadForTesting({});
    service_->bookmark_adapter_ = std::make_unique<NativeBookmarkSyncAdapter>(
        merged_surface_.get(),
        base::BindRepeating([](uint64_t, NativeBookmarkSnapshot) {}));
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(service_->bookmark_adapter_->ready());
  }

  bool ServiceCachesStillApprove() const {
    return service_->bookmark_sync_enabled() && service_->sync_enabled_ &&
           !service_->backend_.is_null() &&
           service_->bookmark_adapter_->ready();
  }

  ProfileSyncService::BookmarkSyncIssue ServiceIssue() const {
    return service_->bookmark_sync_issue();
  }

  void DeliverProjection(BookmarkSyncProjection projection) {
    service_->OnBookmarkProjection(service_->bookmark_adapter_->generation(),
                                   /*local_change=*/true,
                                   std::move(projection));
  }

  size_t NativeBookmarkBarCount() const {
    EXPECT_EQ(model_->bookmark_bar_node()->children().size(),
              merged_surface_->GetChildrenCount(
                  BookmarkParentFolder::BookmarkBarFolder()));
    return model_->bookmark_bar_node()->children().size();
  }

  const bookmarks::BookmarkNode* FirstNativeBookmark() const {
    return model_->bookmark_bar_node()->children().front().get();
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  std::unique_ptr<ProfileSyncBackend> backend_;
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<bookmarks::BookmarkModel> model_;
  std::unique_ptr<BookmarkMergedSurfaceService> merged_surface_;
  std::unique_ptr<ProfileSyncService> service_;
};

TEST_F(BookmarkSyncAuthorizationTest,
       ProviderRevocationOverridesStaleBackendConsentForMergeReadAndAck) {
  auto state = std::make_shared<ProviderAuthorizationState>();
  CreateBackend(std::make_unique<ScopedProvider>(state));
  const auto original = backend().MergeLocalBookmarks(NativePageSnapshot());
  ASSERT_TRUE(original);
  ASSERT_TRUE(original->authorization);
  ASSERT_TRUE(original->authorization.Run());
  const auto acknowledged = AppliedSnapshot(*original);
  const auto before = ReadDurableState();
  ASSERT_TRUE(before);
  ASSERT_FALSE((*before)[0].empty());
  ASSERT_FALSE((*before)[1].empty());

  state->Revoke();
  ASSERT_TRUE(BackendCategoryCacheIsApproved());
  EXPECT_FALSE(original->authorization.Run());
  auto edited = acknowledged;
  edited.entries.front().title = "Must not enter the journal";
  EXPECT_FALSE(backend().MergeLocalBookmarks(edited));
  EXPECT_FALSE(backend().ReadBookmarkProjection());
  EXPECT_FALSE(backend().AcknowledgeNativeBookmarks(acknowledged,
                                                    original->authorization));
  EXPECT_EQ(before, ReadDurableState());

  state->Reapprove();
  EXPECT_FALSE(original->authorization.Run());
  EXPECT_FALSE(backend().AcknowledgeNativeBookmarks(acknowledged,
                                                    original->authorization));
  EXPECT_EQ(before, ReadDurableState());
  const auto current = backend().ReadBookmarkProjection();
  ASSERT_TRUE(current);
  ASSERT_TRUE(current->authorization);
  EXPECT_TRUE(current->authorization.Run());
  EXPECT_TRUE(backend().AcknowledgeNativeBookmarks(AppliedSnapshot(*current),
                                                   current->authorization));
}

TEST_F(BookmarkSyncAuthorizationTest,
       DelayedServiceReplyCannotApplyAfterRevocationOrReapproval) {
  auto state = std::make_shared<ProviderAuthorizationState>();
  CreateBackend(std::make_unique<ScopedProvider>(state));
  const auto delayed = backend().MergeLocalBookmarks(NativePageSnapshot());
  ASSERT_TRUE(delayed);
  ASSERT_TRUE(delayed->authorization);
  ASSERT_TRUE(delayed->authorization.Run());
  CreateStaleApprovedService();
  ASSERT_TRUE(ServiceCachesStillApprove());
  ASSERT_EQ(0u, NativeBookmarkBarCount());

  state->Revoke();
  ASSERT_TRUE(ServiceCachesStillApprove());
  DeliverProjection(*delayed);
  EXPECT_EQ(0u, NativeBookmarkBarCount());
  EXPECT_EQ(ProfileSyncService::BookmarkSyncIssue::kAuthorizationChanged,
            ServiceIssue());

  state->Reapprove();
  ASSERT_TRUE(ServiceCachesStillApprove());
  EXPECT_FALSE(delayed->authorization.Run());
  DeliverProjection(*delayed);
  EXPECT_EQ(0u, NativeBookmarkBarCount());
  EXPECT_EQ(ProfileSyncService::BookmarkSyncIssue::kAuthorizationChanged,
            ServiceIssue());

  const auto fresh = backend().ReadBookmarkProjection();
  ASSERT_TRUE(fresh);
  ASSERT_TRUE(fresh->authorization);
  ASSERT_TRUE(fresh->authorization.Run());
  DeliverProjection(*fresh);
  ASSERT_EQ(1u, NativeBookmarkBarCount());
  EXPECT_EQ(Id(kNativeGuid), FirstNativeBookmark()->uuid());
  EXPECT_EQ(u"Scoped bookmark", FirstNativeBookmark()->GetTitle());
  EXPECT_EQ(GURL("https://example.test/authorization"),
            FirstNativeBookmark()->url());
}

TEST_F(BookmarkSyncAuthorizationTest,
       CategoryAndGlobalSuspensionInvalidateTheBackendScope) {
  auto state = std::make_shared<ProviderAuthorizationState>();
  CreateBackend(std::make_unique<ScopedProvider>(state));
  const auto first = backend().MergeLocalBookmarks(NativePageSnapshot());
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->authorization);
  ASSERT_TRUE(first->authorization.Run());
  auto provider_only = CaptureProviderAuthorization(state);

  std::ignore = backend().SetBookmarkSyncEnabled(false);
  EXPECT_TRUE(provider_only.Run());
  EXPECT_FALSE(first->authorization.Run());
  EXPECT_FALSE(backend().MergeLocalBookmarks(NativePageSnapshot()));
  EXPECT_FALSE(backend().ReadBookmarkProjection());
  std::ignore = backend().SetBookmarkSyncEnabled(true);
  const auto renewed = backend().ReadBookmarkProjection();
  ASSERT_TRUE(renewed);
  ASSERT_TRUE(renewed->authorization);
  EXPECT_TRUE(renewed->authorization.Run());
  EXPECT_FALSE(first->authorization.Run());
  EXPECT_FALSE(backend().AcknowledgeNativeBookmarks(AppliedSnapshot(*first),
                                                    first->authorization));

  backend().SuspendWithoutPersisting();
  // Provider guards deliberately survive in this fake, so the backend's own
  // cancelled scope is what must make the captured authorization fail here.
  EXPECT_TRUE(provider_only.Run());
  EXPECT_FALSE(renewed->authorization.Run());
  EXPECT_FALSE(backend().MergeLocalBookmarks(NativePageSnapshot()));
  EXPECT_FALSE(backend().ReadBookmarkProjection());
}

TEST_F(BookmarkSyncAuthorizationTest,
       ExistingProviderWithoutAuthorizationFailsClosed) {
  CreateBackend(std::make_unique<InertProvider>());
  ASSERT_TRUE(BackendCategoryCacheIsApproved());
  const auto before = ReadDurableState();
  ASSERT_TRUE(before);
  EXPECT_FALSE(backend().MergeLocalBookmarks(NativePageSnapshot()));
  EXPECT_FALSE(backend().ReadBookmarkProjection());
  EXPECT_FALSE(backend().AcknowledgeNativeBookmarks(NativePageSnapshot(), {}));
  EXPECT_EQ(before, ReadDurableState());
}

}  // namespace ahoi::sync
