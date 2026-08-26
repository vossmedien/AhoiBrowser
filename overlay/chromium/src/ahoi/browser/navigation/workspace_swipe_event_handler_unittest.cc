// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_swipe_event_handler.h"

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point_f.h"

namespace ahoi {
namespace {

TEST(WorkspaceSwipeEventHandlerTest, MouseDragPassesThroughUntouched) {
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
      [](int* count, int) {
        ++*count;
        return true;
      },
      &switch_count));
  ui::MouseEvent drag(ui::EventType::kMouseDragged, gfx::PointF(12, 12),
                      gfx::PointF(12, 12), base::TimeTicks::Now(),
                      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);

  handler.OnEvent(&drag);

  EXPECT_FALSE(drag.handled());
  EXPECT_FALSE(drag.stopped_propagation());
  EXPECT_EQ(0, switch_count);
}

TEST(WorkspaceSwipeEventHandlerTest, PretargetHorizontalSwipeSwitchesOnce) {
  int last_delta = 0;
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
      [](int* count, int* recorded_delta, int delta) {
        ++*count;
        *recorded_delta = delta;
        return true;
      },
      &switch_count, &last_delta));

  ui::ScrollEvent begin(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_NONE, -45.0f, 0.0f,
                        -45.0f, 0.0f, 2, ui::EventMomentumPhase::NONE,
                        ui::ScrollEventPhase::kBegan);
  ui::Event::DispatcherApi(&begin).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&begin);
  EXPECT_FALSE(begin.handled());

  ui::ScrollEvent update(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                         base::TimeTicks::Now(), ui::EF_NONE, -45.0f, 0.0f,
                         -45.0f, 0.0f, 2, ui::EventMomentumPhase::NONE,
                         ui::ScrollEventPhase::kUpdate);
  ui::Event::DispatcherApi(&update).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&update);

  EXPECT_TRUE(update.handled());
  EXPECT_TRUE(update.stopped_propagation());
  EXPECT_EQ(1, switch_count);
  EXPECT_EQ(1, last_delta);
}

TEST(WorkspaceSwipeEventHandlerTest, MacLegacyPhaseSequenceSwitchesOnce) {
  int last_delta = 0;
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
      [](int* count, int* recorded_delta, int delta) {
        ++*count;
        *recorded_delta = delta;
        return true;
      },
      &switch_count, &last_delta));

  // ui/events/cocoa currently maps a direct NSEventPhaseMayBegin to
  // momentum=MAY_BEGIN while leaving ScrollEventPhase at kNone.
  ui::ScrollEvent begin(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_NONE, -25.0f, 1.0f,
                        -25.0f, 1.0f, 2, ui::EventMomentumPhase::MAY_BEGIN,
                        ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&begin).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&begin);
  EXPECT_FALSE(begin.handled());

  // Changed direct events have no phase information at all on macOS.
  ui::ScrollEvent update(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                         base::TimeTicks::Now(), ui::EF_NONE, -60.0f, 1.0f,
                         -60.0f, 1.0f, 2, ui::EventMomentumPhase::NONE,
                         ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&update).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&update);

  EXPECT_TRUE(update.handled());
  EXPECT_TRUE(update.stopped_propagation());
  EXPECT_EQ(1, switch_count);
  EXPECT_EQ(1, last_delta);

  ui::ScrollEvent end(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                      base::TimeTicks::Now(), ui::EF_NONE, 0.0f, 0.0f, 0.0f,
                      0.0f, 2, ui::EventMomentumPhase::END,
                      ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&end).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&end);
  EXPECT_TRUE(end.handled());
  EXPECT_EQ(1, switch_count);
}

TEST(WorkspaceSwipeEventHandlerTest, MacMomentumCannotInitiateWorkspaceSwitch) {
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
      [](int* count, int) {
        ++*count;
        return true;
      },
      &switch_count));

  ui::ScrollEvent begin(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_NONE, -10.0f, 0.0f,
                        -10.0f, 0.0f, 2, ui::EventMomentumPhase::MAY_BEGIN,
                        ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&begin).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&begin);

  ui::ScrollEvent direct_end(
      ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
      base::TimeTicks::Now(), ui::EF_NONE, -10.0f, 0.0f, -10.0f, 0.0f, 2,
      ui::EventMomentumPhase::END, ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&direct_end).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&direct_end);

  ui::ScrollEvent momentum(
      ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
      base::TimeTicks::Now(), ui::EF_NONE, -100.0f, 0.0f, -100.0f, 0.0f, 2,
      ui::EventMomentumPhase::INERTIAL_UPDATE, ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&momentum).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&momentum);

  EXPECT_FALSE(momentum.handled());
  EXPECT_EQ(0, switch_count);
}

