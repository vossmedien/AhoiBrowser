// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#endif

namespace ahoi::sync {
namespace {

constexpr char kDevice[] = "a7000000-0000-4000-8000-000000000001";
constexpr char kSession[] = "a7000000-0000-4000-8000-000000000002";
constexpr char kAppearance[] = "a7000000-0000-4000-8000-000000000003";

base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

// Stands in for a CloudKit provider whose key lease was revoked by the
// bootstrap's account notification. It never owns a CKSyncEngine account
// transition unless the test says so, and never starts a transport.
class LeaseRevokedProvider final : public SyncProvider {
 public:
  LeaseRevokedProvider(bool account_transition_pending, bool* destroyed)
      : account_transition_pending_(account_transition_pending),
        destroyed_(destroyed) {}
  ~LeaseRevokedProvider() override { *destroyed_ = true; }

  void Upload(std::vector<SyncChange>, UploadCallback callback) override {
    ADD_FAILURE() << "Account-transition tests must not upload";
    std::move(callback).Run(false, {}, "cancelled");
  }
  void Download(std::string, DownloadCallback callback) override {
    ADD_FAILURE() << "Account-transition tests must not download";
    std::move(callback).Run(false, {}, "cancelled");
  }
  void SetBookmarkSyncEnabled(bool) override {}
  bool IsAccountTransitionPending() override {
    return account_transition_pending_;
  }
  bool ConfirmAccountTransition(bool) override {
    ++provider_confirmations;
    return false;
  }

  int provider_confirmations = 0;

 private:
  const bool account_transition_pending_;
  const raw_ptr<bool> destroyed_;
};

}  // namespace

class ProfileSyncBackendAccountTransitionTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    // Transport stays off during Initialize so no real provider/key setup
    // starts; the tests inject the provider state reached at runtime.
    backend_ = std::make_unique<ProfileSyncBackend>(
        directory_.GetPath().AppendASCII("account.sqlite"), Id(kDevice),
        Id(kSession), "Account transition test",
        /*transport_enabled=*/false, /*history_retention_days=*/90,
        /*bookmark_sync_enabled=*/false,
        /*profile_authorization=*/base::BindRepeating([] { return true; }));
    ASSERT_TRUE(backend_->Initialize().has_value());
    ASSERT_TRUE(backend_
                    ->UpsertAppearance(AppearanceRecord{.id = Id(kAppearance),
                                                        .color_mode = "dark"})
                    .has_value());
    ASSERT_GT(backend_->store_->PendingOutboxCount(), 0);
  }

  void TearDown() override {
    backend_.reset();
    task_environment_.RunUntilIdle();
  }

  // The live 29 Sep 2026 state: a provider exists, the bootstrap reported
  // key_setup_account_changed, and the provider owns no transition itself.
  LeaseRevokedProvider* InjectLeaseRevokedProvider(bool provider_pending) {
    auto provider =
        std::make_unique<LeaseRevokedProvider>(provider_pending, &destroyed_);
    LeaseRevokedProvider* raw = provider.get();
    backend_->provider_ = std::move(provider);
    backend_->key_setup_issue_ = "key_setup_account_changed";
    backend_->transport_enabled_ = true;
    return raw;
  }

  bool StatusShowsAccountTransition() {
    // Mirrors ProfileSyncBackend::CurrentState(), which feeds the Settings
    // "iCloud-Accountwechsel benötigt Bestätigung" label and buttons.
    return backend_->key_setup_issue_ == "key_setup_account_changed" ||
           (backend_->provider_ &&
            backend_->provider_->IsAccountTransitionPending());
  }

  ProfileSyncBackend& backend() { return *backend_; }
  bool AwaitsKeySetupConfirmation() {
    return backend_->KeySetupAccountChangeAwaitsConfirmation();
  }
  bool ResetForKeySetup(bool allow_local_upload) {
    return backend_->ResetForKeySetupAccountChange(allow_local_upload);
  }
  int64_t Outbox() { return backend_->store_->PendingOutboxCount(); }
  bool HasProvider() const { return backend_->provider_ != nullptr; }
  const std::string& KeySetupIssue() const {
    return backend_->key_setup_issue_;
  }

  bool destroyed_ = false;

 private:
  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir directory_;
  std::unique_ptr<ProfileSyncBackend> backend_;
};

// Regression: with a provider present, the status reported a pending account
// transition (from key_setup_issue_) while ConfirmAccountTransition only
// handled the key-setup case when no provider existed, so both buttons were
// accepted by Settings and then silently did nothing.
TEST_F(ProfileSyncBackendAccountTransitionTest,
       KeySetupAccountChangeWithProviderRoutesToKeySetupRecovery) {
  InjectLeaseRevokedProvider(/*provider_pending=*/false);
  ASSERT_TRUE(StatusShowsAccountTransition());
  EXPECT_TRUE(AwaitsKeySetupConfirmation());
}

TEST_F(ProfileSyncBackendAccountTransitionTest,
       ProviderOwnedTransitionKeepsProviderConfirmationPath) {
  InjectLeaseRevokedProvider(/*provider_pending=*/true);
  EXPECT_FALSE(AwaitsKeySetupConfirmation());
  EXPECT_FALSE(ResetForKeySetup(false));
  EXPECT_TRUE(HasProvider());
  EXPECT_GT(Outbox(), 0);
}

TEST_F(ProfileSyncBackendAccountTransitionTest,
       NoUploadChoiceClearsOutboxAndDropsRevokedProvider) {
  InjectLeaseRevokedProvider(/*provider_pending=*/false);
  ASSERT_TRUE(ResetForKeySetup(
      /*allow_local_upload=*/false));
  EXPECT_EQ(Outbox(), 0);
  EXPECT_FALSE(HasProvider());
  EXPECT_TRUE(destroyed_);
  EXPECT_TRUE(KeySetupIssue().empty());
  EXPECT_FALSE(StatusShowsAccountTransition());
  // A second click after the reset is a no-op, not a second outbox rewrite.
  EXPECT_FALSE(ResetForKeySetup(false));
}

TEST_F(ProfileSyncBackendAccountTransitionTest,
       UploadChoiceRequeuesLocalRecordsAndDropsRevokedProvider) {
  InjectLeaseRevokedProvider(/*provider_pending=*/false);
  ASSERT_TRUE(ResetForKeySetup(
      /*allow_local_upload=*/true));
  EXPECT_GT(Outbox(), 0);
  EXPECT_FALSE(HasProvider());
  EXPECT_FALSE(StatusShowsAccountTransition());
}

TEST_F(ProfileSyncBackendAccountTransitionTest,
       ConfirmWithoutCloudKitConfigurationFailsClosed) {
#if BUILDFLAG(IS_MAC)
  const auto configuration = CloudKitSyncConfigurationMac::FromMainBundle();
  if (configuration && configuration->IsTransportConfigured()) {
    GTEST_SKIP() << "Test host carries a CloudKit configuration";
  }
#endif
  LeaseRevokedProvider* provider =
      InjectLeaseRevokedProvider(/*provider_pending=*/false);
  const int64_t outbox = Outbox();
  EXPECT_FALSE(backend().ConfirmAccountTransition(false));
  // Without a configured transport nothing is rewritten or dropped, and the
  // provider path never runs for a transition the provider does not own.
  EXPECT_EQ(Outbox(), outbox);
  EXPECT_TRUE(HasProvider());
  EXPECT_EQ(provider->provider_confirmations, 0);
  EXPECT_TRUE(StatusShowsAccountTransition());
}

}  // namespace ahoi::sync
