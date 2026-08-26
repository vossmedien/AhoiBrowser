// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_user_agent_presets.h"
#include "base/functional/callback_helpers.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

constexpr char kExampleUrl[] = "https://example.test/path?query=1";

class RecordingDataAdapter final : public BrowsingDataRemovalAdapter {
 public:
  bool Remove(const BrowsingDataClearRequest& request,
              CompletionCallback callback) override {
    last_request = request;
    std::move(callback).Run(failed_data_type_mask);
    return true;
  }

  std::optional<BrowsingDataClearRequest> last_request;
  uint32_t failed_data_type_mask = 0;
};

class RecordingSettingsAdapter final : public ContentSettingsAdapter {
 public:
  std::optional<ContentSettingValue> Get(
      const url::Origin& origin,
      ContentSettingType type) const override {
    if (!last_toggle || last_toggle->origin != origin ||
        last_toggle->type != type) {
      return std::nullopt;
    }
    return last_toggle->value;
  }

  bool Set(const url::Origin& origin,
           ContentSettingType type,
           ContentSettingValue value) override {
    last_toggle =
        ContentSettingToggle{.origin = origin, .type = type, .value = value};
    return true;
  }

  bool Reset(const url::Origin& origin, ContentSettingType type) override {
    last_reset = std::make_pair(origin, type);
    last_toggle.reset();
    return true;
  }

  std::optional<ContentSettingToggle> last_toggle;
  std::optional<std::pair<url::Origin, ContentSettingType>> last_reset;
};

TEST(DeveloperToolkitTargetTest, RejectsNullAndUnsupportedSchemes) {
  EXPECT_FALSE(IsSupportedDeveloperTarget(nullptr));
  EXPECT_FALSE(IsSupportedDeveloperTargetUrl(GURL("chrome://settings")));
  EXPECT_FALSE(IsSupportedDeveloperTargetUrl(GURL("file:///tmp/page.html")));
  EXPECT_FALSE(IsSupportedDeveloperTargetUrl(GURL("about:blank")));
  EXPECT_TRUE(IsSupportedDeveloperTargetUrl(GURL(kExampleUrl)));
}

TEST(DeveloperToolkitBrowsingDataTest, BuildsExactOriginCacheRequest) {
  const std::optional<BrowsingDataClearRequest> request =
      BuildBrowsingDataClearRequest(
          GURL(kExampleUrl),
          BrowsingDataOptionsForScope(BrowsingDataScope::kCacheOnly));

  ASSERT_TRUE(request);
  EXPECT_EQ(request->origin, url::Origin::Create(GURL("https://example.test")));
  EXPECT_EQ(request->target, BrowsingDataTarget::kCurrentSite);
  EXPECT_EQ(request->data_type_mask, ToMask(BrowsingDataType::kCache));
  const std::optional<BrowsingDataClearRequest> equivalent_request =
      BuildBrowsingDataClearRequest(
          GURL("https://example.test:443/path"),
          BrowsingDataOptionsForScope(BrowsingDataScope::kCacheOnly));
  ASSERT_TRUE(equivalent_request);
  ASSERT_TRUE(equivalent_request->origin);
  EXPECT_FALSE(equivalent_request->origin->opaque());
  const std::optional<BrowsingDataClearRequest> subdomain_request =
      BuildBrowsingDataClearRequest(
          GURL("https://sub.example.test/path"),
          BrowsingDataOptionsForScope(BrowsingDataScope::kCacheOnly));
  ASSERT_TRUE(subdomain_request);
  EXPECT_NE(request->origin, subdomain_request->origin);
}

TEST(DeveloperToolkitBrowsingDataTest, FullSiteDataHasNoHistoryOrPasswordBits) {
  const std::optional<BrowsingDataClearRequest> request =
      BuildBrowsingDataClearRequest(
          GURL(kExampleUrl),
          BrowsingDataOptionsForScope(BrowsingDataScope::kFullSiteData));

  ASSERT_TRUE(request);
  EXPECT_TRUE(request->data_type_mask & ToMask(BrowsingDataType::kCache));
  EXPECT_TRUE(request->data_type_mask & ToMask(BrowsingDataType::kCookies));
  EXPECT_TRUE(request->data_type_mask &
              ToMask(BrowsingDataType::kServiceWorkers));
  EXPECT_TRUE(request->data_type_mask &
              ToMask(BrowsingDataType::kSessionStorage));
  EXPECT_TRUE(request->data_type_mask &
              ToMask(BrowsingDataType::kCacheStorage));
  const std::optional<BrowsingDataClearRequest> http_request =
      BuildBrowsingDataClearRequest(
          GURL("http://example.test:8080/"),
          BrowsingDataOptionsForScope(BrowsingDataScope::kFullSiteData));
  ASSERT_TRUE(http_request);
  EXPECT_EQ(http_request->origin,
            url::Origin::Create(GURL("http://example.test:8080")));
}

