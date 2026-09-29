// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_INSTALL_COORDINATOR_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_INSTALL_COORDINATOR_H_

#include <memory>

#include "ahoi/browser/extensions/ubo_catalog.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/types/expected.h"

class Profile;

namespace content {
class WebContents;
}

namespace ahoi::extensions {

using UboInstallResult = base::expected<void, UboVerificationError>;
using UboInstallCallback = base::OnceCallback<void(UboInstallResult)>;

// Owns one asynchronous install transaction. Destruction is terminal: it
// cancels pending callbacks, revokes transient authorization, and schedules
// deletion of the downloaded package.
class UboInstallOperation {
 public:
  virtual ~UboInstallOperation() = default;
  virtual void Cancel() = 0;
};

using UboInstallOperationPtr = std::unique_ptr<UboInstallOperation>;

// Backend for an explicit user-initiated install/update. The caller must first
// display the verified entry's distribution, version, source, fixed ID, hash,
// and GPL license. This function re-verifies the package off-thread, uses
// Chromium's normal permission prompt, and commits the MV2 authorization only
// after successful installation. The returned operation owns the package and
// must outlive the Chromium installer callback.
[[nodiscard]] UboInstallOperationPtr InstallUboPackageFromVerifiedCatalog(
    Profile* profile,
    content::WebContents* web_contents,
    UboCatalogEntry entry,
    base::FilePath package_path,
    UboInstallCallback callback);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_INSTALL_COORDINATOR_H_
