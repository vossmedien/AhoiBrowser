// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_INTERNAL_H_
#define AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_INTERNAL_H_

#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace sql {
class Statement;
}  // namespace sql

namespace ahoi::tab_tree::internal {

bool DecodeWorkspace(sql::Statement& statement, Workspace* workspace);
bool DecodeNode(sql::Statement& statement, TreeNode* node);
void BindNodeForInsert(sql::Statement& statement, const TreeNode& node);
void BindWorkspaceForInsert(sql::Statement& statement,
                            const Workspace& workspace);

}  // namespace ahoi::tab_tree::internal

#endif  // AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_INTERNAL_H_
