// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_pin_button.h"

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ahoi {
namespace {

class NavigationPinButton final : public ToolbarButton {
  METADATA_HEADER(NavigationPinButton, ToolbarButton)

 public:
  explicit NavigationPinButton(PrefService* prefs);
  ~NavigationPinButton() override;

 private:
  void TogglePinned();
  void UpdateFromPreferences();

  const raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar registrar_;
};

NavigationPinButton::NavigationPinButton(PrefService* prefs)
    : ToolbarButton(base::BindRepeating(&NavigationPinButton::TogglePinned,
                                        base::Unretained(this))),
      prefs_(prefs) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
  registrar_.Init(prefs_);
  registrar_.Add(
      appearance::kFloatingNavigationAutoHideEnabledPref,
      base::BindRepeating(&NavigationPinButton::UpdateFromPreferences,
                          base::Unretained(this)));
  UpdateFromPreferences();
}

NavigationPinButton::~NavigationPinButton() = default;

void NavigationPinButton::TogglePinned() {
  const auto* preference = prefs_->FindPreference(
      appearance::kFloatingNavigationAutoHideEnabledPref);
  if (!preference || !preference->IsUserModifiable()) {
    return;
  }
  prefs_->SetBoolean(
      appearance::kFloatingNavigationAutoHideEnabledPref,
      !prefs_->GetBoolean(appearance::kFloatingNavigationAutoHideEnabledPref));
}

void NavigationPinButton::UpdateFromPreferences() {
  const auto* preference = prefs_->FindPreference(
      appearance::kFloatingNavigationAutoHideEnabledPref);
  const bool pinned =
      preference &&
      !prefs_->GetBoolean(appearance::kFloatingNavigationAutoHideEnabledPref);
  SetEnabled(preference && preference->IsUserModifiable());
  SetVectorIcon(pinned ? kKeepOffIcon : kKeepIcon);
  const auto label = l10n_util::GetStringUTF16(
      pinned ? IDS_AHOI_UNPIN_NAVIGATION : IDS_AHOI_PIN_NAVIGATION);
  SetTooltipText(label);
  GetViewAccessibility().SetName(label);
  GetViewAccessibility().SetCheckedState(pinned
                                             ? ax::mojom::CheckedState::kTrue
                                             : ax::mojom::CheckedState::kFalse);
}

BEGIN_METADATA(NavigationPinButton)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateNavigationPinButton(PrefService* prefs) {
  if (!prefs || !prefs->FindPreference(
                    appearance::kFloatingNavigationAutoHideEnabledPref)) {
    return nullptr;
  }
  return std::make_unique<NavigationPinButton>(prefs);
}

}  // namespace ahoi
