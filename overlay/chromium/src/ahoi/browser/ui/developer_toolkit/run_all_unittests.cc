// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "mojo/core/embedder/embedder.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/views/views_test_suite.h"

class DeveloperToolkitViewsTestSuite final : public views::ViewsTestSuite {
 public:
  using views::ViewsTestSuite::ViewsTestSuite;

 protected:
  void Initialize() override {
    views::ViewsTestSuite::Initialize();
    base::FilePath executable_dir;
    CHECK(base::PathService::Get(base::DIR_EXE, &executable_dir));
    ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        executable_dir.AppendASCII("gen/chrome/generated_resources_en-US.pak"),
        ui::kScaleFactorNone);
  }
};

int main(int argc, char** argv) {
  mojo::core::Init();
  return DeveloperToolkitViewsTestSuite(argc, argv).RunTests();
}
