# Opaque UTF-8 order-key supplement

`merge_utf8_sort_keys_v3.json` contains eight additional field-merge cases for
Workspace `sort_key` and TreeNode `location`. It is intentionally separate from
the accepted 140 cases in `merge_v3.json`; their fixture hash is unchanged.
Regenerate with `tools/sync_conformance/generate_utf8_sort_vectors.py`.

Both canonically equivalent spellings of e-acute are valid UTF-8 but different
opaque wire keys. Equal field clocks with those different bytes must conflict
in either arrival order. A genuinely newer field clock wins and retains the
winning spelling's original bytes. The supplement's `inputValid: true` requires
both decoders to succeed before a merge rejection can count as the expected
conflict. It is fixture metadata, not a wire-model/capability change.

[Handoff 120](../../handoffs/crest-hardening/120-utf8-sort-key-equality/HANDOFF.md)
contains the narrow Swift equality/stamping fix, native regression source and
both runner integrations. The runner tests and C++ data registration must be
applied by their owners; the fixture alone is no native pass. A local before/
after assertion for an opaque key must compare UTF-8 bytes, not Swift String
canonical equivalence. This does not choose a new normalization policy for
human-readable text or replace the missing full H1 entity/sequence coverage.
