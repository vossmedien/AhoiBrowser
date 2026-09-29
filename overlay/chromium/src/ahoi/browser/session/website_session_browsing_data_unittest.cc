// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/website_session_browsing_data.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/website_session_context.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_browsing_data_remover_delegate.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::session {
namespace {

using content::BrowsingDataFilterBuilder;
using content::BrowsingDataRemover;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

class WebsiteSessionBrowsingDataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_ = TestingProfile::Builder().Build();
    remover()->SetEmbedderDelegate(&embedder_delegate_);
  }

  void TearDown() override {
    // Site data models are deleted soon after their removal completes; that
    // must happen while their partitions still exist.
    task_environment_.RunUntilIdle();
    remover()->SetEmbedderDelegate(nullptr);
    profile_.reset();
  }

  PrefService* prefs() { return profile_->GetPrefs(); }
  BrowsingDataRemover* remover() { return profile_->GetBrowsingDataRemover(); }

  WebsiteSessionBinding BindWorkspace(bool own_website_sessions) {
    const base::Uuid id = base::Uuid::GenerateRandomV4();
    EXPECT_TRUE(
        BindNewWorkspaceWebsiteSessions(prefs(), id, {}, own_website_sessions));
    last_workspace_id_ = id;
    return FindWebsiteSessionBinding(prefs(), id)
        .value_or(WebsiteSessionBinding());
  }

  content::StoragePartitionConfig Config(const WebsiteSessionBinding& b) {
    return StoragePartitionConfigForWebsiteSession(profile_.get(), b);
  }

  content::StoragePartition* LoadPartition(const WebsiteSessionBinding& b) {
    return profile_->GetStoragePartition(Config(b));
  }

  bool IsLoaded(const WebsiteSessionBinding& b) {
    return profile_->GetStoragePartition(Config(b), /*can_create=*/false);
  }

  base::FilePath PathOf(const WebsiteSessionBinding& b) {
    return WebsiteSessionPartitionPath(profile_->GetPath(), b);
  }

  static bool AddCookie(content::StoragePartition* partition, const GURL& url) {
    std::unique_ptr<net::CanonicalCookie> cookie =
        net::CanonicalCookie::CreateForTesting(url, "A=1", base::Time::Now(),
                                               net::CookieSourceType::kOther);
    base::test::TestFuture<net::CookieAccessResult> future;
    partition->GetCookieManagerForBrowserProcess()->SetCanonicalCookie(
        *cookie, url, net::CookieOptions::MakeAllInclusive(),
        future.GetCallback());
    return future.Take().status.IsInclude();
  }

  static std::vector<std::string> CookieDomains(
      content::StoragePartition* partition) {
    base::test::TestFuture<const net::CookieList&> future;
    partition->GetCookieManagerForBrowserProcess()->GetAllCookies(
        future.GetCallback());
    std::vector<std::string> domains;
    for (const net::CanonicalCookie& cookie : future.Take()) {
      domains.push_back(cookie.Domain());
    }
    return domains;
  }

  std::vector<content::StoragePartitionConfig> RemoveAll(uint64_t mask) {
    std::unique_ptr<BrowsingDataFilterBuilder> filter =
        BrowsingDataFilterBuilder::Create(
            BrowsingDataFilterBuilder::Mode::kPreserve);
    base::test::TestFuture<std::vector<content::StoragePartitionConfig>> queued;
    RemoveWebsiteSessionPartitionData(
        profile_.get(), base::Time(), base::Time::Max(), mask,
        BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB, *filter,
        queued.GetCallback());
    std::vector<content::StoragePartitionConfig> reached = queued.Take();
    WaitForRemover();
    return reached;
  }

  // Returns once every partition's site data removal has completed.
  std::vector<content::StoragePartitionConfig> RemoveSiteData(
      const std::vector<url::Origin>& origins) {
    base::test::TestFuture<std::vector<content::StoragePartitionConfig>> done;
    RemoveWebsiteSessionSiteData(profile_.get(), origins, done.GetCallback());
    return done.Take();
  }

  void WaitForRemover() {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return remover()->GetPendingTaskCountForTesting() == 0u; }));
  }

  content::BrowserTaskEnvironment task_environment_;
  content::MockBrowsingDataRemoverDelegate embedder_delegate_;
  std::unique_ptr<TestingProfile> profile_;
  base::Uuid last_workspace_id_;
};

