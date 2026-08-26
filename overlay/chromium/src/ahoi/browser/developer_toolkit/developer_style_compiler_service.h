// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_H_

#include "ahoi/browser/developer_toolkit/public/mojom/developer_style_compiler.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace ahoi {

// Utility-process endpoint. It owns no OS handles beyond its primordial Mojo
// pipe and delegates only to the deterministic bounded compiler core.
class DeveloperStyleCompilerServiceImpl final
    : public developer_toolkit::mojom::DeveloperStyleCompiler {
 public:
  explicit DeveloperStyleCompilerServiceImpl(
      mojo::PendingReceiver<developer_toolkit::mojom::DeveloperStyleCompiler>
          receiver);
  DeveloperStyleCompilerServiceImpl(
      const DeveloperStyleCompilerServiceImpl&) = delete;
  DeveloperStyleCompilerServiceImpl& operator=(
      const DeveloperStyleCompilerServiceImpl&) = delete;
  ~DeveloperStyleCompilerServiceImpl() override;

  void Compile(developer_toolkit::mojom::StyleLanguage language,
               const std::string& source,
               CompileCallback callback) override;

 private:
  mojo::Receiver<developer_toolkit::mojom::DeveloperStyleCompiler> receiver_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_H_
