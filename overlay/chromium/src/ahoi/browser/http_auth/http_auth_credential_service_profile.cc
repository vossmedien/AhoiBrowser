// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"

namespace ahoi {

HttpAuthCredentialService::HttpAuthCredentialService(
    Profile* profile,
    scoped_refptr<password_manager::PasswordStoreInterface> password_store)
    : HttpAuthCredentialService(profile ? profile->GetPrefs() : nullptr,
                                std::move(password_store),
                                profile && profile->IsOffTheRecord()) {}

}  // namespace ahoi
