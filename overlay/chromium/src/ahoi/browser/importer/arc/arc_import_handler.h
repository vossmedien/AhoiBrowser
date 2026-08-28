// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_HANDLER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_HANDLER_H_

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace ahoi::importer::arc {

class ArcImportHandler : public content::WebUIMessageHandler {
 public:
  explicit ArcImportHandler(Profile* profile);
  ArcImportHandler(const ArcImportHandler&) = delete;
  ArcImportHandler& operator=(const ArcImportHandler&) = delete;
  ~ArcImportHandler() override;

  void RegisterMessages() override;

 private:
  void HandleDiscover(const base::ListValue& args);
  void HandleCommit(const base::ListValue& args);
  void ResolvePreview(base::Value callback_id, ArcImportPreview preview);
  void ResolveCommit(base::Value callback_id, ArcImportCommitResult result);

  raw_ptr<Profile> profile_ = nullptr;
  base::WeakPtrFactory<ArcImportHandler> weak_factory_{this};
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_HANDLER_H_
