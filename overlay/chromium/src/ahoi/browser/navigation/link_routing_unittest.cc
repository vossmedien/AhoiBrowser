// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing.h"

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::navigation {
namespace {

RoutingRule Rule(std::string host,
                 bool include_subdomains = false,
                 std::string path_prefix = std::string(),
                 LinkOpenMode mode = LinkOpenMode::kNormalTab) {
  return {.id = base::Uuid::GenerateRandomV4(),
          .host = std::move(host),
          .include_subdomains = include_subdomains,
          .path_prefix = std::move(path_prefix),
          .target_workspace_id = base::Uuid::GenerateRandomV4(),
          .mode = mode};
}

bool Matches(const RoutingRule& rule, std::string_view url) {
  return RuleMatchesUrl(rule, GURL(url));
}

TEST(LinkRoutingTest, OnlyHttpAndHttpsAreRoutable) {
  EXPECT_TRUE(IsRoutableUrl(GURL("https://example.com/")));
  EXPECT_TRUE(IsRoutableUrl(GURL("http://example.com:8080/a")));
  EXPECT_FALSE(IsRoutableUrl(GURL("file:///tmp/a.html")));
  EXPECT_FALSE(IsRoutableUrl(GURL("mailto:a@example.com")));
  EXPECT_FALSE(IsRoutableUrl(GURL("chrome://settings")));
  EXPECT_FALSE(IsRoutableUrl(GURL("ftp://example.com/")));
  EXPECT_FALSE(IsRoutableUrl(GURL("not a url")));

  const RoutingSettings settings{.rules = {Rule("example.com")}};
  EXPECT_EQ(RouteDisposition::kNotRoutable,
            ResolveRoute(settings, GURL("ftp://example.com/")).disposition);
}

TEST(LinkRoutingTest, NormalizesRuleHosts) {
  EXPECT_EQ("example.com", NormalizeRuleHost("Example.COM."));
  EXPECT_EQ("xn--bcher-kva.de", NormalizeRuleHost("bücher.de"));
  EXPECT_EQ("192.168.0.1", NormalizeRuleHost("192.168.0.1"));
  EXPECT_EQ("[::1]", NormalizeRuleHost("::1"));
  EXPECT_EQ("[::1]", NormalizeRuleHost("[::1]"));
  EXPECT_EQ("localhost", NormalizeRuleHost("LOCALHOST"));
  EXPECT_FALSE(NormalizeRuleHost(""));
  EXPECT_FALSE(NormalizeRuleHost("."));
  EXPECT_FALSE(NormalizeRuleHost("example.com:8080"));
  EXPECT_FALSE(NormalizeRuleHost("https://example.com"));
  EXPECT_FALSE(NormalizeRuleHost("example.com/path"));
  EXPECT_FALSE(NormalizeRuleHost("user@example.com"));
  EXPECT_FALSE(NormalizeRuleHost("example.com?q=1"));
  EXPECT_FALSE(NormalizeRuleHost("exa mple.com"));
}

TEST(LinkRoutingTest, NormalizesPathPrefixes) {
  EXPECT_EQ("", NormalizeRulePathPrefix(""));
  EXPECT_EQ("", NormalizeRulePathPrefix("/"));
  EXPECT_EQ("/a", NormalizeRulePathPrefix("/a/"));
  EXPECT_EQ("/a/b", NormalizeRulePathPrefix("/a/./b"));
  EXPECT_EQ("/a%20b", NormalizeRulePathPrefix("/a b"));
  EXPECT_FALSE(NormalizeRulePathPrefix("a"));
  EXPECT_FALSE(NormalizeRulePathPrefix("/a?b"));
  EXPECT_FALSE(NormalizeRulePathPrefix("/a#b"));
}

TEST(LinkRoutingTest, ExactHostDoesNotMatchLookalikes) {
  const RoutingRule rule = Rule("example.com");
  EXPECT_TRUE(Matches(rule, "https://example.com/"));
  EXPECT_TRUE(Matches(rule, "https://EXAMPLE.com./x"));
  EXPECT_TRUE(Matches(rule, "http://example.com:8443/x"));
  EXPECT_FALSE(Matches(rule, "https://www.example.com/"));
  EXPECT_FALSE(Matches(rule, "https://notexample.com/"));
  EXPECT_FALSE(Matches(rule, "https://example.com.evil.net/"));
  EXPECT_FALSE(Matches(rule, "https://example.co/"));
}

TEST(LinkRoutingTest, SubdomainsOnlyWhenIncluded) {
  const RoutingRule rule = Rule("example.com", /*include_subdomains=*/true);
  EXPECT_TRUE(Matches(rule, "https://example.com/"));
  EXPECT_TRUE(Matches(rule, "https://www.example.com/"));
  EXPECT_TRUE(Matches(rule, "https://a.b.example.com/"));
  EXPECT_FALSE(Matches(rule, "https://notexample.com/"));
  EXPECT_FALSE(Matches(rule, "https://example.com.evil.net/"));
  EXPECT_FALSE(Matches(rule, "https://www.example.com.evil.net/"));
}

TEST(LinkRoutingTest, QueryAndFragmentNeverMatch) {
  const RoutingRule rule = Rule("example.com", /*include_subdomains=*/true);
  EXPECT_FALSE(Matches(rule, "https://evil.net/?next=https://example.com/"));
  EXPECT_FALSE(Matches(rule, "https://evil.net/#example.com"));
  EXPECT_FALSE(Matches(rule, "https://evil.net/example.com"));
  EXPECT_FALSE(Matches(rule, "https://example.com@evil.net/"));

  const RoutingRule path_rule = Rule("example.com", false, "/docs");
  EXPECT_FALSE(Matches(path_rule, "https://example.com/?p=/docs"));
  EXPECT_FALSE(Matches(path_rule, "https://example.com/#/docs"));
  EXPECT_TRUE(Matches(path_rule, "https://example.com/docs?x=1#y"));
}

TEST(LinkRoutingTest, PathPrefixMatchesAtSegmentBoundaries) {
  const RoutingRule rule = Rule("example.com", false, "/a");
  EXPECT_TRUE(Matches(rule, "https://example.com/a"));
  EXPECT_TRUE(Matches(rule, "https://example.com/a/"));
  EXPECT_TRUE(Matches(rule, "https://example.com/a/b"));
  EXPECT_FALSE(Matches(rule, "https://example.com/ab"));
  EXPECT_FALSE(Matches(rule, "https://example.com/"));
  EXPECT_FALSE(Matches(rule, "https://example.com/A"));
}

TEST(LinkRoutingTest, IpAndLocalhostRulesAreExactOnly) {
  const RoutingRule ip = Rule("127.0.0.1", /*include_subdomains=*/true);
  EXPECT_TRUE(Matches(ip, "http://127.0.0.1:3000/"));
  EXPECT_FALSE(Matches(ip, "http://1.127.0.0.1/"));
  EXPECT_FALSE(Matches(ip, "http://a.127.0.0.1/"));

  const RoutingRule v6 = Rule("[::1]", /*include_subdomains=*/true);
  EXPECT_TRUE(Matches(v6, "http://[::1]:8080/"));

  const RoutingRule local = Rule("localhost", /*include_subdomains=*/true);
  EXPECT_TRUE(Matches(local, "http://localhost:5173/"));
  EXPECT_FALSE(Matches(local, "http://app.localhost/"));
}

TEST(LinkRoutingTest, FirstEnabledRuleWinsInOrder) {
  RoutingRule disabled = Rule("example.com", true);
  disabled.enabled = false;
  const RoutingRule docs =
      Rule("example.com", true, "/docs", LinkOpenMode::kQuickWindow);
  const RoutingRule any = Rule("example.com", true);
  const RoutingSettings settings{.rules = {disabled, docs, any}};

  const RouteResult docs_route =
      ResolveRoute(settings, GURL("https://www.example.com/docs/x"));
  EXPECT_EQ(RouteDisposition::kRouted, docs_route.disposition);
  EXPECT_EQ(1u, docs_route.rule_index);
  EXPECT_EQ(docs.target_workspace_id, docs_route.target_workspace_id);
  EXPECT_EQ(LinkOpenMode::kQuickWindow, docs_route.mode);

  const RouteResult any_route =
      ResolveRoute(settings, GURL("https://example.com/blog"));
  EXPECT_EQ(2u, any_route.rule_index);
  EXPECT_EQ(any.target_workspace_id, any_route.target_workspace_id);
}

TEST(LinkRoutingTest, DefaultRouteAppliesWithoutMatch) {
  RoutingSettings settings{.rules = {Rule("example.com")}};
  RouteResult route = ResolveRoute(settings, GURL("https://other.org/"));
  EXPECT_EQ(RouteDisposition::kRouted, route.disposition);
  EXPECT_FALSE(route.rule_index);
  EXPECT_FALSE(route.target_workspace_id);  // Last active Workspace.
  EXPECT_EQ(LinkOpenMode::kNormalTab, route.mode);

  const base::Uuid inbox = base::Uuid::GenerateRandomV4();
  settings.default_route = {.target_workspace_id = inbox,
                            .mode = LinkOpenMode::kQuickWindow};
  route = ResolveRoute(settings, GURL("https://other.org/"), {inbox});
  EXPECT_EQ(RouteDisposition::kRouted, route.disposition);
  EXPECT_EQ(inbox, route.target_workspace_id);
  EXPECT_EQ(LinkOpenMode::kQuickWindow, route.mode);
}

TEST(LinkRoutingTest, UnavailableExplicitTargetNeedsChoice) {
  const RoutingRule rule = Rule("example.com");
  const base::Uuid other = base::Uuid::GenerateRandomV4();
  RoutingSettings settings{.rules = {rule}};

  RouteResult route =
      ResolveRoute(settings, GURL("https://example.com/"), {other});
  EXPECT_EQ(RouteDisposition::kNeedsTargetChoice, route.disposition);
  EXPECT_EQ(0u, route.rule_index);
  EXPECT_EQ(rule.target_workspace_id, route.unavailable_target);
  EXPECT_FALSE(route.target_workspace_id);  // Falls back to last active.

  // A stale default target also asks; "last active" never does.
  settings.default_route.target_workspace_id = base::Uuid::GenerateRandomV4();
  route = ResolveRoute(settings, GURL("https://other.org/"), {other});
  EXPECT_EQ(RouteDisposition::kNeedsTargetChoice, route.disposition);
  settings.default_route.target_workspace_id.reset();
  route = ResolveRoute(settings, GURL("https://other.org/"), {});
  EXPECT_EQ(RouteDisposition::kRouted, route.disposition);
}

TEST(LinkRoutingTest, NormalizeAndDedupKeepsFirstRoutableInOrder) {
  const std::vector<GURL> urls = {
      GURL("https://b.example/"), GURL("mailto:x@y.z"),
      GURL("https://a.example/"), GURL("HTTPS://B.EXAMPLE/"),
      GURL("https://a.example/#f")};
  EXPECT_EQ(
      (std::vector<GURL>{GURL("https://b.example/"), GURL("https://a.example/"),
                         GURL("https://a.example/#f")}),
      NormalizeAndDedupUrls(urls));
}

TEST(LinkRoutingTest, SettingsRoundTrip) {
  RoutingRule quick =
      Rule("example.com", true, "/a", LinkOpenMode::kQuickWindow);
  quick.enabled = false;
  const RoutingSettings settings{
      .enabled = false,
      .rules = {Rule("[::1]"), quick},
      .default_route = {.target_workspace_id = base::Uuid::GenerateRandomV4(),
                        .mode = LinkOpenMode::kQuickWindow}};
  EXPECT_EQ(settings, ParseRoutingSettings(SerializeRoutingSettings(settings)));
}

TEST(LinkRoutingTest, StrictParseFallsBackToDefaults) {
  EXPECT_EQ(RoutingSettings(), ParseRoutingSettings(base::DictValue()));

  const RoutingSettings configured{.enabled = false,
                                   .rules = {Rule("example.com")}};
  base::DictValue future = SerializeRoutingSettings(configured);
  future.Set("version", 2);
  EXPECT_EQ(RoutingSettings(), ParseRoutingSettings(future));

  base::DictValue bad_default = SerializeRoutingSettings(configured);
  bad_default.FindDict("default")->Set("target", "nope");
  EXPECT_EQ(RoutingSettings(), ParseRoutingSettings(bad_default));

  base::DictValue bad_rules = SerializeRoutingSettings(configured);
  bad_rules.Set("rules", "nope");
  EXPECT_EQ(RoutingSettings(), ParseRoutingSettings(bad_rules));
}

TEST(LinkRoutingTest, MalformedOrDuplicateRulesAreDropped) {
  const RoutingRule good = Rule("example.com");
  RoutingRule duplicate = Rule("other.org");
  duplicate.id = good.id;
  base::DictValue dict =
      SerializeRoutingSettings({.rules = {good, duplicate, Rule("third.net")}});
  base::ListValue* rules = dict.FindList("rules");
  ASSERT_TRUE(rules);
  (*rules)[2].GetDict().Set("host", "Third.NET");  // Not canonical.
  rules->Append("not a dict");
  base::DictValue bad_mode = (*rules)[0].GetDict().Clone();
  bad_mode.Set("id", base::Uuid::GenerateRandomV4().AsLowercaseString());
  bad_mode.Set("mode", "incognito");
  rules->Append(std::move(bad_mode));

  const RoutingSettings parsed = ParseRoutingSettings(dict);
  ASSERT_EQ(1u, parsed.rules.size());
  EXPECT_EQ(good, parsed.rules[0]);
}

TEST(LinkRoutingTest, PrefStorage) {
  TestingPrefServiceSimple prefs;
  EXPECT_EQ(RoutingSettings(), ReadRoutingSettings(prefs));
  EXPECT_FALSE(WriteRoutingSettings(&prefs, RoutingSettings()));

  prefs.registry()->RegisterDictionaryPref(kLinkRoutingPref);
  EXPECT_EQ(RoutingSettings(), ReadRoutingSettings(prefs));

  const RoutingSettings settings{.rules = {Rule("example.com", true, "/a")}};
  ASSERT_TRUE(WriteRoutingSettings(&prefs, settings));
  EXPECT_EQ(settings, ReadRoutingSettings(prefs));

  // Non-canonical input is refused rather than stored and later dropped.
  EXPECT_FALSE(WriteRoutingSettings(&prefs, {.rules = {Rule("Example.com")}}));
  EXPECT_EQ(settings, ReadRoutingSettings(prefs));

  prefs.SetManagedPref(kLinkRoutingPref, base::Value(base::DictValue()));
  EXPECT_FALSE(WriteRoutingSettings(&prefs, RoutingSettings()));
}

}  // namespace
}  // namespace ahoi::navigation
