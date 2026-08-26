// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_profile_editor_view.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_text_codec.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"
#include "ahoi/browser/developer_toolkit/developer_style_compiler_service_client.h"
#include "ahoi/browser/developer_toolkit/developer_user_agent_presets.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/developer_toolkit/developer_asset_policy_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_response_header_advanced_mode_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {
namespace {

constexpr int kSingleLineHeight = visual_style::kDeveloperToolkitRowHeight;
constexpr int kCodeAreaHeight = 92;
constexpr int kHeadersAreaHeight = 76;

constexpr std::array<DeveloperUserAgentPreset, 7> kUserAgentPresets = {
    DeveloperUserAgentPreset::kBrowserDefault,
    DeveloperUserAgentPreset::kChromeMac,
    DeveloperUserAgentPreset::kSafariMac,
    DeveloperUserAgentPreset::kChromeWindows,
    DeveloperUserAgentPreset::kFirefoxMac,
    DeveloperUserAgentPreset::kMobileSafari,
    DeveloperUserAgentPreset::kCustom,
};

constexpr std::array<DeveloperStyleLanguage, 3> kStyleLanguages = {
    DeveloperStyleLanguage::kCss,
    DeveloperStyleLanguage::kLess,
    DeveloperStyleLanguage::kSass,
};

int StyleLanguageStringId(DeveloperStyleLanguage language) {
  switch (language) {
    case DeveloperStyleLanguage::kCss:
      return IDS_AHOI_DEVELOPER_PROFILE_STYLE_CSS;
    case DeveloperStyleLanguage::kLess:
      return IDS_AHOI_DEVELOPER_PROFILE_STYLE_LESS;
    case DeveloperStyleLanguage::kSass:
      return IDS_AHOI_DEVELOPER_PROFILE_STYLE_SASS;
  }
  return IDS_AHOI_DEVELOPER_PROFILE_STYLE_CSS;
}

size_t StyleLanguageIndex(DeveloperStyleLanguage language) {
  for (size_t index = 0; index < std::size(kStyleLanguages); ++index) {
    if (kStyleLanguages[index] == language) {
      return index;
    }
  }
  return 0;
}

const DeveloperAsset* FindFirstAsset(const DeveloperProfile& profile,
                                     DeveloperAssetKind kind) {
  const auto found = std::find_if(
      profile.assets.begin(), profile.assets.end(),
      [kind](const DeveloperAsset& asset) { return asset.kind == kind; });
  return found == profile.assets.end() ? nullptr : &*found;
}

int UserAgentPresetStringId(DeveloperUserAgentPreset preset) {
  switch (preset) {
    case DeveloperUserAgentPreset::kBrowserDefault:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_DEFAULT;
    case DeveloperUserAgentPreset::kChromeMac:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_CHROME_MAC;
    case DeveloperUserAgentPreset::kSafariMac:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_SAFARI_MAC;
    case DeveloperUserAgentPreset::kChromeWindows:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_CHROME_WINDOWS;
    case DeveloperUserAgentPreset::kFirefoxMac:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_FIREFOX_MAC;
    case DeveloperUserAgentPreset::kMobileSafari:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_MOBILE_SAFARI;
    case DeveloperUserAgentPreset::kCustom:
      return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_CUSTOM;
  }
  return IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_CUSTOM;
}

size_t UserAgentPresetIndex(DeveloperUserAgentPreset preset) {
  for (size_t index = 0; index < std::size(kUserAgentPresets); ++index) {
    if (kUserAgentPresets.at(index) == preset) {
      return index;
    }
  }
  return 0;
}

std::unique_ptr<views::Label> CreateMutedLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  label->SetMultiLine(true);
  return label;
}

}  // namespace

