// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/link_routing_settings_model.h"

#include <string>
#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::settings {
namespace {

using navigation::LinkOpenMode;
using navigation::RoutingRule;
using navigation::RoutingSettings;

class LinkRoutingSettingsModelTest : public testing::Test {
 protected:
  base::DictValue RulePayload(std::string host,
                              const base::Uuid& target,
                              std::string path = std::string()) const {
    base::DictValue payload;
    payload.Set("enabled", true);
    payload.Set("host", std::move(host));
    payload.Set("includeSubdomains", false);
    payload.Set("path", std::move(path));
    payload.Set("target", target.AsLowercaseString());
    payload.Set("mode", "normal_tab");
    return payload;
  }

  const base::Uuid work_ = base::Uuid::GenerateRandomV4();
  const base::Uuid separated_ = base::Uuid::GenerateRandomV4();
  const std::vector<LinkRoutingWorkspace> workspaces_{
      {.id = work_, .name = "Arbeit"},
      {.id = separated_, .name = "Kunde", .separated = true}};
};

TEST_F(LinkRoutingSettingsModelTest, StateListsRulesTargetsAndWorkspaces) {
  const base::Uuid gone = base::Uuid::GenerateRandomV4();
  const RoutingSettings settings{
      .rules = {{.id = base::Uuid::GenerateRandomV4(),
                 .host = "a.com",
                 .target_workspace_id = work_},
                {.id = base::Uuid::GenerateRandomV4(),
                 .host = "b.com",
                 .target_workspace_id = gone,
                 .mode = LinkOpenMode::kQuickWindow}},
      .default_route = {.target_workspace_id = separated_}};
  const base::DictValue state =
      BuildLinkRoutingState(settings, workspaces_, /*can_change=*/true);
  EXPECT_EQ(true, state.FindBool("canChange"));
  EXPECT_EQ(true, state.FindBool("enabled"));
  const base::ListValue* rules = state.FindList("rules");
  ASSERT_TRUE(rules);
  ASSERT_EQ(2u, rules->size());
  EXPECT_EQ(true, (*rules)[0].GetDict().FindBool("targetAvailable"));
  EXPECT_EQ("Arbeit", *(*rules)[0].GetDict().FindString("targetName"));
  EXPECT_EQ(false, (*rules)[1].GetDict().FindBool("targetAvailable"));
  EXPECT_EQ("quick_window", *(*rules)[1].GetDict().FindString("mode"));
  const base::DictValue* default_route = state.FindDict("defaultRoute");
  ASSERT_TRUE(default_route);
  EXPECT_EQ(separated_.AsLowercaseString(),
            *default_route->FindString("target"));
  const base::ListValue* workspaces = state.FindList("workspaces");
  ASSERT_TRUE(workspaces);
  ASSERT_EQ(2u, workspaces->size());
  EXPECT_EQ(true, (*workspaces)[1].GetDict().FindBool("separated"));
  EXPECT_TRUE(state.FindDict("labels")->FindString("includeSubdomains"));
  EXPECT_TRUE(state.FindDict("labels")->FindString("hostPortHint"));
}

TEST_F(LinkRoutingSettingsModelTest, ActionsEditInOrderAndReportErrors) {
  RoutingSettings settings;
  EXPECT_EQ("",
            ApplyLinkRoutingAction(settings, "addRule",
                                   RulePayload("A.com", work_), workspaces_));
  EXPECT_EQ("", ApplyLinkRoutingAction(settings, "addRule",
                                       RulePayload("b.com", separated_, "/x/"),
                                       workspaces_));
  ASSERT_EQ(2u, settings.rules.size());
  EXPECT_EQ("a.com", settings.rules[0].host);
  EXPECT_EQ("/x", settings.rules[1].path_prefix);

  const RoutingSettings before = settings;
  EXPECT_EQ("invalidHost",
            ApplyLinkRoutingAction(settings, "addRule",
                                   RulePayload("a.com:1", work_), workspaces_));
  EXPECT_EQ("invalidPath", ApplyLinkRoutingAction(
                               settings, "addRule",
                               RulePayload("a.com", work_, "x"), workspaces_));
  EXPECT_EQ(
      "invalidTarget",
      ApplyLinkRoutingAction(
          settings, "addRule",
          RulePayload("a.com", base::Uuid::GenerateRandomV4()), workspaces_));
  EXPECT_EQ("invalidRequest",
            ApplyLinkRoutingAction(settings, "unknownAction", {}, workspaces_));
  EXPECT_EQ(before, settings);

  base::DictValue move;
  move.Set("id", settings.rules[1].id.AsLowercaseString());
  move.Set("delta", -1);
  EXPECT_EQ("",
            ApplyLinkRoutingAction(settings, "moveRule", move, workspaces_));
  EXPECT_EQ("b.com", settings.rules[0].host);
  EXPECT_EQ("cannotMove",
            ApplyLinkRoutingAction(settings, "moveRule", move, workspaces_));

  base::DictValue remove;
  remove.Set("id", settings.rules[0].id.AsLowercaseString());
  EXPECT_EQ(
      "", ApplyLinkRoutingAction(settings, "deleteRule", remove, workspaces_));
  EXPECT_EQ("unknownRule", ApplyLinkRoutingAction(settings, "deleteRule",
                                                  remove, workspaces_));
  ASSERT_EQ(1u, settings.rules.size());

  base::DictValue enabled;
  enabled.Set("enabled", false);
  EXPECT_EQ(
      "", ApplyLinkRoutingAction(settings, "setEnabled", enabled, workspaces_));
  EXPECT_FALSE(settings.enabled);

  base::DictValue default_route;
  default_route.Set("target", work_.AsLowercaseString());
  default_route.Set("mode", "quick_window");
  EXPECT_EQ("", ApplyLinkRoutingAction(settings, "setDefault", default_route,
                                       workspaces_));
  EXPECT_EQ(work_, settings.default_route.target_workspace_id);

  EXPECT_EQ("", ApplyLinkRoutingAction(settings, "reset", {}, workspaces_));
  EXPECT_EQ(RoutingSettings(), settings);
}

TEST_F(LinkRoutingSettingsModelTest, UpdateMayKeepAnUnavailableTarget) {
  const base::Uuid gone = base::Uuid::GenerateRandomV4();
  const base::Uuid id = base::Uuid::GenerateRandomV4();
  RoutingSettings settings{
      .rules = {{.id = id, .host = "a.com", .target_workspace_id = gone}}};
  base::DictValue payload = RulePayload("a.com", gone);
  payload.Set("id", id.AsLowercaseString());
  payload.Set("enabled", false);
  EXPECT_EQ(
      "", ApplyLinkRoutingAction(settings, "updateRule", payload, workspaces_));
  EXPECT_FALSE(settings.rules[0].enabled);
  EXPECT_EQ(gone, settings.rules[0].target_workspace_id);

  // Another unavailable Workspace cannot be chosen.
  payload.Set("target", base::Uuid::GenerateRandomV4().AsLowercaseString());
  EXPECT_EQ("invalidTarget", ApplyLinkRoutingAction(settings, "updateRule",
                                                    payload, workspaces_));
}

TEST_F(LinkRoutingSettingsModelTest, ExampleShowsTargetAndWinningRule) {
  const base::Uuid gone = base::Uuid::GenerateRandomV4();
  const RoutingRule mail{.id = base::Uuid::GenerateRandomV4(),
                         .host = "example.com",
                         .include_subdomains = true,
                         .path_prefix = "/mail",
                         .target_workspace_id = work_};
  const RoutingRule stale{.id = base::Uuid::GenerateRandomV4(),
                          .host = "stale.com",
                          .target_workspace_id = gone};
  RoutingSettings settings{.rules = {mail, stale}};

  base::DictValue result = DescribeLinkRoutingExample(
      settings, workspaces_, "  mail.example.com/mail/inbox?x=1 ");
  EXPECT_EQ("routed", *result.FindString("status"));
  EXPECT_EQ(0, result.FindInt("ruleIndex"));
  EXPECT_EQ(mail.id.AsLowercaseString(), *result.FindString("ruleId"));
  EXPECT_EQ(work_.AsLowercaseString(), *result.FindString("targetId"));
  EXPECT_NE(std::string::npos, result.FindString("text")->find("Arbeit"));
  EXPECT_EQ(true, result.FindBool("allPorts"));

  // A rule without a port matches every port, and the example says so.
  result = DescribeLinkRoutingExample(settings, workspaces_,
                                      "http://mail.example.com:8443/mail");
  EXPECT_EQ(0, result.FindInt("ruleIndex"));
  EXPECT_EQ(true, result.FindBool("allPorts"));
  EXPECT_TRUE(result.FindString("text")->find("8443") == std::string::npos);

  // A query value never produces a domain match.
  result = DescribeLinkRoutingExample(settings, workspaces_,
                                      "https://evil.net/?u=example.com/mail");
  EXPECT_EQ("routed", *result.FindString("status"));
  EXPECT_EQ(-1, result.FindInt("ruleIndex"));
  EXPECT_EQ(false, result.FindBool("allPorts"));

  result =
      DescribeLinkRoutingExample(settings, workspaces_, "https://stale.com/");
  EXPECT_EQ("needsChoice", *result.FindString("status"));
  EXPECT_EQ(1, result.FindInt("ruleIndex"));

  EXPECT_EQ("empty", *DescribeLinkRoutingExample(settings, workspaces_, " ")
                          .FindString("status"));
  EXPECT_EQ("notRoutable", *DescribeLinkRoutingExample(settings, workspaces_,
                                                       "mailto:a@example.com")
                                .FindString("status"));
  EXPECT_EQ("notRoutable", *DescribeLinkRoutingExample(settings, workspaces_,
                                                       "ftp://example.com/")
                                .FindString("status"));

  settings.enabled = false;
  EXPECT_EQ("disabled", *DescribeLinkRoutingExample(settings, workspaces_,
                                                    "https://example.com/")
                             .FindString("status"));
}

TEST_F(LinkRoutingSettingsModelTest, EveryErrorHasAVisibleLabel) {
  EXPECT_TRUE(LinkRoutingErrorLabel("").empty());
  for (const char* error :
       {"invalidHost", "invalidPath", "invalidTarget", "invalidMode",
        "unknownRule", "cannotMove", "invalidRequest", "writeFailed", "managed",
        "unavailable"}) {
    EXPECT_FALSE(LinkRoutingErrorLabel(error).empty()) << error;
  }
}

}  // namespace
}  // namespace ahoi::settings
