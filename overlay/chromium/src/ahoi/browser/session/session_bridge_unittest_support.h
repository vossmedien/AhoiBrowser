// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_BRIDGE_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_SESSION_SESSION_BRIDGE_UNITTEST_SUPPORT_H_

#include <string>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/memory/raw_ptr.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "url/gurl.h"

// Tree builders and the SessionBridge fixture shared by the session bridge
// unit tests (split from session_bridge_unittest.cc, source line budget).
namespace ahoi::test_support {

tab_tree::Workspace MakeWorkspace(std::u16string name, std::string sort_key);

tab_tree::TreeNode MakeSavedPage(const base::Uuid& workspace_id,
                                 const GURL& url);

class SessionBridgeTest : public BrowserWithTestWindowTest {
 public:
  SessionBridgeTest();
  ~SessionBridgeTest() override;

  void SetUp() override;

 protected:
  void FlushPersistence();

  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  raw_ptr<SessionBridge> bridge_ = nullptr;
};

}  // namespace ahoi::test_support

#endif  // AHOI_BROWSER_SESSION_SESSION_BRIDGE_UNITTEST_SUPPORT_H_