DeveloperProfileEditorView::DeveloperProfileEditorView(
    std::u16string origin_label,
    DeveloperProfile initial_profile,
    bool has_stored_profile,
    content::WebContents* source_web_contents,
    DeveloperSecretStoreFactory secret_store_factory,
    SaveCallback save_callback,
    RemoveCallback remove_callback,
    base::RepeatingClosure close_callback,
    PrefService* prefs)
    : content::WebContentsObserver(source_web_contents),
      save_callback_(std::move(save_callback)),
      remove_callback_(std::move(remove_callback)),
      close_callback_(std::move(close_callback)),
      origin_scope_(base::UTF16ToUTF8(origin_label)),
      current_browser_user_agent_(embedder_support::GetUserAgent()) {
  const GURL source_url = source_web_contents
                              ? source_web_contents->GetLastCommittedURL()
                              : GURL(origin_scope_);
  DeveloperProfileTabHelper* const tab_helper =
      DeveloperProfileTabHelper::FromWebContents(source_web_contents);
  const std::string current_tab_token =
      tab_helper ? tab_helper->tab_token() : std::string();
  style_compiler_ = std::make_unique<LazyDeveloperStyleCompiler>(
      base::BindRepeating(&CreateSandboxedDeveloperStyleCompilerService));
  style_compiler_->OpenEditor();
  DeveloperAsset initial_style{
      .id = style_asset_id_,
      .name = "Style",
      .kind = DeveloperAssetKind::kStyle,
      .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                .value = origin_scope_},
  };
  DeveloperAsset initial_script{
      .id = script_asset_id_,
      .name = "JavaScript",
      .kind = DeveloperAssetKind::kJavaScript,
      .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                .value = origin_scope_},
  };
  if (const DeveloperAsset* asset =
          FindFirstAsset(initial_profile, DeveloperAssetKind::kStyle)) {
    initial_style = *asset;
    style_asset_id_ = asset->id;
  }
  if (const DeveloperAsset* asset =
          FindFirstAsset(initial_profile, DeveloperAssetKind::kJavaScript)) {
    initial_script = *asset;
    script_asset_id_ = asset->id;
  }
  for (const DeveloperAsset& asset : initial_profile.assets) {
    if (asset.id != style_asset_id_ && asset.id != script_asset_id_) {
      preserved_assets_.push_back(asset);
    }
  }
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      visual_style::kDeveloperToolkitControlSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  AddChildView(CreateMutedLabel(std::move(origin_label)));

  const std::u16string name_label =
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_NAME);
  AddChildView(CreateMutedLabel(name_label));
  name_field_ = AddTextControl(std::make_unique<views::Textfield>(),
                               kSingleLineHeight, name_label, name_label);
  name_field_->SetText(base::UTF8ToUTF16(initial_profile.name));

  css_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_CSS)));
  css_enabled_->SetTextSubpixelRenderingEnabled(false);
  css_enabled_->SetChecked(initial_style.enabled);
  std::vector<ui::SimpleComboboxModel::Item> language_items;
  for (DeveloperStyleLanguage language : kStyleLanguages) {
    language_items.emplace_back(
        l10n_util::GetStringUTF16(StyleLanguageStringId(language)));
  }
  auto language_combobox = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(language_items)));
  language_combobox->SetSelectedIndex(
      StyleLanguageIndex(initial_style.style_language));
  language_combobox->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_STYLE_LANGUAGE));
  language_combobox->SetPreferredSize(gfx::Size(0, kSingleLineHeight));
  style_language_ = AddChildView(std::move(language_combobox));
  style_policy_ = AddChildView(std::make_unique<DeveloperAssetPolicyView>(
      initial_style, source_url, current_tab_token));
  css_source_ = AddTextControl(
      std::make_unique<views::Textarea>(), kCodeAreaHeight,
      std::u16string(css_enabled_->GetText()),
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_CSS_PLACEHOLDER));
  css_source_->SetText(base::UTF8ToUTF16(initial_style.source));

  javascript_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_JAVASCRIPT)));
  javascript_enabled_->SetTextSubpixelRenderingEnabled(false);
  javascript_enabled_->SetChecked(initial_script.enabled);
  javascript_source_ =
      AddTextControl(std::make_unique<views::Textarea>(), kCodeAreaHeight,
                     std::u16string(javascript_enabled_->GetText()),
                     l10n_util::GetStringUTF16(
                         IDS_AHOI_DEVELOPER_PROFILE_JAVASCRIPT_PLACEHOLDER));
  javascript_source_->SetText(base::UTF8ToUTF16(initial_script.source));
  javascript_main_world_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_MAIN_WORLD),
      base::BindRepeating(&DeveloperProfileEditorView::OnMainWorldChanged,
                          base::Unretained(this))));
  javascript_main_world_->SetChecked(initial_script.javascript_world ==
                                     DeveloperJavaScriptWorld::kMain);
  main_world_warning_accepted_ =
      AddChildView(std::make_unique<views::Checkbox>(l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_PROFILE_MAIN_WORLD_WARNING)));
  main_world_warning_accepted_->SetChecked(
      initial_script.main_world_warning_accepted);
  javascript_policy_ = AddChildView(std::make_unique<DeveloperAssetPolicyView>(
      initial_script, source_url, current_tab_token));
  OnMainWorldChanged();

  user_agent_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT),
      base::BindRepeating(
          &DeveloperProfileEditorView::OnUserAgentEnabledChanged,
          base::Unretained(this))));
  user_agent_enabled_->SetTextSubpixelRenderingEnabled(false);
  user_agent_enabled_->SetChecked(initial_profile.user_agent_enabled);

  AddChildView(CreateMutedLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_PRESET)));
  std::vector<ui::SimpleComboboxModel::Item> preset_items;
  preset_items.reserve(std::size(kUserAgentPresets));
  for (DeveloperUserAgentPreset preset : kUserAgentPresets) {
    preset_items.emplace_back(
        l10n_util::GetStringUTF16(UserAgentPresetStringId(preset)));
  }
  auto preset_combobox = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(preset_items)));
  const DeveloperUserAgentPreset initial_preset =
      initial_profile.user_agent_enabled
          ? MatchDeveloperUserAgentPreset(initial_profile.user_agent,
                                          current_browser_user_agent_)
          : DeveloperUserAgentPreset::kBrowserDefault;
  preset_combobox->SetSelectedIndex(UserAgentPresetIndex(initial_preset));
  preset_combobox->SetCallback(
      base::BindRepeating(&DeveloperProfileEditorView::OnUserAgentPresetChanged,
                          base::Unretained(this)));
  preset_combobox->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_PRESET));
  preset_combobox->SetBackgroundColorId(visual_style::kRaisedSurface);
  preset_combobox->SetForegroundColorId(visual_style::kText);
  preset_combobox->SetBorderColorId(visual_style::kDivider);
  preset_combobox->SetPreferredSize(gfx::Size(0, kSingleLineHeight));
  user_agent_preset_ = AddChildView(std::move(preset_combobox));

  user_agent_ =
      AddTextControl(std::make_unique<views::Textfield>(), kSingleLineHeight,
                     std::u16string(user_agent_enabled_->GetText()),
                     l10n_util::GetStringUTF16(
                         IDS_AHOI_DEVELOPER_PROFILE_USER_AGENT_PLACEHOLDER));
  user_agent_->SetText(base::UTF8ToUTF16(initial_profile.user_agent));
  OnUserAgentPresetChanged();

  const bool is_off_the_record =
      !source_web_contents || !source_web_contents->GetBrowserContext() ||
      source_web_contents->GetBrowserContext()->IsOffTheRecord();
  auto header_secret_editor = std::make_unique<DeveloperHeaderSecretEditorView>(
      is_off_the_record, initial_profile.header_rules,
      initial_profile.response_header_rules, std::move(secret_store_factory),
      base::BindRepeating(&DeveloperProfileEditorView::ShowStatus,
                          weak_ptr_factory_.GetWeakPtr()));

  header_rules_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_HEADERS)));
  header_rules_enabled_->SetTextSubpixelRenderingEnabled(false);
  header_rules_enabled_->SetChecked(initial_profile.header_rules_enabled);
  header_rules_sync_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS)));
  header_rules_sync_enabled_->SetChecked(
      initial_profile.header_rules_sync_enabled);
  header_rules_ =
      AddTextControl(std::make_unique<views::Textarea>(), kHeadersAreaHeight,
                     std::u16string(header_rules_enabled_->GetText()),
                     l10n_util::GetStringUTF16(
                         IDS_AHOI_DEVELOPER_PROFILE_HEADERS_PLACEHOLDER));
  header_rules_->SetText(base::UTF8ToUTF16(
      FormatDeveloperHeaderRules(header_secret_editor->PlainRulesForEditor(
          DeveloperHeaderSecretDirection::kRequest))));

  response_header_rules_enabled_ =
      AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(
              IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS),
          base::BindRepeating(
              &DeveloperProfileEditorView::OnResponseHeaderRulesEnabledChanged,
              base::Unretained(this))));
  response_header_rules_enabled_->SetTextSubpixelRenderingEnabled(false);
  response_header_rules_enabled_->SetChecked(
      initial_profile.response_header_rules_enabled);
  response_header_rules_sync_enabled_ =
      AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS)));
  response_header_rules_sync_enabled_->SetChecked(
      initial_profile.response_header_rules_sync_enabled);
  response_header_rules_ = AddTextControl(
      std::make_unique<views::Textarea>(), kHeadersAreaHeight,
      std::u16string(response_header_rules_enabled_->GetText()),
      l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS_PLACEHOLDER));
  response_header_rules_->SetText(base::UTF8ToUTF16(
      FormatDeveloperHeaderRules(header_secret_editor->PlainRulesForEditor(
          DeveloperHeaderSecretDirection::kResponse))));
  AddChildView(CreateMutedLabel(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS_HELP)));
  response_header_advanced_mode_ =
      AddChildView(std::make_unique<DeveloperResponseHeaderAdvancedModeView>(
          initial_profile.response_header_rules_enabled,
          initial_profile.response_header_advanced_mode_acknowledged));

  header_secret_editor_ = AddChildView(std::move(header_secret_editor));

  cache_disabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_CACHE_OFF)));
  cache_disabled_->SetChecked(initial_profile.cache_disabled);

  if (has_stored_profile) {
    auto remove_button = std::make_unique<DeveloperToolkitButton>(
        base::BindRepeating(&DeveloperProfileEditorView::OnRemove,
                            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_REMOVE));
    remove_button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    remove_button->SetFocusBehavior(FocusBehavior::ALWAYS);
    remove_button->SetPreferredSize(
        gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
    AddChildView(std::move(remove_button));
  }

  status_label_ = AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetVisible(false);

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs,
          base::BindRepeating(&DeveloperProfileEditorView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
}

DeveloperProfileEditorView::~DeveloperProfileEditorView() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  style_compiler_->CloseEditor();
}

