// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_cache_status_view.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class DeveloperCacheStatusViewTest : public views::ViewsTestBase {};

TEST_F(DeveloperCacheStatusViewTest, ExposesAnUnambiguousAsyncState) {
  auto view =
      std::make_unique<DeveloperCacheStatusView>(u"https://example.com");
  EXPECT_EQ(DeveloperCacheStatusView::State::kClearing,
            view->state_for_testing());
  view->SetState(DeveloperCacheStatusView::State::kSucceeded);
  EXPECT_EQ(DeveloperCacheStatusView::State::kSucceeded,
            view->state_for_testing());
  EXPECT_TRUE(view->status_label_for_testing()->GetVisible());
  view->SetState(DeveloperCacheStatusView::State::kFailed);
  EXPECT_EQ(DeveloperCacheStatusView::State::kFailed,
            view->state_for_testing());
}

}  // namespace
}  // namespace ahoi
