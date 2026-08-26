// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_service.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

tab_tree::Workspace MakeWorkspace(std::string_view id,
                                  std::string sort_key,
                                  bool tombstone = false) {
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::ParseLowercase(id);
  workspace.name = u"Workspace";
  workspace.sort_key = std::move(sort_key);
  workspace.created_at = base::Time::UnixEpoch() + base::Seconds(1);
  workspace.modified_at = base::Time::UnixEpoch() + base::Seconds(1);
  workspace.tombstone = tombstone;
  return workspace;
}

class RecordingWorkspaceObserver : public WorkspaceServiceObserver {
 public:
  void OnWorkspaceListChanged() override { ++list_change_count; }

  void OnActiveWorkspaceChanged(
      const base::Uuid& window_id,
      const std::optional<base::Uuid>& old_workspace_id,
      const std::optional<base::Uuid>& new_workspace_id,
      WorkspaceActivationSource source) override {
    ++activation_count;
    last_window_id = window_id;
    last_old_workspace_id = old_workspace_id;
    last_new_workspace_id = new_workspace_id;
    last_source = source;
  }

  int list_change_count = 0;
  int activation_count = 0;
  base::Uuid last_window_id;
  std::optional<base::Uuid> last_old_workspace_id;
  std::optional<base::Uuid> last_new_workspace_id;
  WorkspaceActivationSource last_source = WorkspaceActivationSource::kRestore;
};

TEST(WorkspaceServiceTest, SortsAndFiltersSnapshotAtomically) {
  WorkspaceService service;
  const auto first = MakeWorkspace("00000000-0000-4000-8000-000000000001", "a");
  const auto second =
      MakeWorkspace("00000000-0000-4000-8000-000000000002", "b");
  const auto deleted =
      MakeWorkspace("00000000-0000-4000-8000-000000000003", "c", true);

  EXPECT_TRUE(service.ReplaceWorkspaces({second, deleted, first}));
  ASSERT_EQ(service.ordered_workspaces().size(), 2u);
  EXPECT_EQ(service.ordered_workspaces()[0].id, first.id);
  EXPECT_EQ(service.ordered_workspaces()[1].id, second.id);

  auto duplicate = second;
  duplicate.sort_key = "d";
  EXPECT_FALSE(service.ReplaceWorkspaces({first, second, duplicate}));
  ASSERT_EQ(service.ordered_workspaces().size(), 2u);
  EXPECT_EQ(service.ordered_workspaces()[1].id, second.id);
}

TEST(WorkspaceServiceTest, RejectsDuplicateAcrossLiveAndTombstoneRows) {
  WorkspaceService service;
  const auto original =
      MakeWorkspace("00000000-0000-4000-8000-000000000001", "a");
  ASSERT_TRUE(service.ReplaceWorkspaces({original}));

  auto deleted_duplicate = original;
  deleted_duplicate.tombstone = true;
  EXPECT_FALSE(
      service.ReplaceWorkspaces({original, std::move(deleted_duplicate)}));
  ASSERT_EQ(service.ordered_workspaces().size(), 1u);
  EXPECT_EQ(service.ordered_workspaces()[0], original);
}

TEST(WorkspaceServiceTest, RejectsInvalidModelRowsAtomically) {
  WorkspaceService service;
  const auto original =
      MakeWorkspace("00000000-0000-4000-8000-000000000001", "a");
  ASSERT_TRUE(service.ReplaceWorkspaces({original}));

  auto invalid = MakeWorkspace("00000000-0000-4000-8000-000000000002", "b");
  invalid.modified_at = base::Time();
  EXPECT_FALSE(service.ReplaceWorkspaces({invalid}));
  ASSERT_EQ(service.ordered_workspaces().size(), 1u);
  EXPECT_EQ(service.ordered_workspaces()[0], original);
}

TEST(WorkspaceServiceTest, ActivatesRelativePerWindowAndWraps) {
  WorkspaceService service;
  RecordingWorkspaceObserver observer;
  service.AddObserver(&observer);
  const auto first = MakeWorkspace("00000000-0000-4000-8000-000000000001", "a");
  const auto second =
      MakeWorkspace("00000000-0000-4000-8000-000000000002", "b");
  const base::Uuid window =
      base::Uuid::ParseLowercase("10000000-0000-4000-8000-000000000001");
  ASSERT_TRUE(service.ReplaceWorkspaces({first, second}));

  EXPECT_EQ(service.ActivateRelative(window, 1, true,
                                     WorkspaceActivationSource::kGesture),
            first.id);
  EXPECT_EQ(service.ActivateRelative(window, 1, true,
                                     WorkspaceActivationSource::kGesture),
            second.id);
  EXPECT_EQ(service.ActivateRelative(window, 1, true,
                                     WorkspaceActivationSource::kGesture),
            first.id);
  EXPECT_EQ(observer.activation_count, 3);
  EXPECT_EQ(observer.last_source, WorkspaceActivationSource::kGesture);

  service.RemoveObserver(&observer);
}

TEST(WorkspaceServiceTest, ReconciliationClearsRemovedActivation) {
  WorkspaceService service;
  RecordingWorkspaceObserver observer;
  service.AddObserver(&observer);
  const auto first = MakeWorkspace("00000000-0000-4000-8000-000000000001", "a");
  const auto second =
      MakeWorkspace("00000000-0000-4000-8000-000000000002", "b");
  const base::Uuid window =
      base::Uuid::ParseLowercase("10000000-0000-4000-8000-000000000001");
  ASSERT_TRUE(service.ReplaceWorkspaces({first, second}));
  ASSERT_TRUE(service.SetActiveWorkspace(window, second.id,
                                         WorkspaceActivationSource::kSidebar));

  ASSERT_TRUE(service.ReplaceWorkspaces({first}));
  EXPECT_EQ(service.GetActiveWorkspace(window), std::nullopt);
  EXPECT_EQ(observer.last_old_workspace_id, second.id);
  EXPECT_EQ(observer.last_new_workspace_id, std::nullopt);
  EXPECT_EQ(observer.last_source,
            WorkspaceActivationSource::kDataReconciliation);

  service.RemoveObserver(&observer);
}

}  // namespace
}  // namespace ahoi