views::View* DeveloperProfileEditorView::initially_focused_view() const {
  return name_field_;
}

void DeveloperProfileEditorView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

bool DeveloperProfileEditorView::Save() {
  if (compile_in_flight_) {
    return false;
  }
  std::optional<DeveloperProfile> profile = BuildProfileForSave();
  if (!profile) {
    return false;
  }
  if (!CanPersistObservedTarget() ||
      !header_secret_editor_->BeginProfileCommit()) {
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return false;
  }
  DeveloperAsset* const style_asset =
      profile->assets.size() >= 2 ? &profile->assets[profile->assets.size() - 2]
                                  : nullptr;
  if (!style_asset ||
      style_asset->style_language == DeveloperStyleLanguage::kCss ||
      (!style_asset->enabled && style_asset->source.empty())) {
    if (!save_callback_.Run(*profile)) {
      header_secret_editor_->CompleteProfileCommit(false);
      ShowStatus(l10n_util::GetStringUTF16(
                     IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
                 true);
      return false;
    }
    header_secret_editor_->CompleteProfileCommit(true);
    return true;
  }
  compile_in_flight_ = true;
  style_compiler_->Compile(
      {.language = style_asset->style_language, .source = style_asset->source},
      base::BindOnce(&DeveloperProfileEditorView::OnStyleCompiled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(*profile)));
  return false;
}

std::optional<DeveloperProfile>
DeveloperProfileEditorView::BuildProfileForSave() {
  const size_t style_index =
      std::min(style_language_->GetSelectedIndex().value_or(0),
               std::size(kStyleLanguages) - 1);
  if (javascript_main_world_->GetChecked() &&
      !main_world_warning_accepted_->GetChecked()) {
    ShowStatus(l10n_util::GetStringUTF16(
                   IDS_AHOI_DEVELOPER_PROFILE_MAIN_WORLD_REQUIRED),
               true);
    return std::nullopt;
  }
  DeveloperHeaderTextParseResult parsed =
      ParseDeveloperHeaderRules(base::UTF16ToUTF8(header_rules_->GetText()));
  if (!parsed.succeeded()) {
    ShowStatus(
        l10n_util::GetStringFUTF16(IDS_AHOI_DEVELOPER_PROFILE_HEADERS_ERROR,
                                   base::NumberToString16(parsed.error_line)),
        true);
    return std::nullopt;
  }
  DeveloperHeaderTextParseResult parsed_response = ParseDeveloperHeaderRules(
      base::UTF16ToUTF8(response_header_rules_->GetText()));
  if (!parsed_response.succeeded()) {
    ShowStatus(l10n_util::GetStringFUTF16(
                   IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS_ERROR,
                   base::NumberToString16(parsed_response.error_line)),
               true);
    return std::nullopt;
  }

  DeveloperProfile profile;
  profile.name = base::UTF16ToUTF8(name_field_->GetText());
  profile.assets = preserved_assets_;
  DeveloperAsset style_asset{
      .id = style_asset_id_,
      .name = "Style",
      .kind = DeveloperAssetKind::kStyle,
      .style_language = kStyleLanguages[style_index],
      .enabled = css_enabled_->GetChecked(),
      .source = base::UTF16ToUTF8(css_source_->GetText()),
  };
  if (!style_policy_->ApplyTo(&style_asset)) {
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return std::nullopt;
  }
  profile.assets.push_back(std::move(style_asset));
  DeveloperAsset script_asset{
      .id = script_asset_id_,
      .name = "JavaScript",
      .kind = DeveloperAssetKind::kJavaScript,
      .enabled = javascript_enabled_->GetChecked(),
      .source = base::UTF16ToUTF8(javascript_source_->GetText()),
      .javascript_world = javascript_main_world_->GetChecked()
                              ? DeveloperJavaScriptWorld::kMain
                              : DeveloperJavaScriptWorld::kIsolated,
      .main_world_warning_accepted = main_world_warning_accepted_->GetChecked(),
  };
  if (!javascript_policy_->ApplyTo(&script_asset)) {
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return std::nullopt;
  }
  profile.assets.push_back(std::move(script_asset));
  profile.user_agent_enabled = user_agent_enabled_->GetChecked();
  profile.user_agent = base::UTF16ToUTF8(user_agent_->GetText());
  profile.header_rules_enabled = header_rules_enabled_->GetChecked();
  profile.header_rules_sync_enabled = header_rules_sync_enabled_->GetChecked();
  profile.header_rules = std::move(parsed.rules);
  profile.response_header_rules_enabled =
      response_header_rules_enabled_->GetChecked();
  profile.response_header_rules_sync_enabled =
      response_header_rules_sync_enabled_->GetChecked();
  profile.response_header_advanced_mode_acknowledged =
      response_header_advanced_mode_->acknowledged();
  profile.response_header_rules = std::move(parsed_response.rules);
  profile.cache_disabled = cache_disabled_->GetChecked();
  if (!header_secret_editor_->ApplyToProfile(&profile)) {
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return std::nullopt;
  }
  if (HasActiveAdvancedDeveloperResponseHeaderRules(profile) &&
      !profile.response_header_advanced_mode_acknowledged) {
    ShowStatus(
        l10n_util::GetStringUTF16(
            IDS_AHOI_DEVELOPER_PROFILE_ADVANCED_RESPONSE_HEADERS_REQUIRED),
        true);
    return std::nullopt;
  }
  return profile;
}

void DeveloperProfileEditorView::OnStyleCompiled(
    DeveloperProfile profile,
    DeveloperStyleCompileResult result) {
  compile_in_flight_ = false;
  if (!result.succeeded()) {
    header_secret_editor_->CompleteProfileCommit(false);
    if (result.status == DeveloperStyleCompileStatus::kSyntaxError ||
        result.status == DeveloperStyleCompileStatus::kUnsupportedSyntax) {
      std::u16string text = l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR);
      if (result.error_line != 0) {
        text.append(u" [");
        text.append(base::NumberToString16(result.error_line));
        text.push_back(u':');
        text.append(base::NumberToString16(result.error_column));
        text.push_back(u']');
      }
      ShowStatus(std::move(text), true);
      return;
    }
    ShowStatus(l10n_util::GetStringUTF16(
                   IDS_AHOI_DEVELOPER_PROFILE_COMPILER_UNAVAILABLE),
               true);
    return;
  }
  DeveloperAsset* const style_asset =
      profile.assets.size() >= 2 ? &profile.assets[profile.assets.size() - 2]
                                 : nullptr;
  if (!style_asset || result.css.empty()) {
    header_secret_editor_->CompleteProfileCommit(false);
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return;
  }
  style_asset->compiled_css = std::move(result.css);
  style_asset->compiled_style_version = kDeveloperStyleCompilerVersion;
  if (!CanPersistObservedTarget() || !header_secret_editor_->valid() ||
      !save_callback_.Run(profile)) {
    header_secret_editor_->CompleteProfileCommit(false);
    ShowStatus(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_VALIDATION_ERROR),
        true);
    return;
  }
  header_secret_editor_->CompleteProfileCommit(true);
  close_callback_.Run();
}

