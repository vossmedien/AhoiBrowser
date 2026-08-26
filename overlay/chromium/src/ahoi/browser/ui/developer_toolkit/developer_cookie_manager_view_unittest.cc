// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_cookie_manager_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class FakeDeveloperCookieAdapter final : public DeveloperCookieAdapter {
 public:
  explicit FakeDeveloperCookieAdapter(std::vector<DeveloperCookie> cookies)
      : cookies_(std::move(cookies)) {}

  bool Load(const GURL& site_url,
            DeveloperCookieLoadCallback callback) override {
    ++load_count_;
    std::move(callback).Run(
        DeveloperCookieLoadResult{{DeveloperCookieError::kNone}, cookies_});
    return true;
  }

  bool Save(const GURL& site_url,
            std::optional<uint64_t> existing_cookie_id,
            DeveloperCookieDraft draft,
            DeveloperCookieMutationCallback callback) override {
    return false;
  }

  bool Delete(const GURL& site_url,
              uint64_t cookie_id,
              DeveloperCookieMutationCallback callback) override {
    return false;
  }

  bool DeleteMany(const GURL& site_url,
                  std::vector<uint64_t> cookie_ids,
                  DeveloperCookieMutationCallback callback) override {
    ++delete_many_count_;
    last_delete_ids_ = std::move(cookie_ids);
    pending_delete_callback_ = std::move(callback);
    return true;
  }

  void CompleteDelete(DeveloperCookieError error) {
    ASSERT_FALSE(pending_delete_callback_.is_null());
    if (error == DeveloperCookieError::kNone) {
      std::erase_if(cookies_, [this](const DeveloperCookie& cookie) {
        return std::ranges::find(last_delete_ids_, cookie.id) !=
               last_delete_ids_.end();
      });
    }
    std::move(pending_delete_callback_).Run({error});
  }

  int load_count() const { return load_count_; }
  int delete_many_count() const { return delete_many_count_; }
  const std::vector<uint64_t>& last_delete_ids() const {
    return last_delete_ids_;
  }

 private:
  std::vector<DeveloperCookie> cookies_;
  std::vector<uint64_t> last_delete_ids_;
  DeveloperCookieMutationCallback pending_delete_callback_;
  int load_count_ = 0;
  int delete_many_count_ = 0;
};

std::vector<DeveloperCookie> ExampleCookies() {
  return {
      {.id = 1,
       .name = "alpha-session",
       .value = "one",
       .domain = "example.test",
       .path = "/"},
      {.id = 2,
       .name = "beta-session",
       .value = "two",
       .domain = "example.test",
       .path = "/"},
      {.id = 3,
       .name = "alpha-preference",
       .value = "three",
       .domain = "example.test",
       .path = "/settings"},
  };
}

class DeveloperCookieManagerViewTest : public views::ViewsTestBase {};

TEST_F(DeveloperCookieManagerViewTest,
       BatchConfirmationTracksTheCurrentFilterSnapshot) {
  auto adapter = std::make_unique<FakeDeveloperCookieAdapter>(ExampleCookies());
  FakeDeveloperCookieAdapter* const adapter_ptr = adapter.get();
  auto view = std::make_unique<DeveloperCookieManagerView>(
      GURL("https://example.test/page"), std::move(adapter));
  ASSERT_EQ(1, adapter_ptr->load_count());

  view->SetFilterForTesting(u"alpha");
  ASSERT_EQ(2u, view->visible_cookie_count_for_testing());
  views::test::ButtonTestApi(view->delete_visible_button_for_testing())
      .NotifyDefaultMouseClick();
  const std::vector<uint64_t> alpha_ids = {1, 3};
  ASSERT_TRUE(view->pending_delete_ids_for_testing());
  EXPECT_EQ(alpha_ids, *view->pending_delete_ids_for_testing());
  EXPECT_EQ(0, adapter_ptr->delete_many_count());

  // A changed search invalidates the old destructive confirmation. The new
  // visible set must be confirmed independently.
  view->SetFilterForTesting(u"beta");
  EXPECT_FALSE(view->pending_delete_ids_for_testing());
  views::test::ButtonTestApi(view->delete_visible_button_for_testing())
      .NotifyDefaultMouseClick();
  const std::vector<uint64_t> beta_ids = {2};
  ASSERT_TRUE(view->pending_delete_ids_for_testing());
  EXPECT_EQ(beta_ids, *view->pending_delete_ids_for_testing());
  EXPECT_EQ(0, adapter_ptr->delete_many_count());

  views::test::ButtonTestApi(view->delete_visible_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(1, adapter_ptr->delete_many_count());
  EXPECT_EQ(beta_ids, adapter_ptr->last_delete_ids());
  EXPECT_FALSE(view->pending_delete_ids_for_testing());
  EXPECT_TRUE(view->busy_for_testing());
  EXPECT_FALSE(view->delete_visible_button_for_testing()->GetEnabled());

  // An in-flight batch cannot be dispatched again.
  views::test::ButtonTestApi(view->delete_visible_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(1, adapter_ptr->delete_many_count());

  adapter_ptr->CompleteDelete(DeveloperCookieError::kNone);
  EXPECT_FALSE(view->busy_for_testing());
  EXPECT_EQ(2, adapter_ptr->load_count());
  EXPECT_EQ(0u, view->visible_cookie_count_for_testing());
  EXPECT_FALSE(view->delete_visible_button_for_testing()->GetEnabled());
  EXPECT_TRUE(view->status_label_for_testing()->GetVisible());
}

TEST_F(DeveloperCookieManagerViewTest, NoMatchesCannotArmADeleteConfirmation) {
  auto adapter = std::make_unique<FakeDeveloperCookieAdapter>(ExampleCookies());
  auto view = std::make_unique<DeveloperCookieManagerView>(
      GURL("https://example.test/page"), std::move(adapter));

  view->SetFilterForTesting(u"does-not-exist");
  EXPECT_EQ(0u, view->visible_cookie_count_for_testing());
  EXPECT_FALSE(view->delete_visible_button_for_testing()->GetEnabled());
  views::test::ButtonTestApi(view->delete_visible_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_FALSE(view->pending_delete_ids_for_testing());
}

}  // namespace
}  // namespace ahoi
