// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_HYBRID_LOGICAL_CLOCK_H_
#define AHOI_BROWSER_SYNC_HYBRID_LOGICAL_CLOCK_H_

#include <string>

#include "ahoi/browser/sync/sync_model.h"
#include "base/time/time.h"

namespace ahoi::sync {

// A small, persisted-state-friendly HLC implementation. Callers persist the
// returned stamp in the record; the clock itself can be reconstructed with the
// greatest locally observed stamp after a restart.
class HybridLogicalClock {
 public:
  explicit HybridLogicalClock(std::string device_tiebreak);
  HybridLogicalClock(const HybridLogicalClock&) = delete;
  HybridLogicalClock& operator=(const HybridLogicalClock&) = delete;
  ~HybridLogicalClock() = default;

  const std::string& device_tiebreak() const { return device_tiebreak_; }
  const HlcStamp& last() const { return last_; }

  // Produces a strictly newer local stamp, even when the wall clock moves
  // backwards or has insufficient resolution.
  HlcStamp Tick(base::Time wall_time = base::Time::Now());

  // Incorporates a remote stamp and returns a strictly newer local stamp.
  HlcStamp Observe(const HlcStamp& remote,
                   base::Time wall_time = base::Time::Now());

  // Restores the local clock after process restart. Invalid/foreign stamps are
  // ignored; the next local Tick still advances from the supplied value.
  void Restore(const HlcStamp& stamp);

 private:
  HlcStamp Advance(int64_t wall_time_us,
                   const HlcStamp* remote,
                   bool observe_remote);

  std::string device_tiebreak_;
  HlcStamp last_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_HYBRID_LOGICAL_CLOCK_H_
