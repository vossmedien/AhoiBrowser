# Sidebar focus/overflow timeout, 2 October 2026

Exact Build69 source c0afa449, one job/no retries, actual sidebar suite outcome:
**182 SUCCESS, 1 TIMEOUT, 8 NOTRUN**, direct exit1/nativePass false. The failing
case is `OverflowCuesAndFocusFollowAReorderedItem`; its next eight batch methods
never execute. Other queued UI programs do not launch after this RED. Raw summary,
log, executable/component hashes and runner state are preserved. Runner84809 and
test processes are terminal, owner lock released; no automatic retry waits.

The test requests focus without proving its native widget is active or its view
received focus. Its source now activates the owned widget, waits for IsActive and
asserts GetFocusedView before the reorder. The original item identity, scroll
visibility, second-layout/stale-frame ordering, final focus and overflow-arrow
checks remain unchanged. Syntax passes. This is a stronger fixture precondition,
**not proof of the timeout cause or a corrected native pass**; actual execution
must determine whether a compositor/layout defect remains.

Chromium ScrollView::GetVisibleRect returns content coordinates; the original
containment check is correct and was not weakened or replaced. The previously
executed Core140/editor5 and fifteen baseline programs/872 cases remain separate
valid partial evidence, not full candidate acceptance. Installed9acb43c8 is intact.

Next: freeze this fixture-only delta after fresh capacity/ownership checks; run
the actual affected sidebar cases/full suite, remaining UI/browser gates and only
then consider canonical installation and installed visible acceptance. Raw logs
are verbatim; any logged trailing whitespace is retained for hash integrity.
