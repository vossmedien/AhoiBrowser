// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/shared_tab_target_policy.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

using Participation = NativeSharedTabParticipation;
using Kind = SharedTabTargetKind;
using Action = SharedTabTargetAction;

class SharedTabTargetPolicyTest : public testing::Test {
 protected:
  const base::Uuid kPageId =
      base::Uuid::ParseLowercase("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  const base::Uuid kOtherPageId =
      base::Uuid::ParseLowercase("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
};

TEST_F(SharedTabTargetPolicyTest, ConsumesCanonicalV3TargetCases) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(
      root.AppendASCII(
          "ahoi/browser/sync/testdata/shared_tab_wire_v3_contract.json"),
      &bytes));
  const auto fixture = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  ASSERT_TRUE(fixture);
  const auto* cases = fixture->FindList("target_cases");
  ASSERT_TRUE(cases);
  ASSERT_FALSE(cases->empty());
  for (const auto& value : *cases) {
    ASSERT_TRUE(value.is_dict());
    const auto& item = value.GetDict();
    const auto* name = item.FindString("name");
    ASSERT_TRUE(name);
    SCOPED_TRACE(*name);
    const auto kind = item.FindInt("target_kind");
    const auto* url = item.FindString("url");
    const auto* entity = item.FindString("entity");
    const auto valid = item.FindBool("valid");
    ASSERT_TRUE(kind);
    ASSERT_TRUE(url);
    ASSERT_TRUE(entity);
    ASSERT_TRUE(valid);
    SharedTabTarget target{.kind = static_cast<Kind>(*kind), .url = *url};
    if (const auto* scheme = item.FindString("local_scheme")) {
      target.local_scheme = *scheme;
    }
    if (*entity == "page") {
      EXPECT_EQ(*valid,
                IsValidSharedPageTarget(
                    target, item.FindBool("is_temporary").value_or(false)));
    } else {
      ASSERT_EQ("presence", *entity);
      std::optional<base::Uuid> linked_id;
      if (const auto* id = item.FindString("tree_node_id")) {
        linked_id = base::Uuid::ParseLowercase(*id);
      }
      EXPECT_EQ(*valid, SharedTabPresenceMatchesPage(target, linked_id, kPageId,
                                                     target, true));
    }
    if (item.FindBool("activate_on_peer") == false) {
      EXPECT_EQ(Action::kUnavailable,
                SelectSharedTabTargetAction(kPageId, target, true));
    }
  }
}

TEST_F(SharedTabTargetPolicyTest,
       OnlyExplicitEmptyTemporaryTargetsBecomeNewTabs) {
  EXPECT_FALSE(DescribeNativeSharedTabTarget(GURL()));
  EXPECT_FALSE(DescribeNativeSharedTabTarget(GURL(), Participation::kNormal));
  const auto target = DescribeNativeSharedTabTarget(
      GURL(), Participation::kExplicitEmptyTemporary);
  ASSERT_TRUE(target);
  EXPECT_EQ(Kind::kNewTab, target->kind);
  EXPECT_TRUE(IsValidSharedPageTarget(*target, true));
  EXPECT_FALSE(IsValidSharedPageTarget(*target, false));
  EXPECT_FALSE(DescribeNativeSharedTabTarget(
      GURL("https://example.com/"), Participation::kExplicitEmptyTemporary));
  EXPECT_FALSE(DescribeNativeSharedTabTarget(GURL("https://example.com/"),
                                             Participation::kExcluded));
}

TEST_F(SharedTabTargetPolicyTest, NativeLocalTargetsLoseAllPrivateUrlBytes) {
  for (const auto* url :
       {"file:///fixture/private.txt", "javascript:secret()",
        "data:text/plain,private", "blob:https://example.com/id",
        "chrome-extension://fixture/private.html", "chrome://settings/",
        "about:blank", "custom-protocol:private",
        "https://user:secret@example.com/"}) {
    SCOPED_TRACE(url);
    const auto target =
        DescribeNativeSharedTabTarget(GURL(url), Participation::kNormal);
    ASSERT_TRUE(target);
    EXPECT_EQ(Kind::kLocalOnly, target->kind);
    EXPECT_TRUE(target->url.empty());
    EXPECT_TRUE(IsValidSharedPageTarget(*target, true));
    EXPECT_EQ(Action::kUnavailable,
              SelectSharedTabTargetAction(kPageId, *target, true));
    EXPECT_EQ(Action::kExistingLocalRuntime,
              SelectSharedTabTargetAction(kPageId, *target, true, kPageId));
  }
}

