// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"

#include <utility>

#include "base/strings/string_util.h"

namespace ahoi {
namespace {

bool IsBoundedText(const std::string& value) {
  return value.size() <= kMaxDeveloperCssBytes && base::IsStringUTF8(value) &&
         value.find('\0') == std::string::npos;
}

DeveloperStyleCompileResult Error(DeveloperStyleCompileStatus status) {
  return {.status = status};
}

}  // namespace

LazyDeveloperStyleCompiler::LazyDeveloperStyleCompiler(ServiceFactory factory)
    : factory_(std::move(factory)) {}

LazyDeveloperStyleCompiler::~LazyDeveloperStyleCompiler() = default;

void LazyDeveloperStyleCompiler::OpenEditor() {
  editor_open_ = true;
}

void LazyDeveloperStyleCompiler::CloseEditor() {
  editor_open_ = false;
  service_.reset();
}

void LazyDeveloperStyleCompiler::Compile(
    DeveloperStyleCompileRequest request,
    DeveloperStyleCompileCallback callback) {
  if (callback.is_null()) {
    return;
  }
  if (!editor_open_) {
    std::move(callback).Run(Error(DeveloperStyleCompileStatus::kEditorClosed));
    return;
  }
  if (request.source.empty() || !IsBoundedText(request.source)) {
    std::move(callback).Run(
        Error(DeveloperStyleCompileStatus::kInvalidRequest));
    return;
  }
  if (request.language == DeveloperStyleLanguage::kCss) {
    std::move(callback).Run({.status = DeveloperStyleCompileStatus::kSucceeded,
                             .css = std::move(request.source)});
    return;
  }
  if (!service_) {
    service_ = factory_.is_null() ? nullptr : factory_.Run();
  }
  if (!service_) {
    std::move(callback).Run(
        Error(DeveloperStyleCompileStatus::kServiceUnavailable));
    return;
  }
  service_->Compile(
      std::move(request),
      base::BindOnce(
          [](DeveloperStyleCompileCallback completed,
             DeveloperStyleCompileResult result) {
            if (!result.succeeded()) {
              result.css.clear();
              std::move(completed).Run(std::move(result));
              return;
            }
            if (!IsBoundedText(result.css)) {
              std::move(completed).Run(
                  Error(DeveloperStyleCompileStatus::kOutputRejected));
              return;
            }
            std::move(completed).Run(std::move(result));
          },
          std::move(callback)));
}

}  // namespace ahoi
