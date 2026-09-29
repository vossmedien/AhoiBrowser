// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing_editor.h"

#include <set>
#include <string>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::navigation {
namespace {

RoutingRule Rule(std::string host,
                 const base::Uuid& target,
                 bool include_subdomains = false,
                 std::string path_prefix = std::string()) {
  return {.id = base::Uuid::GenerateRandomV4(),
          .host = std::move(host),
          .include_subdomains = include_subdomains,
          .path_prefix = std::move(path_prefix),
          .target_workspace_id = target};
}

base::DictValue EditorRule(std::string host,
                           std::string path,
                           std::string target,
                           std::string mode = "normal_tab") {
  base::DictValue value;
  value.Set("enabled", true);
  value.Set("host", std::move(host));
  value.Set("includeSubdomains", true);
  value.Set("path", std::move(path));
  value.Set("target", std::move(target));
  value.Set("mode", std::move(mode));
  return value;
}

class LinkRoutingEditorTest : public testing::Test {
 protected:
  const base::Uuid work_ = base::Uuid::GenerateRandomV4();
  const base::Uuid home_ = base::Uuid::GenerateRandomV4();
  const std::set<base::Uuid> allowed_{work_, home_};
};

TEST_F(LinkRoutingEditorTest, EditorValueIsNormalizedAndRoundTrips) {
  const base::Uuid id = base::Uuid::GenerateRandomV4();
  auto rule =
      RuleFromEditorValue(EditorRule("Mail.Example.COM.", "/Inbox/",
                                     work_.AsLowercaseString(), "quick_window"),
                          id, allowed_);
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ(id, rule->id);
  EXPECT_EQ("mail.example.com", rule->host);
  EXPECT_TRUE(rule->include_subdomains);
  EXPECT_EQ("/Inbox", rule->path_prefix);
  EXPECT_EQ(work_, rule->target_workspace_id);
  EXPECT_EQ(LinkOpenMode::kQuickWindow, rule->mode);

  const base::DictValue value = RuleToEditorValue(*rule);
  EXPECT_EQ(id.AsLowercaseString(), *value.FindString("id"));
  auto again = RuleFromEditorValue(value, id, allowed_);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(*rule, *again);

  // The stored form survives the strict pref codec unchanged.
  const RoutingSettings settings{.rules = {*rule}};
  EXPECT_EQ(settings, ParseRoutingSettings(SerializeRoutingSettings(settings)));
}

TEST_F(LinkRoutingEditorTest, EditorValueReportsEachValidationError) {
  const base::Uuid id = base::Uuid::GenerateRandomV4();
  const std::string work = work_.AsLowercaseString();
  EXPECT_EQ(
      RoutingEditError::kInvalidHost,
      RuleFromEditorValue(EditorRule("https://a.com", "", work), id, allowed_)
          .error());
  EXPECT_EQ(
      RoutingEditError::kInvalidHost,
      RuleFromEditorValue(EditorRule("a.com:8080", "", work), id, allowed_)
          .error());
  EXPECT_EQ(
      RoutingEditError::kInvalidPath,
      RuleFromEditorValue(EditorRule("a.com", "inbox", work), id, allowed_)
          .error());
  EXPECT_EQ(
      RoutingEditError::kInvalidPath,
      RuleFromEditorValue(EditorRule("a.com", "/a?b=1", work), id, allowed_)
          .error());
  EXPECT_EQ(
      RoutingEditError::kInvalidTarget,
      RuleFromEditorValue(EditorRule("a.com", "", "not-a-uuid"), id, allowed_)
          .error());
  // A valid UUID that is not a choosable Workspace (for example a Profile or
  // partition id) is rejected.
  EXPECT_EQ(RoutingEditError::kInvalidTarget,
            RuleFromEditorValue(
                EditorRule("a.com", "",
                           base::Uuid::GenerateRandomV4().AsLowercaseString()),
                id, allowed_)
                .error());
  EXPECT_EQ(RoutingEditError::kInvalidMode,
            RuleFromEditorValue(EditorRule("a.com", "", work, "incognito"), id,
                                allowed_)
                .error());
  base::DictValue missing = EditorRule("a.com", "", work);
  missing.Remove("enabled");
  EXPECT_EQ(RoutingEditError::kInvalidRequest,
            RuleFromEditorValue(missing, id, allowed_).error());
  EXPECT_EQ("invalidHost",
            RoutingEditErrorName(RoutingEditError::kInvalidHost));
}

TEST_F(LinkRoutingEditorTest, DefaultRouteAcceptsLastActiveOrKnownWorkspace) {
  base::DictValue value;
  value.Set("target", "last_active");
  value.Set("mode", "quick_window");
  auto route = DefaultRouteFromEditorValue(value, allowed_);
  ASSERT_TRUE(route.has_value());
  EXPECT_FALSE(route->target_workspace_id);
  EXPECT_EQ(LinkOpenMode::kQuickWindow, route->mode);

  value.Set("target", home_.AsLowercaseString());
  route = DefaultRouteFromEditorValue(value, allowed_);
  ASSERT_TRUE(route.has_value());
  EXPECT_EQ(home_, route->target_workspace_id);

  value.Set("target", base::Uuid::GenerateRandomV4().AsLowercaseString());
  EXPECT_EQ(RoutingEditError::kInvalidTarget,
            DefaultRouteFromEditorValue(value, allowed_).error());
}

TEST_F(LinkRoutingEditorTest, AddUpdateDeleteAndMoveKeepOrder) {
  RoutingSettings settings;
  RoutingRule a = Rule("a.com", work_);
  RoutingRule b = Rule("b.com", home_);
  RoutingRule c = Rule("c.com", work_);
  EXPECT_EQ(RoutingEditError::kNone, AddRule(settings, a));
  EXPECT_EQ(RoutingEditError::kNone, AddRule(settings, b));
  EXPECT_EQ(RoutingEditError::kNone, AddRule(settings, c));
  EXPECT_EQ(RoutingEditError::kInvalidRequest, AddRule(settings, a));
  ASSERT_EQ(3u, settings.rules.size());

  EXPECT_EQ(RoutingEditError::kCannotMove, MoveRule(settings, a.id, -1));
  EXPECT_EQ(RoutingEditError::kCannotMove, MoveRule(settings, c.id, 1));
  EXPECT_EQ(RoutingEditError::kInvalidRequest, MoveRule(settings, b.id, 2));
  EXPECT_EQ(RoutingEditError::kNone, MoveRule(settings, c.id, -1));
  EXPECT_EQ(std::vector<RoutingRule>({a, c, b}), settings.rules);
  EXPECT_EQ(RoutingEditError::kNone, MoveRule(settings, a.id, 1));
  EXPECT_EQ(std::vector<RoutingRule>({c, a, b}), settings.rules);

  RoutingRule a_changed = a;
  a_changed.enabled = false;
  a_changed.target_workspace_id = home_;
  EXPECT_EQ(RoutingEditError::kNone, UpdateRule(settings, a_changed));
  EXPECT_EQ(std::vector<RoutingRule>({c, a_changed, b}), settings.rules);

  EXPECT_EQ(RoutingEditError::kNone, DeleteRule(settings, c.id));
  EXPECT_EQ(RoutingEditError::kUnknownRule, DeleteRule(settings, c.id));
  EXPECT_EQ(RoutingEditError::kUnknownRule, UpdateRule(settings, c));
  EXPECT_EQ(RoutingEditError::kUnknownRule, MoveRule(settings, c.id, 1));
  EXPECT_EQ(std::vector<RoutingRule>({a_changed, b}), settings.rules);

  EXPECT_EQ(RoutingSettings(), DefaultRoutingSettings());
}

TEST_F(LinkRoutingEditorTest, RememberRewritesTheWinningExactHostRule) {
  const base::Uuid gone = base::Uuid::GenerateRandomV4();
  RoutingRule exact = Rule("example.com", gone);
  RoutingSettings settings{.rules = {Rule("other.com", work_), exact}};
  const std::optional<size_t> index = RememberSiteChoice(
      settings, GURL("https://example.com/a?q=1"), home_,
      LinkOpenMode::kQuickWindow, base::Uuid::GenerateRandomV4());
  ASSERT_EQ(1u, index);
  ASSERT_EQ(2u, settings.rules.size());
  EXPECT_EQ(exact.id, settings.rules[1].id);
  EXPECT_EQ(home_, settings.rules[1].target_workspace_id);
  EXPECT_EQ(LinkOpenMode::kQuickWindow, settings.rules[1].mode);
}

TEST_F(LinkRoutingEditorTest, RememberInsertsBeforeTheWinnerOnly) {
  const base::Uuid gone = base::Uuid::GenerateRandomV4();
  RoutingRule first = Rule("mail.example.com", work_, false, "/inbox");
  RoutingRule parent = Rule("example.com", gone, /*include_subdomains=*/true);
  RoutingRule shadowed = Rule("mail.example.com", work_);
  RoutingSettings settings{.rules = {first, parent, shadowed}};
  const base::Uuid new_id = base::Uuid::GenerateRandomV4();
  const GURL url("https://mail.example.com/drafts");
  const std::optional<size_t> index = RememberSiteChoice(
      settings, url, home_, LinkOpenMode::kNormalTab, new_id);
  ASSERT_EQ(1u, index);
  ASSERT_EQ(3u, settings.rules.size());
  EXPECT_EQ(first, settings.rules[0]);
  EXPECT_EQ(new_id, settings.rules[1].id);
  EXPECT_EQ("mail.example.com", settings.rules[1].host);
  EXPECT_FALSE(settings.rules[1].include_subdomains);
  EXPECT_TRUE(settings.rules[1].path_prefix.empty());
  // The shadowed duplicate for the same host is replaced, not kept.
  EXPECT_EQ(parent, settings.rules[2]);

  // The earlier path rule still decides its own links; the new rule wins for
  // the chosen site.
  EXPECT_EQ(0u, ResolveRoute(settings, GURL("https://mail.example.com/inbox/1"))
                    .rule_index);
  EXPECT_EQ(home_, ResolveRoute(settings, url).target_workspace_id);
  EXPECT_EQ(settings, ParseRoutingSettings(SerializeRoutingSettings(settings)));
}

TEST_F(LinkRoutingEditorTest, RememberAppendsWhenTheDefaultRouteWon) {
  RoutingSettings settings{
      .rules = {Rule("a.com", work_)},
      .default_route = {.target_workspace_id = base::Uuid::GenerateRandomV4()}};
  const std::optional<size_t> index = RememberSiteChoice(
      settings, GURL("http://Example.COM./"), home_, LinkOpenMode::kNormalTab,
      base::Uuid::GenerateRandomV4());
  ASSERT_EQ(1u, index);
  EXPECT_EQ("example.com", settings.rules[1].host);

  RoutingSettings unchanged = settings;
  EXPECT_FALSE(RememberSiteChoice(settings, GURL("mailto:a@example.com"), home_,
                                  LinkOpenMode::kNormalTab,
                                  base::Uuid::GenerateRandomV4()));
  EXPECT_FALSE(RememberSiteChoice(settings, GURL("https://b.com/"),
                                  base::Uuid(), LinkOpenMode::kNormalTab,
                                  base::Uuid::GenerateRandomV4()));
  EXPECT_EQ(unchanged, settings);
}

}  // namespace
}  // namespace ahoi::navigation
