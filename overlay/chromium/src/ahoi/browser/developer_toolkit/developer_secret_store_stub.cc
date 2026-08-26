// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_secret_store.h"

namespace ahoi {

std::unique_ptr<DeveloperSecretStore> CreatePlatformDeveloperSecretStore() {
  return nullptr;
}

}  // namespace ahoi