TEST(DeveloperToolkitBrowsingDataTest,
     GlobalRequestHasNoOriginAndPreservesGranularOptions) {
  const BrowsingDataClearOptions options{
      .target = BrowsingDataTarget::kGlobal,
      .time_range = BrowsingDataTimeRange::kLast24Hours,
      .data_type_mask = ToMask(BrowsingDataType::kCache) |
                        ToMask(BrowsingDataType::kCacheStorage),
  };
  const std::optional<BrowsingDataClearRequest> request =
      BuildBrowsingDataClearRequest(GURL(), options);
  ASSERT_TRUE(request);
  EXPECT_FALSE(request->origin.has_value());
  EXPECT_EQ(request->target, BrowsingDataTarget::kGlobal);
  EXPECT_EQ(request->time_range, BrowsingDataTimeRange::kLast24Hours);
  EXPECT_EQ(request->data_type_mask, options.data_type_mask);
}

TEST(DeveloperToolkitBrowsingDataTest, RejectsEmptyOrUnknownTypeMask) {
  BrowsingDataClearOptions options;
  options.data_type_mask = 0;
  EXPECT_FALSE(BuildBrowsingDataClearRequest(GURL(kExampleUrl), options));
  options.data_type_mask = 1u << 31;
  EXPECT_FALSE(BuildBrowsingDataClearRequest(GURL(kExampleUrl), options));
}

TEST(DeveloperToolkitBrowsingDataTest,
     ControllerDoesNotCallAdapterForNullTarget) {
  auto adapter = std::make_unique<RecordingDataAdapter>();
  RecordingDataAdapter* adapter_ptr = adapter.get();
  BrowsingDataController controller(std::move(adapter));

  EXPECT_FALSE(controller.ClearCache(nullptr, base::DoNothing()));
  EXPECT_FALSE(controller.ClearSiteData(nullptr, base::DoNothing()));
  EXPECT_FALSE(adapter_ptr->last_request.has_value());
}

TEST(DeveloperToolkitContentSettingsTest, ToggleDefaultsToBlockAndAlternates) {
  EXPECT_EQ(ToggleContentSettingValue(std::nullopt),
            ContentSettingValue::kBlock);
  EXPECT_EQ(ToggleContentSettingValue(ContentSettingValue::kBlock),
            ContentSettingValue::kAllow);
  EXPECT_EQ(ToggleContentSettingValue(ContentSettingValue::kAllow),
            ContentSettingValue::kBlock);
}

TEST(DeveloperToolkitContentSettingsTest, ControllerRejectsNullTarget) {
  auto adapter = std::make_unique<RecordingSettingsAdapter>();
  RecordingSettingsAdapter* adapter_ptr = adapter.get();
  ContentSettingsController controller(std::move(adapter));

  EXPECT_FALSE(controller.Toggle(nullptr, ContentSettingType::kJavaScript));
  EXPECT_FALSE(adapter_ptr->last_toggle.has_value());
}

TEST(DeveloperToolkitDocumentActionsTest, ExposesOnlyFixedStaticScripts) {
  EXPECT_GT(kDeveloperToolkitIsolatedWorldId,
            content::ISOLATED_WORLD_ID_GLOBAL);
  EXPECT_LE(kDeveloperToolkitIsolatedWorldId, content::ISOLATED_WORLD_ID_MAX);

  constexpr DocumentAction kActions[] = {
      DocumentAction::kToggleCss,
      DocumentAction::kTogglePasswordFields,
      DocumentAction::kToggleStructureOutlines,
      DocumentAction::kToggleAltTitleLabels,
      DocumentAction::kToggleDocumentMetadata,
      DocumentAction::kClearSessionStorage,
      DocumentAction::kResetDocumentModifications,
  };

  for (DocumentAction action : kActions) {
    const DocumentActionScript script = GetDocumentActionScript(action);
    EXPECT_FALSE(script.source.empty());
    EXPECT_TRUE(IsFixedDocumentActionScript(script.source));
    // Session Storage is exposed through window.sessionStorage; the other
    // fixed document helpers use document.* directly.
    EXPECT_TRUE(script.source.find("document.") != std::string_view::npos ||
                script.source.find("window.") != std::string_view::npos);
    EXPECT_EQ(script.source.find("eval"), std::string_view::npos);
    EXPECT_EQ(script.source.find("Function"), std::string_view::npos);
  }
}