TEST_F(SharedTabTargetPolicyTest, TargetGroupsAreCanonicalBoundedAndExact) {
  const SharedTabTarget web{.kind = Kind::kWeb, .url = "https://example.com/"};
  EXPECT_EQ(web, DescribeNativeSharedTabTarget(GURL(web.url),
                                               Participation::kNormal));
  EXPECT_TRUE(IsValidSharedPageTarget(web, false));
  auto extra = web;
  extra.local_scheme = "";
  EXPECT_FALSE(IsValidSharedPageTarget(extra, true));
  extra = web;
  extra.url = "HTTPS://EXAMPLE.COM";
  EXPECT_FALSE(IsValidSharedPageTarget(extra, true));
  extra.url = "https://example.com/" + std::string(131072, 'a');
  EXPECT_FALSE(IsValidSharedPageTarget(extra, true));
  extra.url = web.url + std::string(131072 - web.url.size(), 'a');
  EXPECT_TRUE(IsValidSharedPageTarget(extra, true));
  extra = web;
  extra.kind = static_cast<Kind>(99);
  EXPECT_FALSE(IsValidSharedPageTarget(extra, true));
  EXPECT_FALSE(IsValidSharedPageTarget({.kind = Kind::kLocalOnly}, true));
  EXPECT_FALSE(IsValidSharedPageTarget(
      {.kind = Kind::kLocalOnly, .local_scheme = "FILE"}, true));
}

TEST_F(SharedTabTargetPolicyTest,
       PresenceRequiresIdentityAndAtomicTargetAgreement) {
  const SharedTabTarget local{.kind = Kind::kLocalOnly, .local_scheme = "file"};
  EXPECT_TRUE(
      SharedTabPresenceMatchesPage(local, kPageId, kPageId, local, true));
  EXPECT_FALSE(
      SharedTabPresenceMatchesPage(local, std::nullopt, kPageId, local, true));
  EXPECT_FALSE(
      SharedTabPresenceMatchesPage(local, kOtherPageId, kPageId, local, true));
  auto other = local;
  other.local_scheme = "javascript";
  EXPECT_FALSE(
      SharedTabPresenceMatchesPage(other, kPageId, kPageId, local, true));
  other = local;
  other.url = "file:///fixture/private.txt";
  EXPECT_FALSE(
      SharedTabPresenceMatchesPage(other, kPageId, kPageId, local, true));
}

TEST_F(SharedTabTargetPolicyTest, ActivationNeverInventsLocalTargetOrBinding) {
  const SharedTabTarget local{.kind = Kind::kLocalOnly, .local_scheme = "file"};
  EXPECT_EQ(Action::kUnavailable,
            SelectSharedTabTargetAction(kPageId, local, true, kOtherPageId));
  EXPECT_EQ(Action::kUnavailable, SelectSharedTabTargetAction(
                                      base::Uuid(), local, true, base::Uuid()));
  EXPECT_EQ(Action::kExistingLocalRuntime,
            SelectSharedTabTargetAction(kPageId, local, false, kPageId));
  EXPECT_EQ(
      Action::kPlatformNewTab,
      SelectSharedTabTargetAction(kPageId, {.kind = Kind::kNewTab}, true));
  EXPECT_EQ(Action::kUnavailable, SelectSharedTabTargetAction(
                                      kPageId, {.kind = Kind::kNewTab}, false));
  EXPECT_EQ(
      Action::kWebNavigation,
      SelectSharedTabTargetAction(
          kPageId, {.kind = Kind::kWeb, .url = "https://example.com/"}, false));
}

}  // namespace
}  // namespace ahoi::session
