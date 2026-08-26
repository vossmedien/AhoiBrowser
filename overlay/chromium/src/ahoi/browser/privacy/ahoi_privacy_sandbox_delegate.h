// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_AHOI_PRIVACY_SANDBOX_DELEGATE_H_
#define AHOI_BROWSER_PRIVACY_AHOI_PRIVACY_SANDBOX_DELEGATE_H_

#include "components/privacy_sandbox/privacy_sandbox_settings.h"

namespace ahoi::privacy {

// Product delegate for a browser that does not expose advertising/profiling
// Privacy Sandbox APIs. This remains restricted even if upstream prefs,
// notices, account capabilities or Finch state are inconsistent.
class AhoiPrivacySandboxDelegate final
    : public privacy_sandbox::PrivacySandboxSettings::Delegate {
 public:
  explicit AhoiPrivacySandboxDelegate(bool is_incognito_profile);
  ~AhoiPrivacySandboxDelegate() override;

  bool IsPrivacySandboxRestricted() const override;
  bool IsPrivacySandboxCurrentlyUnrestricted() const override;
  bool IsIncognitoProfile() const override;
  bool HasAppropriateTopicsConsent() const override;
  bool IsSubjectToM1NoticeRestricted() const override;
  bool IsRestrictedNoticeEnabled() const override;

 private:
  const bool is_incognito_profile_;
};

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_AHOI_PRIVACY_SANDBOX_DELEGATE_H_
