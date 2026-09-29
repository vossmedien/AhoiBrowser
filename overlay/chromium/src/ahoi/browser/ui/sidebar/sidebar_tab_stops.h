// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_STOPS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_STOPS_H_

#include <cstddef>
#include <optional>
#include <vector>

namespace ahoi::sidebar {

// The keyboard order of one window's sidebar (Crest dad3abad): next/previous
// tab and Cmd+1..9 walk what the sidebar shows for the active Workspace, top
// to bottom, instead of Chromium's window-wide tab strip. A stop is one saved
// row, one temporary row or one split; a split is entered at its first pane.
// Rows inside a collapsed folder are not stops, but their live tabs keep that
// folder's position so stepping from such a tab starts where it sits.
//
// Tabs are identified by Chromium's process-local tab handle value. This type
// knows nothing about Views or the tab strip so its rules stay unit-testable.
class SidebarTabStops {
 public:
  SidebarTabStops();
  SidebarTabStops(SidebarTabStops&&);
  SidebarTabStops& operator=(SidebarTabStops&&);
  ~SidebarTabStops();

  // Appends the next stop in display order. `tab_handles` lists the stop's
  // live tabs in pane order; empty input is ignored.
  void AddStop(std::vector<int> tab_handles);
  // Appends a position that is not a stop, such as a collapsed folder, with
  // the live tabs hidden below it; empty input is ignored.
  void AddHiddenTabs(std::vector<int> tab_handles);

  size_t stop_count() const { return stop_positions_.size(); }

  // Returns the tab to activate when stepping `delta` stops from the active
  // tab, wrapping at both ends. An active tab outside every position (for
  // example one of another Workspace) steps from the ends. Returns nullopt
  // when there is nothing to activate or the step lands on the active stop.
  std::optional<int> Step(std::optional<int> active_tab_handle,
                          int delta) const;
  // Cmd+1..8: the stop at zero-based `index`, or nullopt beyond the last stop
  // or when the active tab already belongs to that stop.
  std::optional<int> Nth(std::optional<int> active_tab_handle,
                         size_t index) const;
  // Cmd+9: the last stop, with the same no-op rule as Nth().
  std::optional<int> Last(std::optional<int> active_tab_handle) const;

 private:
  struct Position {
    std::vector<int> tab_handles;
    bool is_stop = false;
  };

  std::optional<size_t> FindPosition(int tab_handle) const;
  std::optional<int> StopTarget(std::optional<int> active_tab_handle,
                                size_t stop_index) const;

  std::vector<Position> positions_;
  // Indices into `positions_` of every stop, ascending.
  std::vector<size_t> stop_positions_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_STOPS_H_
