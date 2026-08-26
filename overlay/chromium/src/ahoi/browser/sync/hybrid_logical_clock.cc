// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/hybrid_logical_clock.h"

#include <algorithm>
#include <limits>

#include "base/check.h"

namespace ahoi::sync {

HybridLogicalClock::HybridLogicalClock(std::string device_tiebreak)
    : device_tiebreak_(std::move(device_tiebreak)) {
  DCHECK(!device_tiebreak_.empty());
  last_.device_tiebreak = device_tiebreak_;
}

HlcStamp HybridLogicalClock::Tick(base::Time wall_time) {
  return Advance(wall_time.ToDeltaSinceWindowsEpoch().InMicroseconds(), nullptr,
                 false);
}

HlcStamp HybridLogicalClock::Observe(const HlcStamp& remote,
                                     base::Time wall_time) {
  return Advance(wall_time.ToDeltaSinceWindowsEpoch().InMicroseconds(),
                 &remote, true);
}

void HybridLogicalClock::Restore(const HlcStamp& stamp) {
  if (stamp.device_tiebreak.empty()) {
    return;
  }
  if (last_ < stamp) {
    last_ = stamp;
  }
  last_.device_tiebreak = device_tiebreak_;
}

HlcStamp HybridLogicalClock::Advance(int64_t wall_time_us,
                                     const HlcStamp* remote,
                                     bool observe_remote) {
  const int64_t remote_physical =
      observe_remote && remote ? remote->physical_time_us : 0;
  int64_t physical = std::max({wall_time_us, last_.physical_time_us,
                                remote_physical});

  uint32_t logical = 0;
  bool counter_overflow = false;
  if (physical == last_.physical_time_us &&
      (!observe_remote || !remote || physical != remote->physical_time_us)) {
    if (last_.logical == std::numeric_limits<uint32_t>::max()) {
      counter_overflow = true;
    } else {
      logical = last_.logical + 1;
    }
  } else if (observe_remote && remote && physical == remote->physical_time_us &&
             physical == last_.physical_time_us) {
    logical = std::max(last_.logical, remote->logical);
    if (logical == std::numeric_limits<uint32_t>::max()) {
      counter_overflow = true;
      logical = 0;
    } else {
      ++logical;
    }
  } else if (observe_remote && remote && physical == remote->physical_time_us) {
    if (remote->logical == std::numeric_limits<uint32_t>::max()) {
      counter_overflow = true;
    } else {
      logical = remote->logical + 1;
    }
  }

  // A logical counter overflow must still produce a strictly newer stamp.
  // Advancing the physical component is practically unreachable, but keeping
  // the invariant here prevents a malformed/fuzzed clock from duplicating a
  // version after UINT32_MAX local events in one microsecond.
  if (counter_overflow) {
    if (physical < std::numeric_limits<int64_t>::max()) {
      ++physical;
    }
  }

  last_ = HlcStamp{.physical_time_us = physical,
                   .logical = logical,
                   .device_tiebreak = device_tiebreak_};
  return last_;
}

}  // namespace ahoi::sync
