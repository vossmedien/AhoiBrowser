// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge_unittest_support.h"

#include <string>
#include <utility>

#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::test_support {

tab_tree::Workspace MakeWorkspace(std::u16string name, std::string sort_key) {
  const base::Time now = base::Time::Now();
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = std::move(name);
  workspace.sort_key = std::move(sort_key);
  workspace.created_at = now;
  workspace.modified_at = now;
  return workspace;
}

tab_tree::TreeNode MakeSavedPage(const base::Uuid& workspace_id,
                                 const GURL& url) {
  const base::Time now = base::Time::Now();
  tab_tree::TreeNode node;
  node.id = base::Uuid::GenerateRandomV4();
  node.workspace_id = workspace_id;
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.title = u"Production";
  node.url = url;
  node.sort_key = "a";
  node.created_at = now;
  node.modified_at = now;
  return node;
}

SessionBridgeTest::SessionBridgeTest() = default;

SessionBridgeTest::~SessionBridgeTest() = default;

void SessionBridgeTest::SetUp() {
  BrowserWithTestWindowTest::SetUp();
  workspace_service_ = WorkspaceServiceFactory::GetForProfile(profile());
  bridge_ = SessionBridgeFactory::GetForProfile(profile());
  ASSERT_TRUE(workspace_service_);
  ASSERT_TRUE(bridge_);
  ASSERT_TRUE(bridge_->is_operational());
  base::RunLoop ready;
  bridge_->RunWhenReadyForTesting(ready.QuitClosure());
  ready.Run();
  ASSERT_TRUE(bridge_->is_ready());
}

void SessionBridgeTest::FlushPersistence() {
  base::RunLoop flushed;
  bridge_->FlushPersistenceForTesting(flushed.QuitClosure());
  flushed.Run();
}

}  // namespace ahoi::test_support
