// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_EDITOR_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_EDITOR_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace views {
class Checkbox;
class Combobox;
class Label;
class Textfield;
}  // namespace views

class PrefService;

namespace ahoi {

class LazyDeveloperStyleCompiler;
struct DeveloperStyleCompileResult;
class DeveloperAssetPolicyView;
class DeveloperHeaderSecretEditorView;
class DeveloperResponseHeaderAdvancedModeView;

namespace appearance {
class AppearanceRuntimeSignalSource;
struct GlassPolicy;
}  // namespace appearance

class DeveloperProfileEditorView final : public views::View,
                                         public content::WebContentsObserver {
 public:
  using SaveCallback = base::RepeatingCallback<bool(const DeveloperProfile&)>;
  using RemoveCallback = base::RepeatingCallback<bool()>;

  DeveloperProfileEditorView(std::u16string origin_label,
                             DeveloperProfile initial_profile,
                             bool has_stored_profile,
                             content::WebContents* source_web_contents,
                             DeveloperSecretStoreFactory secret_store_factory,
                             SaveCallback save_callback,
                             RemoveCallback remove_callback,
                             base::RepeatingClosure close_callback,
                             PrefService* prefs = nullptr);
  DeveloperProfileEditorView(const DeveloperProfileEditorView&) = delete;
  DeveloperProfileEditorView& operator=(const DeveloperProfileEditorView&) =
      delete;
  ~DeveloperProfileEditorView() override;

  bool Save();
  views::View* initially_focused_view() const;
  void ReapplyAppearance();

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;

 private:
  std::optional<DeveloperProfile> BuildProfileForSave();
  void OnStyleCompiled(DeveloperProfile profile,
                       DeveloperStyleCompileResult result);
  views::Textfield* AddTextControl(std::unique_ptr<views::Textfield> field,
                                   int height,
                                   std::u16string accessible_name,
                                   std::u16string placeholder);
  void OnRemove();
  void OnUserAgentEnabledChanged();
  void OnUserAgentPresetChanged();
  void OnMainWorldChanged();
  void OnResponseHeaderRulesEnabledChanged();
  void ShowStatus(std::u16string text, bool is_error);
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);
  bool CanPersistObservedTarget() const;

  const SaveCallback save_callback_;
  const RemoveCallback remove_callback_;
  const base::RepeatingClosure close_callback_;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Checkbox> css_enabled_ = nullptr;
  raw_ptr<views::Combobox> style_language_ = nullptr;
  raw_ptr<DeveloperAssetPolicyView> style_policy_ = nullptr;
  raw_ptr<views::Textfield> css_source_ = nullptr;
  raw_ptr<views::Checkbox> javascript_enabled_ = nullptr;
  raw_ptr<views::Checkbox> javascript_main_world_ = nullptr;
  raw_ptr<views::Checkbox> main_world_warning_accepted_ = nullptr;
  raw_ptr<DeveloperAssetPolicyView> javascript_policy_ = nullptr;
  raw_ptr<views::Textfield> javascript_source_ = nullptr;
  raw_ptr<views::Checkbox> user_agent_enabled_ = nullptr;
  raw_ptr<views::Combobox> user_agent_preset_ = nullptr;
  raw_ptr<views::Textfield> user_agent_ = nullptr;
  raw_ptr<views::Checkbox> header_rules_enabled_ = nullptr;
  raw_ptr<views::Checkbox> header_rules_sync_enabled_ = nullptr;
  raw_ptr<views::Textfield> header_rules_ = nullptr;
  raw_ptr<views::Checkbox> response_header_rules_enabled_ = nullptr;
  raw_ptr<views::Checkbox> response_header_rules_sync_enabled_ = nullptr;
  raw_ptr<views::Textfield> response_header_rules_ = nullptr;
  raw_ptr<DeveloperResponseHeaderAdvancedModeView>
      response_header_advanced_mode_ = nullptr;
  raw_ptr<DeveloperHeaderSecretEditorView> header_secret_editor_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Checkbox> cache_disabled_ = nullptr;
  std::string origin_scope_;
  std::string style_asset_id_ = "style-default";
  std::string script_asset_id_ = "script-default";
  std::vector<DeveloperAsset> preserved_assets_;
  std::string current_browser_user_agent_;
  std::unique_ptr<LazyDeveloperStyleCompiler> style_compiler_;
  bool compile_in_flight_ = false;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<DeveloperProfileEditorView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_EDITOR_VIEW_H_
