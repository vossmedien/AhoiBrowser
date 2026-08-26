// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mojo/core/embedder/embedder.h"
#include "ui/views/views_test_suite.h"

int main(int argc, char** argv) {
  mojo::core::Init();
  return views::ViewsTestSuite(argc, argv).RunTests();
}
