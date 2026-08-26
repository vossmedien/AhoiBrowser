// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BROWSING_DATA_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BROWSING_DATA_H_

#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/functional/callback.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace ahoi {

// This adapter is the only seam that needs to know how the host Profile and
// BrowsingDataRemover are wired. It is deliberately on-demand: construction
// and idle operation do not schedule observers, timers, or cleanup work.
class BrowsingDataRemovalAdapter {
 public:
  using CompletionCallback = base::OnceCallback<void(uint32_t)>;

  virtual ~BrowsingDataRemovalAdapter() = default;

  // A true return guarantees exactly one completion callback. The callback's
  // mask uses BrowsingDataType bits and is always a subset of the request.
  virtual bool Remove(const BrowsingDataClearRequest& request,
                      CompletionCallback callback) = 0;
};

std::optional<BrowsingDataClearRequest> BuildBrowsingDataClearRequest(
    const GURL& active_url,
    BrowsingDataClearOptions options);

BrowsingDataClearOptions BrowsingDataOptionsForScope(BrowsingDataScope scope);

// Calls the adapter only after the active WebContents target has been
// validated and the exact committed origin has been captured.
class BrowsingDataController {
 public:
  explicit BrowsingDataController(
      std::unique_ptr<BrowsingDataRemovalAdapter> adapter);
  BrowsingDataController(const BrowsingDataController&) = delete;
  BrowsingDataController& operator=(const BrowsingDataController&) = delete;
  ~BrowsingDataController();

  bool ClearCache(const content::WebContents* web_contents,
                  BrowsingDataClearCallback callback);
  bool ClearSiteData(const content::WebContents* web_contents,
                     BrowsingDataClearCallback callback);
  bool ClearData(const content::WebContents* web_contents,
                 BrowsingDataClearOptions options,
                 BrowsingDataClearCallback callback);

 private:
  std::unique_ptr<BrowsingDataRemovalAdapter> adapter_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BROWSING_DATA_H_
