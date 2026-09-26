// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_SYNC_SYNC_UNITTEST_SUPPORT_H_

#include <cstdint>
#include <string>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_model.h"
#include "base/time/time.h"
#include "base/uuid.h"

// Fixture helpers shared by the sync unit tests.
namespace ahoi::sync::test_support {

inline base::Uuid Id(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

inline base::Time At(int64_t micros) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

// Format 3 records need canonical device UUIDs and clocks/timestamps at or
// after the Unix epoch; fixtures express both as offsets from that minimum.
inline constexpr char kDeviceA[] = "10000000-0000-4000-8000-00000000d00a";
inline constexpr char kDeviceB[] = "10000000-0000-4000-8000-00000000d00b";

inline base::Time Ts(int64_t offset) {
  return At(kMinimumSyncClockPhysicalUs + offset);
}

inline SyncVersion Version(const char* device, int64_t offset, uint32_t logical = 0) {
  return SyncVersion{.model_version = kCurrentModelVersion,
                     .stamp = HlcStamp{.physical_time_us =
                                           kMinimumSyncClockPhysicalUs + offset,
                                       .logical = logical,
                                       .device_tiebreak = device}};
}

// Presence records must name their shared page, which is never the tab itself.
inline base::Uuid SharedPageFor(const char* tab_id) {
  std::string page(tab_id);
  page[0] = page[0] == 'f' ? 'e' : 'f';
  return Id(page.c_str());
}

inline RemoteTabRecord Tab(const char* id,
                    const char* device,
                    const char* session,
                    const char* url,
                    SyncVersion version,
                    bool incognito = false) {
  return RemoteTabRecord{.id = Id(id),
                         .device_id = Id(device),
                         .session_id = Id(session),
                         .url = url,
                         .title = "Ahoi",
                         .opened_at = Ts(10),
                         .last_active = At(version.stamp.physical_time_us),
                         .is_incognito = incognito,
                         .version = std::move(version),
                         .tree_node_id = SharedPageFor(id),
                         .target_kind = SharedTabTargetKind::kWeb};
}

}  // namespace ahoi::sync::test_support

#endif  // AHOI_BROWSER_SYNC_SYNC_UNITTEST_SUPPORT_H_
