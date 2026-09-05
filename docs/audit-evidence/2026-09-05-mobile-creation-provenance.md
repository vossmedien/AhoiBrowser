# Mobile creation-provenance P2 correction

Source correction to `3964bcbe1dad81967c2b984b8b17fb6750ba7748`, 2026-09-05.
Owner: Mobile `01a044d6-1545-7532-8394-6b7df1144bb1`, canonical shared branch.
Status: **implemented in source; not compiled or behavior-tested**.

## Finding and correction

A v2 node genuinely created by A at time 1000 retained only `known=true`.
A later valid v2 import from B kept the immutable creation time but rewrote
the `created_at` field clock to 5000. Field merge correctly selected that v2
clock, but provenance merge cleared the Boolean, and promotion consequently
used Bottom. Simply OR-ing the Boolean would wrongly attribute creation to B.

`TreeNode.creationProvenanceClock` now retains the actual observed HLC as local
snapshot metadata. It is not serialized by the desktop wire codec, is not a
new Creator ID, and does not alter the agreed v2/v3 maps or Golden bytes.
Existing Boolean-only snapshots recover their explicitly known clock; absent
or false flags do not manufacture an origin. Data already stripped of evidence
by an older buggy merge cannot be reconstructed and is not claimed recovered.

Legacy merges still choose their normal replicated field clocks and framing,
while independently retaining actual creation evidence. Promotion and the
origin projection use that evidence. Subsequent local edits preserve it.
TreeNode equality/hash deliberately exclude it; the import commit separately
detects evidence-only changes so they persist without a wire echo or an
artificial dominating merge clock. The v3 writer/live-import gates remain off.

## Follow-up: the peer promotes before this device

The read-only review of `895daf99c7b33610532dfe0ee4654a8bbe0fb9fa`
identified a remaining order-dependent loss: the local v2 node has genuine
evidence C, while an uninformed peer first promotes that same node to v3 with
the creation-provenance field clock Bottom. The v3 branch previously selected
the correct Wire Bottom but also cleared the independent local evidence.

Retained evidence is now collected independently of the result version. If
v3 selects Bottom, the result keeps the existing local observation rather than
clearing it. Wire Bottom remains untouched, even after later synthetic legacy
rewrites. Real v3 field-clock selection is unchanged. Creation-time values,
wire maps, record equality/hash, default writer 2 and the activation gate remain
unchanged. An already-v3 node is not repromoted merely to preserve local evidence.

Two additional regressions cover peer-first promotion with both saved/default
and explicitly temporary v3 states: both merge directions, JSON restart,
file-backed repository restart, repeated v3 import/no echo, unchanged replica
version/hash, preserved creation time and a subsequent newer legacy edit.
The reverse-order file test also exercises persisting only newly learned local
evidence when the replicated v3 row was already present. The unknown/unknown
case still has no origin. These tests are **source only, not executed**.

## Focused regression source

`SharedTabCreationProvenanceTests` exercises the actual v2 codec and field
merge, plus local repositories/file persistence, with no CloudKit/key access:

- Original A plus newer synthetic B creation clock, in both merge directions;
  same replicated value/hash/wire bytes, original A retained, later rewrite,
  snapshot restart, promotion and subsequent mixed-v3/v2 merge.
- File-backed repository restart and repeated imported-v2 replay, including
  explicit `shouldReenqueue=false` after convergence.
- Evidence-only state change committed despite equal replicated snapshots,
  without re-enqueue.
- Compatible Boolean-only snapshot decode; unknown/absent evidence still
  promotes to Bottom and creates no badge.
- A local edit after the legacy rewrite retains the original clock through
  later promotion, without raising the default wire writer above 2.

The existing frozen-fixture equality test now marks the retained clock, not a
Boolean. Its capability-byte assertion also explicitly marks the throwing
canonical-JSON call. The new test file is included in the generated Xcode project.

## Verification boundary

XcodeGen generation and `git diff --check` succeeded; changed source files
remain below the 800-line convention. No compiler, test suite, app, simulator,
server, profile, key or CloudKit operation was started for this correction.

The CPU start gate found foreign Blender data bake PID 57077 / parent 51136,
`Lagoon420/source-03`, at 261.3% and later 174.3%; its command and cwd identified
the canonical FillIt project. It was left untouched. Those are point-in-time
observations, not a permanent lease or an assertion of later process state.

Visible v3 promotion is not currently an available product journey because
v3 writer/live-tab activation is deliberately closed. At the next permitted
Swift verification phase, run the new provenance suite and the frozen/read/
merge regressions under that explicit E2E exception, binding results to the
new source/test binary. This report contains **no test pass** and does not
relabel the old Build15 Bookmark result as coverage of this correction.

The native Desktop Bookmark-v2 freeze/build does not depend on this Swift-only
fix. No common C++, GN, Golden fixture, ADR or installed Desktop candidate
was changed or reserved. Matching-client activation and the native cross-client
CloudKit roundtrip remain separate gates.

For the peer-first follow-up, the Desktop owner reported the active guarded
`225df88` build as Session `30212`. Mobile starts no competing intensive work
and does not infer a My-Mac/UI slot. The existing generated Xcode project already
references the extended test file. Source review and whitespace checking are
not compiler or test execution; both provenance corrections remain pending the
next separately permitted Swift verification phase.
