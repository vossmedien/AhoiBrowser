// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_sync_control.h"

#include <memory>
#include <utility>

#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/prefs/pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/dialog_delegate.h"

namespace ahoi::sidebar {
namespace {

class SidebarBookmarkSyncControlTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    // Use the real profile service; global sync remains at its default false,
    // so these control/consent tests never start a backend or transport.
    sync::ProfileSyncServiceFactory::GetInstance();
    profile_ = TestingProfile::Builder().Build();
    ASSERT_TRUE(sync::ProfileSyncServiceFactory::GetForProfile(profile_.get()));
    ExpectNoConsent();

    owner_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                              views::Widget::InitParams::TYPE_WINDOW);
    owner_->SetBounds(gfx::Rect(100, 100, 400, 240));
    auto* contents = owner_->SetContentsView(std::make_unique<views::View>());
    auto control = CreateBookmarkSyncControl(profile_.get());
    ASSERT_TRUE(control);
    control_ = contents->AddChildView(std::move(control));
    control_->SetBounds(16, 16, 32, 32);
    ASSERT_EQ(1u, control_->children().size());
    trigger_ = views::AsViewClass<views::Button>(control_->children().front());
    ASSERT_TRUE(trigger_);
    owner_->Show();
    views::test::RunScheduledLayout(owner_.get());
    ASSERT_TRUE(control_->IsDrawn());
    ASSERT_TRUE(trigger_->IsDrawn());
  }

  void TearDown() override {
    trigger_ = nullptr;
    control_ = nullptr;
    owner_.reset();
    base::RunLoop().RunUntilIdle();
    profile_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  void ExpectNoConsent() {
    EXPECT_FALSE(
        profile_->GetPrefs()->GetBoolean(sync::kBookmarkSyncEnabledPref));
    EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(sync::kSyncEnabledPref));
  }

  views::Widget* OpenBubble() {
    views::test::ButtonTestApi(trigger_).NotifyDefaultMouseClick();
    base::RunLoop().RunUntilIdle();
    // BubbleDialogDelegate parents its native widget to the anchor's widget;
    // M152 exposes that ownership on Mac through GetAllOwnedWidgets().
    const auto owned =
        views::Widget::GetAllOwnedWidgets(owner_->GetNativeView());
    EXPECT_EQ(1u, owned.size());
    return owned.size() == 1u ? *owned.begin() : nullptr;
  }

  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<views::Widget> owner_;
  raw_ptr<views::View> control_ = nullptr;
  raw_ptr<views::Button> trigger_ = nullptr;
};

TEST_F(SidebarBookmarkSyncControlTest,
       OpeningCancellingAndReopeningNeverGrantsConsent) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    SCOPED_TRACE(attempt);
    auto* bubble = OpenBubble();
    ASSERT_TRUE(bubble);
    ASSERT_TRUE(bubble->IsVisible());
    auto weak_bubble = bubble->GetWeakPtr();
    ExpectNoConsent();
    auto* dialog = bubble->widget_delegate()->AsDialogDelegate();
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(dialog->GetCancelButton());
    dialog->CancelDialog();
    // The production close callback must release the client-owned Widget and
    // its widget-owned ModelHost exactly once, including on the next opening.
    EXPECT_FALSE(weak_bubble);
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(
        views::Widget::GetAllOwnedWidgets(owner_->GetNativeView()).empty());
    ExpectNoConsent();
  }
  EXPECT_TRUE(profile_->GetPrefs()
                  ->FindPreference(sync::kBookmarkSyncEnabledPref)
                  ->IsDefaultValue());
  EXPECT_TRUE(profile_->GetPrefs()
                  ->FindPreference(sync::kSyncEnabledPref)
                  ->IsDefaultValue());
}

TEST_F(SidebarBookmarkSyncControlTest, DestroyingOwnerDestroysItsOpenBubble) {
  auto* bubble = OpenBubble();
  ASSERT_TRUE(bubble);
  ASSERT_TRUE(bubble->IsVisible());
  auto weak_bubble = bubble->GetWeakPtr();
  auto weak_owner = owner_->GetWeakPtr();
  views::ViewTracker control_tracker(control_);
  trigger_ = nullptr;
  control_ = nullptr;
  owner_.reset();
  EXPECT_FALSE(weak_owner);
  EXPECT_FALSE(weak_bubble);
  EXPECT_FALSE(control_tracker.view());
  base::RunLoop().RunUntilIdle();
  ExpectNoConsent();
  // The profile outlives the UI. A subsequent real preference notification
  // must not invoke either the destroyed control or its closed bubble.
  auto* service =
      sync::ProfileSyncServiceFactory::GetForProfile(profile_.get());
  ASSERT_TRUE(service);
  ASSERT_TRUE(service->SetBookmarkSyncEnabled(true));
  ASSERT_TRUE(service->SetBookmarkSyncEnabled(false));
  base::RunLoop().RunUntilIdle();
  ExpectNoConsent();
}

TEST_F(SidebarBookmarkSyncControlTest,
       ConfirmingTheDialogChangesOnlyBookmarkCategoryConsent) {
  auto* bubble = OpenBubble();
  ASSERT_TRUE(bubble);
  auto weak_bubble = bubble->GetWeakPtr();
  auto* dialog = bubble->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(dialog);
  ASSERT_TRUE(dialog->GetOkButton());
  ExpectNoConsent();
  // Invoke the native dialog accept path, which runs the actual DialogModel
  // action and synchronous close callback, including preference reentrancy.
  dialog->AcceptDialog();
  EXPECT_FALSE(weak_bubble);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(profile_->GetPrefs()->GetBoolean(sync::kBookmarkSyncEnabledPref));
  EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(sync::kSyncEnabledPref));
  EXPECT_TRUE(profile_->GetPrefs()
                  ->FindPreference(sync::kSyncEnabledPref)
                  ->IsDefaultValue());
  auto* service =
      sync::ProfileSyncServiceFactory::GetForProfile(profile_.get());
  ASSERT_TRUE(service);
  EXPECT_TRUE(service->bookmark_sync_enabled());
  EXPECT_FALSE(service->sync_enabled());
  EXPECT_FALSE(service->initialized());
  EXPECT_TRUE(
      views::Widget::GetAllOwnedWidgets(owner_->GetNativeView()).empty());
}

}  // namespace
}  // namespace ahoi::sidebar
