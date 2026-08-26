// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_H_

#include <memory>
#include <string>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/functional/callback.h"

namespace ahoi {

enum class DeveloperStyleCompileStatus {
  kSucceeded,
  kEditorClosed,
  kInvalidRequest,
  kServiceUnavailable,
  kSyntaxError,
  kUnsupportedSyntax,
  kOutputRejected,
};

struct DeveloperStyleCompileRequest {
  DeveloperStyleLanguage language = DeveloperStyleLanguage::kCss;
  std::string source;

  bool operator==(const DeveloperStyleCompileRequest&) const = default;
};

struct DeveloperStyleCompileResult {
  DeveloperStyleCompileStatus status =
      DeveloperStyleCompileStatus::kServiceUnavailable;
  std::string css;
  uint32_t error_line = 0;
  uint32_t error_column = 0;

  bool succeeded() const {
    return status == DeveloperStyleCompileStatus::kSucceeded;
  }
  bool operator==(const DeveloperStyleCompileResult&) const = default;
};

using DeveloperStyleCompileCallback =
    base::OnceCallback<void(DeveloperStyleCompileResult)>;

// Implementations for LESS/SASS must live in a sandboxed Utility process.
// The interface deliberately has no filesystem/network access and transports
// one bounded source string to one bounded CSS result.
class DeveloperStyleCompilerService {
 public:
  virtual ~DeveloperStyleCompilerService() = default;
  virtual void Compile(DeveloperStyleCompileRequest request,
                       DeveloperStyleCompileCallback callback) = 0;
};

// Browser-side lifetime gate. No service exists before the native editor is
// opened, CSS does not launch one, and closing the last editor drops the
// Utility remote/compiler memory immediately.
class LazyDeveloperStyleCompiler {
 public:
  using ServiceFactory =
      base::RepeatingCallback<std::unique_ptr<DeveloperStyleCompilerService>()>;

  explicit LazyDeveloperStyleCompiler(ServiceFactory factory);
  LazyDeveloperStyleCompiler(const LazyDeveloperStyleCompiler&) = delete;
  LazyDeveloperStyleCompiler& operator=(const LazyDeveloperStyleCompiler&) =
      delete;
  ~LazyDeveloperStyleCompiler();

  void OpenEditor();
  void CloseEditor();
  void Compile(DeveloperStyleCompileRequest request,
               DeveloperStyleCompileCallback callback);

  bool editor_open_for_testing() const { return editor_open_; }
  bool service_loaded_for_testing() const { return service_ != nullptr; }

 private:
  const ServiceFactory factory_;
  std::unique_ptr<DeveloperStyleCompilerService> service_;
  bool editor_open_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_H_
