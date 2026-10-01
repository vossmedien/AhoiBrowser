# Build 60: native M154 split runtime

Frozen source `d0688d88601ee073dc383f81273786af79956ab4` passes **15/15**
`SplitLayoutMenuBrowserTest` cases: one job, one iteration, no retries or
skips, direct exit 0. The two new close/reorder regressions pass, as do the
previous multi-pane preset, reload, extraction and rollback failures.
The command, tested executable/component hashes, source and raw evidence
hashes are bound in `receipt.json`; complete logs and launcher summary remain
in `.work/agent-queue/60/native-split/`.

The guarded build compiled and linked the candidate and staged its runtime,
but ended exit 1 at Apple Development identity selection. No Build-60 bundle
was installed or visibly accepted. These native tests ran from the generated
output after that terminal signing failure, under the owner build/E2E locks
and after 96 seconds desktop idle. Both locks were released afterwards.
Installed source remains Build 59 (`d3ebc1b9`). The installed lifecycle/matrix
journeys and remaining product DoD gates stay open.