void DeveloperProfileEditorView::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle && navigation_handle->IsInPrimaryMainFrame()) {
    if (header_secret_editor_) {
      header_secret_editor_->OnPrimaryNavigationStarted();
    }
  }
}

void DeveloperProfileEditorView::WebContentsDestroyed() {
  if (header_secret_editor_) {
    header_secret_editor_->OnPrimaryNavigationStarted();
  }
  Observe(nullptr);
}

void DeveloperProfileEditorView::OnMainWorldChanged() {
  const bool main_world = javascript_main_world_->GetChecked();
  main_world_warning_accepted_->SetEnabled(main_world);
  if (!main_world) {
    main_world_warning_accepted_->SetChecked(false);
  }
}

void DeveloperProfileEditorView::OnResponseHeaderRulesEnabledChanged() {
  response_header_advanced_mode_->SetResponseHeadersEnabled(
      response_header_rules_enabled_->GetChecked());
}

views::Textfield* DeveloperProfileEditorView::AddTextControl(
    std::unique_ptr<views::Textfield> field,
    int height,
    std::u16string accessible_name,
    std::u16string placeholder) {
  auto shell = std::make_unique<views::View>();
  shell->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kRaisedSurface, visual_style::kControlCornerRadius));
  shell->SetBorder(views::CreateRoundedRectBorder(
      1, visual_style::kControlCornerRadius, visual_style::kDivider));
  auto* shell_layout =
      shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 12)));
  shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  shell->SetPreferredSize(gfx::Size(0, height));

  field->SetPlaceholderText(placeholder);
  field->SetAccessibleName(accessible_name);
  field->SetBorder(nullptr);
  field->SetBackgroundColor(visual_style::kRaisedSurface);
  field->SetTextColorId(visual_style::kText);
  field->SetPlaceholderTextColorId(visual_style::kMutedText);
  field->RemoveHoverEffect();
  views::Textfield* const result = shell->AddChildView(std::move(field));
  shell_layout->SetFlexForView(result, 1);
  AddChildView(std::move(shell));
  return result;
}

