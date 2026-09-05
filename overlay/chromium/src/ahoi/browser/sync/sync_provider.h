// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_PROVIDER_H_
#define AHOI_BROWSER_SYNC_SYNC_PROVIDER_H_

#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "base/functional/callback.h"

namespace ahoi::sync {

// Transport-neutral seam for CloudKit, a self-hosted relay, or an iOS-owned
// bridge. Providers never own local state: uploads are acknowledged by
// mutation id and downloads are addressed by the store's opaque change token.
class SyncProvider {
 public:
  using UploadCallback =
      base::OnceCallback<void(bool success,
                              std::vector<std::string> acknowledged_ids,
                              std::string error)>;
  using DownloadCallback = base::OnceCallback<
      void(bool success, ProviderBatch batch, std::string error)>;

  virtual ~SyncProvider() = default;

  virtual void Upload(std::vector<SyncChange> changes,
                      UploadCallback callback) = 0;
  virtual void Download(std::string change_token,
                        DownloadCallback callback) = 0;
  // Local category consent, additional to the caller's global sync gate.
  // Providers must retain blocked remote bookmarks without
  // decrypting/delivering them and recheck consent before delayed uploads.
  // Never infer it from sync.
  virtual void SetBookmarkSyncEnabled(bool enabled);
  virtual bool IsBookmarkConsentRevoked();
  virtual bool IsAccountTransitionPending();
  virtual bool IsZoneRecoveryPending();
  virtual bool ConfirmAccountTransition(bool allow_local_upload);
  virtual bool ConfirmZoneRecovery();
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_PROVIDER_H_
