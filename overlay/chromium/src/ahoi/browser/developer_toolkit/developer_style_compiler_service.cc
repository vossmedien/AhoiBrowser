// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_compiler_service.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_style_preprocessor.h"

namespace ahoi {
namespace {

developer_toolkit::mojom::StyleCompileStatus ToMojoStatus(
    DeveloperStyleCompileStatus status) {
  using MojoStatus = developer_toolkit::mojom::StyleCompileStatus;
  switch (status) {
    case DeveloperStyleCompileStatus::kSucceeded:
      return MojoStatus::kSucceeded;
    case DeveloperStyleCompileStatus::kUnsupportedSyntax:
      return MojoStatus::kUnsupportedSyntax;
    case DeveloperStyleCompileStatus::kOutputRejected:
      return MojoStatus::kOutputRejected;
    case DeveloperStyleCompileStatus::kSyntaxError:
      return MojoStatus::kSyntaxError;
    case DeveloperStyleCompileStatus::kEditorClosed:
    case DeveloperStyleCompileStatus::kInvalidRequest:
    case DeveloperStyleCompileStatus::kServiceUnavailable:
      return MojoStatus::kInputRejected;
  }
}

}  // namespace

DeveloperStyleCompilerServiceImpl::DeveloperStyleCompilerServiceImpl(
    mojo::PendingReceiver<developer_toolkit::mojom::DeveloperStyleCompiler>
        receiver)
    : receiver_(this, std::move(receiver)) {}

DeveloperStyleCompilerServiceImpl::~DeveloperStyleCompilerServiceImpl() =
    default;

void DeveloperStyleCompilerServiceImpl::Compile(
    developer_toolkit::mojom::StyleLanguage language,
    const std::string& source,
    CompileCallback callback) {
  const DeveloperStyleLanguage core_language =
      language == developer_toolkit::mojom::StyleLanguage::kLess
          ? DeveloperStyleLanguage::kLess
          : DeveloperStyleLanguage::kSass;
  DeveloperStyleCompileResult result = CompileDeveloperStyleSource(
      {.language = core_language, .source = source});
  std::move(callback).Run(developer_toolkit::mojom::StyleCompileResult::New(
      ToMojoStatus(result.status), std::move(result.css), result.error_line,
      result.error_column));
}

}  // namespace ahoi
