// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_CACHE_STATUS_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_CACHE_STATUS_VIEW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

class PrefService;

namespace ahoi {

namespace appearance {
class AppearanceRuntimeSignalSource;
struct GlassPolicy;
}  // namespace appearance

class DeveloperCacheStatusView final : public views::View {
 public:
  enum class State {
    kClearing,
    kSucceeded,
    kFailed,
  };

  DeveloperCacheStatusView(std::u16string site_label,
                           PrefService* prefs = nullptr);
  DeveloperCacheStatusView(const DeveloperCacheStatusView&) = delete;
  DeveloperCacheStatusView& operator=(const DeveloperCacheStatusView&) = delete;
  ~DeveloperCacheStatusView() override;

  void SetState(State state);
  State state_for_testing() const { return state_; }
  views::Label* status_label_for_testing() const { return status_label_; }
  void ReapplyAppearance();

 private:
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);

  State state_ = State::kClearing;
  raw_ptr<views::Label> status_label_ = nullptr;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<DeveloperCacheStatusView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_CACHE_STATUS_VIEW_H_
