# Trusted native startup preference and restore diagnosis

Installed Build71/source6bf4c233/M154, windowed real provider, no keychain mock.
Own synthetic retained session is copied; the original SNSS hash remains
991b5d78440a019af92d608036fe4fe5eb266cc4a89b94e746b0ae9b466bf13d.

Native Settings getter reads restore_on_startup5 (DEFAULT), despite the
offline JSON edit to1. Native setter succeeds and reads back1 (LAST). Native
close exits0. Replaying the same original session with that trusted preference
pair reads the exact old Session file, ProcessSessionWindows2, restores4+3
tabs and discovers the six expected fixture URLs (Solo, D, E, F, G, H).
The remaining NTP is recorded, not omitted. Native close exits0, owned cleanup
complete, original records unchanged.

This proves the effective-pref discrepancy and actual native session decoding,
not visible split geometry/group/window correctness or the full Master. The
previous three lifecycle failures remain RED until the corrected trusted-setter
harness actually passes its unchanged assertions on the installed candidate.
Native protection, user profile and source/build/install remain unchanged.
Raw browser/site logs are copied byte-for-byte; receipt binds them and tool/CDP
hashes are in each native state. No secret value or protected hash is exported.