TEST(WorkspaceSwipeEventHandlerTest, TargetPhaseScrollPassesThrough) {
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
      [](int* count, int) {
        ++*count;
        return true;
      },
      &switch_count));
  ui::ScrollEvent scroll(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                         base::TimeTicks::Now(), ui::EF_NONE, -100.0f, 0.0f,
                         -100.0f, 0.0f, 2, ui::EventMomentumPhase::NONE,
                         ui::ScrollEventPhase::kBegan);

  handler.OnEvent(&scroll);

  EXPECT_FALSE(scroll.handled());
  EXPECT_EQ(0, switch_count);
}

TEST(WorkspaceSwipeEventHandlerTest,
     WorkspaceSwipeOutsideConfiguredRegionPassesThrough) {
  int switch_count = 0;
  WorkspaceSwipeEventHandler handler(base::BindRepeating(
                                         [](int* count, int) {
                                           ++*count;
                                           return true;
                                         },
                                         &switch_count),
                                     {},
                                     base::BindRepeating([] { return false; }));

  ui::ScrollEvent begin(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_NONE, -45.0f, 0.0f,
                        -45.0f, 0.0f, 2, ui::EventMomentumPhase::NONE,
                        ui::ScrollEventPhase::kBegan);
  ui::Event::DispatcherApi(&begin).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&begin);

  ui::ScrollEvent update(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                         base::TimeTicks::Now(), ui::EF_NONE, -80.0f, 0.0f,
                         -80.0f, 0.0f, 2, ui::EventMomentumPhase::NONE,
                         ui::ScrollEventPhase::kUpdate);
  ui::Event::DispatcherApi(&update).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&update);

  EXPECT_FALSE(begin.handled());
  EXPECT_FALSE(update.handled());
  EXPECT_EQ(0, switch_count);
}

TEST(WorkspaceSwipeEventHandlerTest, CommandScrollSwitchesTabBeforeWorkspace) {
  int workspace_switch_count = 0;
  int tab_switch_count = 0;
  int last_delta = 0;
  WorkspaceSwipeEventHandler handler(
      base::BindRepeating(
          [](int* count, int) {
            ++*count;
            return true;
          },
          &workspace_switch_count),
      base::BindRepeating(
          [](int* count, int* last_delta, int delta) {
            ++*count;
            *last_delta = delta;
            return true;
          },
          &tab_switch_count, &last_delta));

  ui::ScrollEvent event(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_COMMAND_DOWN, 0.0f,
                        30.0f, 0.0f, 30.0f, 2, ui::EventMomentumPhase::NONE,
                        ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&event).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&event);

  EXPECT_TRUE(event.handled());
  EXPECT_TRUE(event.stopped_propagation());
  EXPECT_EQ(0, workspace_switch_count);
  EXPECT_EQ(1, tab_switch_count);
  EXPECT_EQ(1, last_delta);
}

TEST(WorkspaceSwipeEventHandlerTest, CommandScrollPreviewConsumesBeforeSwitch) {
  int preview_delta = 0;
  int tab_switch_count = 0;
  WorkspaceSwipeEventHandler handler(
      base::BindRepeating([](int) { return true; }),
      base::BindRepeating(
          [](int* count, int) {
            ++*count;
            return true;
          },
          &tab_switch_count),
      {},
      base::BindRepeating(
          [](int* recorded_delta, int delta) {
            *recorded_delta = delta;
            return true;
          },
          &preview_delta));

  ui::ScrollEvent event(ui::EventType::kScroll, gfx::PointF(), gfx::PointF(),
                        base::TimeTicks::Now(), ui::EF_COMMAND_DOWN, 0.0f,
                        -8.0f, 0.0f, -8.0f, 2, ui::EventMomentumPhase::NONE,
                        ui::ScrollEventPhase::kNone);
  ui::Event::DispatcherApi(&event).set_phase(ui::EP_PRETARGET);
  handler.OnEvent(&event);

  EXPECT_TRUE(event.handled());
  EXPECT_EQ(-1, preview_delta);
  EXPECT_EQ(0, tab_switch_count);
}

}  // namespace
}  // namespace ahoi
