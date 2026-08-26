// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/ahoi_privacy_sandbox_delegate.h"

namespace ahoi::privacy {

AhoiPrivacySandboxDelegate::AhoiPrivacySandboxDelegate(
    bool is_incognito_profile)
    : is_incognito_profile_(is_incognito_profile) {}

AhoiPrivacySandboxDelegate::~AhoiPrivacySandboxDelegate() = default;

bool AhoiPrivacySandboxDelegate::IsPrivacySandboxRestricted() const {
  return true;
}

bool AhoiPrivacySandboxDelegate::IsPrivacySandboxCurrentlyUnrestricted() const {
  return false;
}

bool AhoiPrivacySandboxDelegate::IsIncognitoProfile() const {
  return is_incognito_profile_;
}

bool AhoiPrivacySandboxDelegate::HasAppropriateTopicsConsent() const {
  return false;
}

bool AhoiPrivacySandboxDelegate::IsSubjectToM1NoticeRestricted() const {
  return false;
}

bool AhoiPrivacySandboxDelegate::IsRestrictedNoticeEnabled() const {
  // PrivacySandboxSettingsImpl may let Ad Measurement ignore restriction when
  // this returns true. Ahoi therefore fails closed and does not use that
  // partial-enable notice mode.
  return false;
}

}  // namespace ahoi::privacy
