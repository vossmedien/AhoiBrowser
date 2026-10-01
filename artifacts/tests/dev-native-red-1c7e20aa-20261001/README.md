# Build 61 compiled/signature GREEN, native fixture RED

Frozen source 1c7e20aa: guarded build exit 0, nested stable Apple Development
signing and stamped component/provenance verification pass. The generated
core Developer Toolkit native binary then ran with one job/no retries and ended
exit 1: 75 SUCCESS, 6 CRASH and 46 NOTRUN due to crashed test batches. This is
not 127 executed product assertions and cannot count as a native gate pass.

The crashes are the same PrefRegistry DCHECK: ahoi.developer_toolkit.profiles
was registered twice in fixture construction. Toolkit registration already
calls profile registration. Four new/modified fixtures incorrectly called both;
the next source removes the duplicate profile call and keeps the sole toolkit
registration. A separate stale-pref fixture still correctly registers only
profiles. No production registration is made idempotent and no DCHECK is hidden.
No installation or visible acceptance occurred; installed Build 59 remains.
