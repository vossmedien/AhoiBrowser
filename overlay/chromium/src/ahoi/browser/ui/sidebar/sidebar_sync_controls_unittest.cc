// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_sync_controls.h"

#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

class SidebarSyncControlsTest : public views::ViewsTestBase {};

TEST_F(SidebarSyncControlsTest, StatusLivesOnlyInExpandedSyncSettings) {
  std::unique_ptr<views::View> controls = CreateSidebarSyncControlsView(
      /*service=*/nullptr, /*filter_devices=*/{}, base::DoNothing());

  ui::AXNodeData accessibility;
  controls->GetViewAccessibility().GetAccessibleNodeData(&accessibility);
  EXPECT_EQ(ax::mojom::Role::kGroup, accessibility.role);
  EXPECT_FALSE(
      accessibility.GetString16Attribute(ax::mojom::StringAttribute::kName)
          .empty());

  EXPECT_FALSE(SidebarSyncSettingsExpandedForTesting(controls.get()));
  EXPECT_FALSE(SidebarSyncStatusVisibleForTesting(controls.get()));
  EXPECT_FALSE(SidebarSyncStatusTextForTesting(controls.get()).empty());

  SetSidebarSyncSettingsExpandedForTesting(controls.get(), true);
  EXPECT_TRUE(SidebarSyncSettingsExpandedForTesting(controls.get()));
  EXPECT_TRUE(SidebarSyncStatusVisibleForTesting(controls.get()));

  SetSidebarSyncSettingsExpandedForTesting(controls.get(), false);
  EXPECT_FALSE(SidebarSyncStatusVisibleForTesting(controls.get()));
}

}  // namespace
}  // namespace ahoi::sidebar
