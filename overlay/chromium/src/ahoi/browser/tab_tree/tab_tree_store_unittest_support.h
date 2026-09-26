// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_UNITTEST_SUPPORT_H_

#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

// The TabTreeStore fixture shared by the store unit tests: a fresh SQLite
// store in a temporary directory plus Workspace and node builders (split
// from tab_tree_store_unittest.cc, source line budget).
namespace ahoi::tab_tree::test_support {

class AhoiTabTreeStoreTest : public testing::Test {
 public:
  AhoiTabTreeStoreTest();
  ~AhoiTabTreeStoreTest() override;

  void SetUp() override;

 protected:
  bool ReopenStore();

  Workspace NewWorkspace(std::u16string name, std::string sort_key);

  TreeNode NewFolder(const Workspace& workspace,
                     std::optional<base::Uuid> parent_id,
                     std::u16string title,
                     std::string sort_key);

  TreeNode NewSavedPage(const Workspace& workspace,
                        std::optional<base::Uuid> parent_id,
                        std::u16string title,
                        const GURL& url,
                        std::string sort_key);

  base::ScopedTempDir temp_dir_;
  base::FilePath database_path_;
  std::unique_ptr<TabTreeStore> store_;
};

}  // namespace ahoi::tab_tree::test_support

#endif  // AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_UNITTEST_SUPPORT_H_