void DeveloperProfileEditorView::OnRemove() {
  if (remove_callback_.Run()) {
    close_callback_.Run();
    return;
  }
  ShowStatus(l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_REMOVE_ERROR),
             true);
}

void DeveloperProfileEditorView::OnUserAgentEnabledChanged() {
  if (!user_agent_enabled_->GetChecked()) {
    user_agent_preset_->SetSelectedIndex(
        UserAgentPresetIndex(DeveloperUserAgentPreset::kBrowserDefault));
    OnUserAgentPresetChanged();
    return;
  }
  const DeveloperUserAgentPreset next =
      user_agent_->GetText().empty() ? DeveloperUserAgentPreset::kChromeMac
                                     : DeveloperUserAgentPreset::kCustom;
  user_agent_preset_->SetSelectedIndex(UserAgentPresetIndex(next));
  OnUserAgentPresetChanged();
}

void DeveloperProfileEditorView::OnUserAgentPresetChanged() {
  const size_t index =
      std::min(user_agent_preset_->GetSelectedIndex().value_or(0),
               std::size(kUserAgentPresets) - 1);
  const DeveloperUserAgentPreset preset = kUserAgentPresets.at(index);
  const std::optional<std::string> resolved =
      ResolveDeveloperUserAgentPreset(preset, current_browser_user_agent_);
  if (preset == DeveloperUserAgentPreset::kBrowserDefault) {
    user_agent_enabled_->SetChecked(false);
    user_agent_->SetReadOnly(true);
    return;
  }
  user_agent_enabled_->SetChecked(true);
  if (resolved) {
    user_agent_->SetText(base::UTF8ToUTF16(*resolved));
    user_agent_->SetReadOnly(true);
    return;
  }
  user_agent_->SetReadOnly(false);
}

void DeveloperProfileEditorView::ShowStatus(std::u16string text,
                                            bool is_error) {
  status_label_->SetText(std::move(text));
  status_label_->SetEnabledColor(is_error ? ui::kColorAlertHighSeverity
                                          : visual_style::kMutedText);
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperProfileEditorView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  const appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(
          appearance::SurfaceRole::kDeveloperTools, policy);
  views::ClientView* client_view =
      GetWidget() ? GetWidget()->client_view() : nullptr;
  if (!client_view) {
    appearance::ApplySurfaceAppearance(this, surface);
    return;
  }
  appearance::ClearSurfaceBackgroundAppearance(this);
  appearance::ApplySurfaceBackgroundAppearance(client_view, surface);
}

bool DeveloperProfileEditorView::CanPersistObservedTarget() const {
  if (!web_contents() || !web_contents()->GetBrowserContext() ||
      web_contents()->GetBrowserContext()->IsOffTheRecord()) {
    return false;
  }
  const url::Origin current =
      url::Origin::Create(web_contents()->GetLastCommittedURL());
  return !current.opaque() && current.Serialize() == origin_scope_;
}

}  // namespace ahoi