TEST_F(WebsiteSessionBrowsingDataTest, ReachesOwnAndRecoveryContextsOnce) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  const base::Uuid own_id = last_workspace_id_;
  BindWorkspace(false);
  const WebsiteSessionBinding retired = BindWorkspace(true);
  ASSERT_TRUE(RetireWebsiteSessionBinding(prefs(), last_workspace_id_,
                                          PathOf(retired)));
  // A duplicated Workspace shares its source's context.
  base::DictValue root = prefs()->GetDict(kWebsiteSessionBindingsPref).Clone();
  root.FindDict("workspaces")
      ->Set(base::Uuid::GenerateRandomV4().AsLowercaseString(),
            own.context_id.AsLowercaseString());
  prefs()->SetDict(kWebsiteSessionBindingsPref, std::move(root));
  ASSERT_EQ(own, FindWebsiteSessionBinding(prefs(), own_id));

  const std::optional<WebsiteSessionBinding> recovery =
      GetWebsiteSessionRecoveryBinding(prefs());
  ASSERT_TRUE(recovery.has_value());
  EXPECT_THAT(WebsiteSessionBindingsForDataRemoval(prefs()),
              UnorderedElementsAre(own, *recovery));
}

TEST_F(WebsiteSessionBrowsingDataTest, WithoutOwnSessionsNothingIsQueued) {
  BindWorkspace(false);
  EXPECT_THAT(WebsiteSessionBindingsForDataRemoval(prefs()), IsEmpty());
  EXPECT_THAT(RemoveAll(BrowsingDataRemover::DATA_TYPE_COOKIES), IsEmpty());
}

TEST_F(WebsiteSessionBrowsingDataTest, ClearsCookiesOfALoadedOwnPartition) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  content::StoragePartition* partition = LoadPartition(own);
  ASSERT_TRUE(partition);
  ASSERT_TRUE(AddCookie(partition, GURL("https://host1.com/")));
  ASSERT_TRUE(AddCookie(profile_->GetDefaultStoragePartition(),
                        GURL("https://host1.com/")));

  // Only StoragePartition types are forwarded, scoped to the own partition.
  std::unique_ptr<BrowsingDataFilterBuilder> expected =
      BrowsingDataFilterBuilder::Create(
          BrowsingDataFilterBuilder::Mode::kPreserve);
  expected->SetStoragePartitionConfig(Config(own));
  embedder_delegate_.ExpectCall(
      base::Time(), base::Time::Max(), BrowsingDataRemover::DATA_TYPE_COOKIES,
      BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB, expected.get());

  EXPECT_THAT(RemoveAll(BrowsingDataRemover::DATA_TYPE_COOKIES |
                        BrowsingDataRemover::DATA_TYPE_DOWNLOADS),
              ElementsAre(Config(own)));
  embedder_delegate_.VerifyAndClearExpectations();
  EXPECT_THAT(CookieDomains(partition), IsEmpty());
  // The default partition is the caller's own removal, not this helper's.
  EXPECT_THAT(CookieDomains(profile_->GetDefaultStoragePartition()),
              ElementsAre("host1.com"));
}

TEST_F(WebsiteSessionBrowsingDataTest, NeverCreatesAPartition) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  const std::optional<WebsiteSessionBinding> recovery =
      GetWebsiteSessionRecoveryBinding(prefs());
  ASSERT_TRUE(recovery.has_value());

  EXPECT_THAT(RemoveAll(BrowsingDataRemover::DATA_TYPE_COOKIES), IsEmpty());
  EXPECT_FALSE(IsLoaded(own));
  EXPECT_FALSE(IsLoaded(*recovery));
  EXPECT_FALSE(base::PathExists(PathOf(own)));
  EXPECT_FALSE(base::PathExists(PathOf(*recovery)));
}

TEST_F(WebsiteSessionBrowsingDataTest, ReachesAnUnloadedPartitionOnDisk) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  ASSERT_TRUE(base::CreateDirectory(PathOf(own)));
  ASSERT_FALSE(IsLoaded(own));

  EXPECT_THAT(RemoveAll(BrowsingDataRemover::DATA_TYPE_COOKIES),
              ElementsAre(Config(own)));
}

