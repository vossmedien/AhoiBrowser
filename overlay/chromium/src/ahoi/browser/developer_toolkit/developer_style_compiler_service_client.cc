// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_compiler_service_client.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/public/mojom/developer_style_compiler.mojom.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "content/public/browser/service_process_host.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace ahoi {
namespace {

class MojoDeveloperStyleCompilerService final
    : public DeveloperStyleCompilerService {
 public:
  MojoDeveloperStyleCompilerService() = default;
  MojoDeveloperStyleCompilerService(const MojoDeveloperStyleCompilerService&) =
      delete;
  MojoDeveloperStyleCompilerService& operator=(
      const MojoDeveloperStyleCompilerService&) = delete;
  ~MojoDeveloperStyleCompilerService() override {
    remote_.reset();
    CompletePending({.status = DeveloperStyleCompileStatus::kEditorClosed});
  }

  void Compile(DeveloperStyleCompileRequest request,
               DeveloperStyleCompileCallback callback) override {
    if (request.language == DeveloperStyleLanguage::kCss ||
        !pending_callback_.is_null()) {
      std::move(callback).Run(
          {.status = DeveloperStyleCompileStatus::kInvalidRequest});
      return;
    }
    EnsureConnected();
    if (!remote_.is_bound()) {
      std::move(callback).Run(
          {.status = DeveloperStyleCompileStatus::kServiceUnavailable});
      return;
    }
    pending_callback_ = std::move(callback);
    const developer_toolkit::mojom::StyleLanguage language =
        request.language == DeveloperStyleLanguage::kLess
            ? developer_toolkit::mojom::StyleLanguage::kLess
            : developer_toolkit::mojom::StyleLanguage::kSass;
    remote_->Compile(
        language, request.source,
        base::BindOnce(&MojoDeveloperStyleCompilerService::OnCompiled,
                       weak_factory_.GetWeakPtr()));
  }

 private:
  void EnsureConnected() {
    if (remote_.is_bound()) {
      return;
    }
    remote_ = content::ServiceProcessHost::Launch<
        developer_toolkit::mojom::DeveloperStyleCompiler>(
        content::ServiceProcessHost::Options()
            .WithDisplayName(u"Ahoi Developer Style Compiler")
            .Pass());
    if (!remote_.is_bound()) {
      return;
    }
    remote_.reset_on_idle_timeout(base::Seconds(30));
    remote_.set_disconnect_handler(
        base::BindOnce(&MojoDeveloperStyleCompilerService::OnDisconnected,
                       weak_factory_.GetWeakPtr()));
  }

  void OnCompiled(developer_toolkit::mojom::StyleCompileResultPtr result) {
    if (!result) {
      CompletePending(
          {.status = DeveloperStyleCompileStatus::kServiceUnavailable});
      return;
    }
    DeveloperStyleCompileStatus status =
        DeveloperStyleCompileStatus::kServiceUnavailable;
    switch (result->status) {
      case developer_toolkit::mojom::StyleCompileStatus::kSucceeded:
        status = DeveloperStyleCompileStatus::kSucceeded;
        break;
      case developer_toolkit::mojom::StyleCompileStatus::kSyntaxError:
        status = DeveloperStyleCompileStatus::kSyntaxError;
        break;
      case developer_toolkit::mojom::StyleCompileStatus::kUnsupportedSyntax:
        status = DeveloperStyleCompileStatus::kUnsupportedSyntax;
        break;
      case developer_toolkit::mojom::StyleCompileStatus::kInputRejected:
        status = DeveloperStyleCompileStatus::kInvalidRequest;
        break;
      case developer_toolkit::mojom::StyleCompileStatus::kOutputRejected:
        status = DeveloperStyleCompileStatus::kOutputRejected;
        break;
    }
    CompletePending({.status = status,
                     .css = std::move(result->css),
                     .error_line = result->error_line,
                     .error_column = result->error_column});
  }

  void OnDisconnected() {
    remote_.reset();
    CompletePending(
        {.status = DeveloperStyleCompileStatus::kServiceUnavailable});
  }

  void CompletePending(DeveloperStyleCompileResult result) {
    if (!pending_callback_.is_null()) {
      std::move(pending_callback_).Run(std::move(result));
    }
  }

  mojo::Remote<developer_toolkit::mojom::DeveloperStyleCompiler> remote_;
  DeveloperStyleCompileCallback pending_callback_;
  base::WeakPtrFactory<MojoDeveloperStyleCompilerService> weak_factory_{this};
};

}  // namespace

std::unique_ptr<DeveloperStyleCompilerService>
CreateSandboxedDeveloperStyleCompilerService() {
  return std::make_unique<MojoDeveloperStyleCompilerService>();
}

}  // namespace ahoi
