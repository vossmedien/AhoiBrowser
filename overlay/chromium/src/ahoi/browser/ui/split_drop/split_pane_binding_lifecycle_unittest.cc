// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/split_drop/split_pane_binding_lifecycle.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::split_drop {
namespace {

struct FakeContents {
  int id = 0;
};

class FakePaneHost {
 public:
  explicit FakePaneHost(std::vector<FakeContents*> bindings)
      : bindings_(std::move(bindings)) {
    for (FakeContents* contents : bindings_) {
      if (contents) {
        ++observer_counts_[contents->id];
      }
    }
    RecordMaximumObserverCount();
  }

  FakeContents* Get(size_t index) const { return bindings_[index]; }

  void Set(size_t index, FakeContents* contents) {
    if (bindings_[index] == contents) {
      return;
    }
    if (bindings_[index]) {
      --observer_counts_[bindings_[index]->id];
      events_.push_back(base::StringPrintf("detach:%zu", index));
    }
    bindings_[index] = contents;
    if (contents) {
      ++observer_counts_[contents->id];
      events_.push_back(base::StringPrintf("attach:%zu", index));
    }
    RecordMaximumObserverCount();
  }

  void Prepare() {
    events_.push_back("prepare");
    bindings_at_prepare_ = bindings_;
  }

  const std::vector<FakeContents*>& bindings() const { return bindings_; }
  const std::vector<FakeContents*>& bindings_at_prepare() const {
    return bindings_at_prepare_;
  }
  const std::vector<std::string>& events() const { return events_; }
  int maximum_observer_count() const { return maximum_observer_count_; }

 private:
  void RecordMaximumObserverCount() {
    for (int count : observer_counts_) {
      maximum_observer_count_ = std::max(maximum_observer_count_, count);
    }
  }

  std::vector<FakeContents*> bindings_;
  std::vector<FakeContents*> bindings_at_prepare_;
  std::vector<std::string> events_;
  std::array<int, 8> observer_counts_{};
  int maximum_observer_count_ = 0;
};

void Rebind(FakePaneHost* host,
            const std::vector<FakeContents*>& desired_bindings) {
  RebindSplitPanesWithoutOverlap(
      desired_bindings, [host](size_t index) { return host->Get(index); },
      [host](size_t index, FakeContents* contents) {
        host->Set(index, contents);
      },
      [host]() { host->Prepare(); });
}

TEST(SplitPaneBindingLifecycleTest, TwoPaneSwapDetachesBeforeEitherAttach) {
  FakeContents first{1};
  FakeContents second{2};
  FakePaneHost host({&first, &second});

  Rebind(&host, {&second, &first});

  EXPECT_EQ((std::vector<std::string>{"detach:0", "detach:1", "prepare",
                                      "attach:0", "attach:1"}),
            host.events());
  EXPECT_EQ((std::vector<FakeContents*>{nullptr, nullptr}),
            host.bindings_at_prepare());
  EXPECT_EQ((std::vector<FakeContents*>{&second, &first}), host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());
}

TEST(SplitPaneBindingLifecycleTest, ThreePaneRotationNeverDoubleSubscribes) {
  FakeContents first{1};
  FakeContents second{2};
  FakeContents third{3};
  FakePaneHost host({&first, &second, &third});

  Rebind(&host, {&third, &first, &second});

  EXPECT_EQ((std::vector<FakeContents*>{nullptr, nullptr, nullptr}),
            host.bindings_at_prepare());
  EXPECT_EQ((std::vector<FakeContents*>{&third, &first, &second}),
            host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());
}

TEST(SplitPaneBindingLifecycleTest,
     FourPaneReorderKeepsUnchangedPaneAndMovesOthersSafely) {
  FakeContents first{1};
  FakeContents second{2};
  FakeContents third{3};
  FakeContents fourth{4};
  FakePaneHost host({&first, &second, &third, &fourth});

  Rebind(&host, {&fourth, &second, &first, &third});

  EXPECT_EQ((std::vector<FakeContents*>{nullptr, &second, nullptr, nullptr}),
            host.bindings_at_prepare());
  EXPECT_EQ((std::vector<FakeContents*>{&fourth, &second, &first, &third}),
            host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());
}

TEST(SplitPaneBindingLifecycleTest,
     ClosingPaneShrinksFourToThreeToTwoWithoutOverlap) {
  FakeContents first{1};
  FakeContents second{2};
  FakeContents third{3};
  FakeContents fourth{4};
  FakePaneHost host({&first, &second, &third, &fourth});

  Rebind(&host, {&second, &third, &fourth, nullptr});
  EXPECT_EQ((std::vector<FakeContents*>{&second, &third, &fourth, nullptr}),
            host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());

  Rebind(&host, {&third, &fourth, nullptr, nullptr});
  EXPECT_EQ((std::vector<FakeContents*>{&third, &fourth, nullptr, nullptr}),
            host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());
}

TEST(SplitPaneBindingLifecycleTest, UnsplitMovesRetainedPaneOnlyAfterDetach) {
  FakeContents first{1};
  FakeContents second{2};
  FakeContents third{3};
  FakePaneHost host({&first, &second, &third, nullptr});

  Rebind(&host, {&third, nullptr, nullptr, nullptr});

  EXPECT_EQ((std::vector<FakeContents*>{nullptr, nullptr, nullptr, nullptr}),
            host.bindings_at_prepare());
  EXPECT_EQ((std::vector<FakeContents*>{&third, nullptr, nullptr, nullptr}),
            host.bindings());
  EXPECT_EQ(1, host.maximum_observer_count());
}

}  // namespace
}  // namespace ahoi::split_drop
