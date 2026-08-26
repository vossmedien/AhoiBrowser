// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_PANE_BINDING_LIFECYCLE_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_PANE_BINDING_LIFECYCLE_H_

#include <cstddef>
#include <type_traits>
#include <vector>

#include "base/check.h"

namespace ahoi::split_drop {

// Rebinds a fixed set of split-pane hosts without ever hosting one binding in
// two panes at the same time. Some WebContents-owned controllers intentionally
// permit only one UI observer (for example ReadAnything immersive mode), so a
// direct A<->B assignment can synchronously CHECK before the old pane gets a
// chance to unsubscribe.
//
// Every changed pane is detached first, `prepare_between_phases` updates the
// pane layout/visibility while no moving binding is hosted, and only then are
// destination bindings attached. The callbacks are synchronous and the
// binding type must have an empty value (normally nullptr).
template <typename DesiredBindings,
          typename GetBinding,
          typename SetBinding,
          typename PrepareBetweenPhases>
void RebindSplitPanesWithoutOverlap(
    const DesiredBindings& desired_bindings,
    GetBinding get_binding,
    SetBinding set_binding,
    PrepareBetweenPhases prepare_between_phases) {
  using Binding =
      std::decay_t<decltype(desired_bindings[static_cast<size_t>(0)])>;
  const Binding empty_binding{};
  const size_t pane_count = desired_bindings.size();
  CHECK(pane_count > 0u);

  std::vector<Binding> current_bindings;
  current_bindings.reserve(pane_count);
  for (size_t index = 0; index < pane_count; ++index) {
    current_bindings.push_back(get_binding(index));
  }

  // Both sides of the transition are model invariants. Checking them here
  // turns a future corrupt binding plan into a local, diagnosable failure
  // rather than a later observer-lifetime crash.
  for (size_t left = 0; left < pane_count; ++left) {
    for (size_t right = left + 1; right < pane_count; ++right) {
      CHECK(current_bindings[left] == empty_binding ||
            current_bindings[left] != current_bindings[right]);
      CHECK(desired_bindings[left] == empty_binding ||
            desired_bindings[left] != desired_bindings[right]);
    }
  }

  std::vector<size_t> changed_indices;
  changed_indices.reserve(pane_count);
  for (size_t index = 0; index < pane_count; ++index) {
    if (current_bindings[index] != desired_bindings[index]) {
      changed_indices.push_back(index);
    }
  }

  for (size_t index : changed_indices) {
    set_binding(index, empty_binding);
  }
  for (size_t index : changed_indices) {
    CHECK(get_binding(index) == empty_binding);
  }

  prepare_between_phases();

  for (size_t index : changed_indices) {
    const Binding desired_binding = desired_bindings[index];
    if (desired_binding == empty_binding) {
      continue;
    }
    for (size_t hosted_index = 0; hosted_index < pane_count; ++hosted_index) {
      CHECK(get_binding(hosted_index) != desired_binding);
    }
    set_binding(index, desired_binding);
    CHECK(get_binding(index) == desired_binding);
  }

  for (size_t index = 0; index < pane_count; ++index) {
    CHECK(get_binding(index) == desired_bindings[index]);
  }
}

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_PANE_BINDING_LIFECYCLE_H_