TEST(DeveloperToolkitDocumentActionsTest, PayloadsContainNoCallerInput) {
  const DocumentActionScript script =
      GetDocumentActionScript(DocumentAction::kToggleCss);
  EXPECT_FALSE(
      IsFixedDocumentActionScript(std::string(script.source) + " changed"));
  EXPECT_NE(script.source.find("__ahoi_css_disabled__"),
            std::string_view::npos);
  EXPECT_NE(script.source.find("querySelectorAll"), std::string_view::npos);

  const DocumentActionScript reset =
      GetDocumentActionScript(DocumentAction::kResetDocumentModifications);
  EXPECT_NE(reset.source.find("data-ahoi-audit-label"), std::string_view::npos);
  EXPECT_NE(reset.source.find("data-ahoi-asset-style"), std::string_view::npos);
  EXPECT_NE(reset.source.find("__ahoi_metadata_panel__"),
            std::string_view::npos);
}

TEST(DeveloperActivationStateTest, TracksAndClearsCompactChips) {
  DeveloperActivationState state;
  state.Set(DeveloperActivation::kCss, true);
  state.Set(DeveloperActivation::kCacheOff, true);
  EXPECT_TRUE(state.Has(DeveloperActivation::kCss));
  EXPECT_TRUE(state.Has(DeveloperActivation::kCacheOff));
  state.Toggle(DeveloperActivation::kCss);
  EXPECT_FALSE(state.Has(DeveloperActivation::kCss));
  state.Reset();
  EXPECT_EQ(state.mask, 0u);
}

TEST(DeveloperUserAgentPresetsTest, ResolvesAndMatchesConcretePresets) {
  constexpr std::string_view kCurrent =
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
      "AppleWebKit/537.36 Chrome/151.0.0.0 Safari/537.36";
  const std::optional<std::string> windows = ResolveDeveloperUserAgentPreset(
      DeveloperUserAgentPreset::kChromeWindows, kCurrent);
  ASSERT_TRUE(windows);
  EXPECT_NE(windows->find("Windows NT 10.0; Win64; x64"), std::string::npos);
  EXPECT_EQ(MatchDeveloperUserAgentPreset(*windows, kCurrent),
            DeveloperUserAgentPreset::kChromeWindows);
  EXPECT_EQ(MatchDeveloperUserAgentPreset(kCurrent, kCurrent),
            DeveloperUserAgentPreset::kChromeMac);
}

TEST(DeveloperUserAgentPresetsTest, KeepsDefaultAndCustomDistinct) {
  constexpr std::string_view kCurrent = "Current Browser UA";
  EXPECT_FALSE(ResolveDeveloperUserAgentPreset(
      DeveloperUserAgentPreset::kBrowserDefault, kCurrent));
  EXPECT_FALSE(ResolveDeveloperUserAgentPreset(
      DeveloperUserAgentPreset::kCustom, kCurrent));
  EXPECT_EQ(MatchDeveloperUserAgentPreset("", kCurrent),
            DeveloperUserAgentPreset::kBrowserDefault);
  EXPECT_EQ(MatchDeveloperUserAgentPreset("My test agent", kCurrent),
            DeveloperUserAgentPreset::kCustom);
}

TEST(DeveloperToolkitTest, RejectsUnsupportedTargetWithoutCallingAdapters) {
  auto data_adapter = std::make_unique<RecordingDataAdapter>();
  auto settings_adapter = std::make_unique<RecordingSettingsAdapter>();
  DeveloperToolkit toolkit(std::move(data_adapter),
                           std::move(settings_adapter));

  EXPECT_FALSE(toolkit.ClearCache(nullptr, base::DoNothing()));
  EXPECT_FALSE(toolkit.ClearSiteData(nullptr, base::DoNothing()));
  EXPECT_FALSE(toolkit.ToggleJavaScript(nullptr));
  EXPECT_FALSE(toolkit.ToggleImages(nullptr));
}

TEST(DeveloperToolkitBrowsingDataTest, CompletionClassifiesPartialAndFailure) {
  const BrowsingDataClearOptions options{
      .target = BrowsingDataTarget::kGlobal,
      .time_range = BrowsingDataTimeRange::kLastHour,
      .data_type_mask =
          ToMask(BrowsingDataType::kCache) | ToMask(BrowsingDataType::kCookies),
  };
  const BrowsingDataClearResult success{.options = options};
  EXPECT_EQ(BrowsingDataClearStatus::kSucceeded, success.status());

  const BrowsingDataClearResult partial{
      .options = options,
      .failed_data_type_mask = ToMask(BrowsingDataType::kCookies),
  };
  EXPECT_EQ(BrowsingDataClearStatus::kPartiallySucceeded, partial.status());
  EXPECT_EQ(ToMask(BrowsingDataType::kCache),
            partial.completed_data_type_mask());

  const BrowsingDataClearResult failure{
      .options = options,
      .failed_data_type_mask = options.data_type_mask,
  };
  EXPECT_EQ(BrowsingDataClearStatus::kFailed, failure.status());
}

}  // namespace
}  // namespace ahoi
