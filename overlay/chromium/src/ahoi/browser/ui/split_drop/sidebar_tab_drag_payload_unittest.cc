// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"

#include "base/pickle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::drag {
namespace {

TEST(SidebarTabDragPayloadTest, SavedPayloadPreservesPrivateFormatAndFallback) {
  const base::Uuid node_id = base::Uuid::GenerateRandomV4();
  ui::OSExchangeData data;
  WriteSavedSidebarTabDragPayload(&data, node_id, u"Saved title");

  EXPECT_TRUE(data.HasCustomFormat(SavedSidebarTabDragFormat()));
  ASSERT_TRUE(data.GetString().has_value());
  EXPECT_EQ(u"Saved title", *data.GetString());
  EXPECT_EQ(node_id, ReadSavedSidebarTabDragPayload(data));
  ASSERT_TRUE(ReadSidebarTabDragPayload(data).has_value());
  EXPECT_EQ(node_id, ReadSidebarTabDragPayload(data)->saved_node_id);
}

TEST(SidebarTabDragPayloadTest,
     RuntimePayloadPreservesPrivateFormatAndFallback) {
  ui::OSExchangeData data;
  WriteRuntimeSidebarTabDragPayload(&data, 481, u"Runtime title");

  EXPECT_TRUE(data.HasCustomFormat(RuntimeSidebarTabDragFormat()));
  ASSERT_TRUE(data.GetString().has_value());
  EXPECT_EQ(u"Runtime title", *data.GetString());
  EXPECT_EQ(481, ReadRuntimeSidebarTabDragPayload(data));
  ASSERT_TRUE(ReadSidebarTabDragPayload(data).has_value());
  EXPECT_EQ(481, ReadSidebarTabDragPayload(data)->runtime_tab_handle);
}

TEST(SidebarTabDragPayloadTest, RejectsAmbiguousOrPortableOnlyData) {
  const base::Uuid node_id = base::Uuid::GenerateRandomV4();
  ui::OSExchangeData ambiguous;
  WriteSavedSidebarTabDragPayload(&ambiguous, node_id, u"Saved");
  base::Pickle runtime_pickle;
  runtime_pickle.WriteInt(9);
  ambiguous.SetPickledData(RuntimeSidebarTabDragFormat(), runtime_pickle);
  EXPECT_FALSE(ReadSidebarTabDragPayload(ambiguous).has_value());

  ui::OSExchangeData malformed_ambiguous;
  base::Pickle malformed_saved_pickle;
  malformed_saved_pickle.WriteInt(17);
  malformed_ambiguous.SetPickledData(SavedSidebarTabDragFormat(),
                                     malformed_saved_pickle);
  base::Pickle valid_runtime_pickle;
  valid_runtime_pickle.WriteInt(9);
  malformed_ambiguous.SetPickledData(RuntimeSidebarTabDragFormat(),
                                     valid_runtime_pickle);
  EXPECT_FALSE(ReadSidebarTabDragPayload(malformed_ambiguous).has_value());

  ui::OSExchangeData portable_only;
  portable_only.SetString(u"https://must-not-be-read.example/");
  EXPECT_FALSE(ReadSidebarTabDragPayload(portable_only).has_value());
}

}  // namespace
}  // namespace ahoi::drag