TEST_F(WebsiteSessionBrowsingDataTest, SkipsARetiredPartition) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  content::StoragePartition* partition = LoadPartition(own);
  ASSERT_TRUE(partition);
  ASSERT_TRUE(AddCookie(partition, GURL("https://host1.com/")));
  ASSERT_TRUE(
      RetireWebsiteSessionBinding(prefs(), last_workspace_id_, PathOf(own)));

  EXPECT_THAT(RemoveAll(BrowsingDataRemover::DATA_TYPE_COOKIES), IsEmpty());
  // The Workspace deletion owns this partition's removal.
  EXPECT_THAT(CookieDomains(partition), ElementsAre("host1.com"));
}

TEST_F(WebsiteSessionBrowsingDataTest, IgnoresModifierOnlyAndEmptyFilters) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  ASSERT_TRUE(LoadPartition(own));
  EXPECT_THAT(
      RemoveAll(BrowsingDataRemover::DATA_TYPE_DOWNLOADS |
                BrowsingDataRemover::DATA_TYPE_AVOID_CLOSING_CONNECTIONS),
      IsEmpty());

  std::unique_ptr<BrowsingDataFilterBuilder> nothing =
      BrowsingDataFilterBuilder::Create(
          BrowsingDataFilterBuilder::Mode::kDelete);
  base::test::TestFuture<std::vector<content::StoragePartitionConfig>> queued;
  RemoveWebsiteSessionPartitionData(
      profile_.get(), base::Time(), base::Time::Max(),
      BrowsingDataRemover::DATA_TYPE_COOKIES,
      BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB, *nothing,
      queued.GetCallback());
  EXPECT_THAT(queued.Take(), IsEmpty());
}

TEST_F(WebsiteSessionBrowsingDataTest, SiteDataRemovalKeepsSiblingHosts) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  content::StoragePartition* partition = LoadPartition(own);
  ASSERT_TRUE(partition);
  ASSERT_TRUE(AddCookie(partition, GURL("https://www.host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://app.host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://host2.com/")));

  // A site-details page clears its host only, like the default partition's
  // BrowsingDataModel does, not the whole registrable domain.
  EXPECT_THAT(RemoveSiteData({url::Origin::Create(
                  GURL("https://www.host1.com/"))}),
              ElementsAre(Config(own)));
  EXPECT_THAT(CookieDomains(partition),
              UnorderedElementsAre("app.host1.com", "host1.com", "host2.com"));
}

TEST_F(WebsiteSessionBrowsingDataTest, SiteGroupRemovalNamesEachHost) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  content::StoragePartition* partition = LoadPartition(own);
  ASSERT_TRUE(partition);
  ASSERT_TRUE(AddCookie(partition, GURL("https://www.host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://app.host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://host1.com/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://host2.com/")));

  // A site group passes each of its origins and its eTLD+1.
  EXPECT_THAT(
      RemoveSiteData({url::Origin::Create(GURL("https://www.host1.com/")),
                      url::Origin::Create(GURL("https://app.host1.com/")),
                      url::Origin::Create(GURL("https://host1.com/"))}),
      ElementsAre(Config(own)));
  EXPECT_THAT(CookieDomains(partition), ElementsAre("host2.com"));
}

TEST_F(WebsiteSessionBrowsingDataTest, SiteDataRemovalCoversLocalHosts) {
  const WebsiteSessionBinding own = BindWorkspace(true);
  content::StoragePartition* partition = LoadPartition(own);
  ASSERT_TRUE(partition);
  ASSERT_TRUE(AddCookie(partition, GURL("http://127.0.0.1:8080/")));
  ASSERT_TRUE(AddCookie(partition, GURL("https://host2.com/")));

  EXPECT_THAT(
      RemoveSiteData({url::Origin::Create(GURL("http://127.0.0.1:8080/"))}),
      ElementsAre(Config(own)));
  EXPECT_THAT(CookieDomains(partition), ElementsAre("host2.com"));
}

TEST_F(WebsiteSessionBrowsingDataTest, SiteDataRemovalNeverCreatesAPartition) {
  const WebsiteSessionBinding own = BindWorkspace(true);

  EXPECT_THAT(
      RemoveSiteData({url::Origin::Create(GURL("https://host1.com/"))}),
      IsEmpty());
  EXPECT_FALSE(IsLoaded(own));
  EXPECT_FALSE(base::PathExists(PathOf(own)));
}

}  // namespace
}  // namespace ahoi::session
