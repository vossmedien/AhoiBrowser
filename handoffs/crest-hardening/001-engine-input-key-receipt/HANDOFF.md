# 001 – Engine input key in build receipts (H4)

Status: integrated 2ab7909
Owner lane: desktop (build path)
Base: `tools/build_provenance.py` at `484a2f9` (unchanged at lane commit time)

## Purpose

Record the engine input key of `tools/engine_input_key.py` in every Ahoi build
receipt, so `python3 tools/engine_input_key.py lookup` and the orchestrator can
tell before a build whether an identical candidate already exists.

The key is derived from the receipt's own recorded fields
(`receipt_components`), so it describes what was actually built, not the
expected configuration.

## Apply

```sh
git apply handoffs/crest-hardening/001-engine-input-key-receipt/build_provenance.patch
```

Adds `engineInputKey` and `engineInputs` to Ahoi receipts (not to the
unmodified upstream control). No new receipt schema version is needed: both
fields are additive and existing readers ignore them.

## Expected tests

- `python3 -m unittest tests/repository/test_chromium_dependencies.py`
  (imports `build_provenance`; checked green with the patch applied).
- `python3 -m unittest tests/repository/test_engine_input_key.py`.
- Next regular build only: the new receipt carries `engineInputKey`, and
  `python3 tools/engine_input_key.py lookup` on the same clean source lists it
  under `reusable`. No build solely for this handoff.

## Evidence already available

Without the patch, the lookup recomputes keys from existing receipts. On
25 September it matched exactly one clean receipt each for exported source trees
`e9f4a99` (M153, Xcode 27), `4cb622a`, `c986090` and `92694fe`, and linked the
latter three to their installation receipts.

## Risks

`engine_input_key` imports `overlay_fingerprint`; both live in `tools/` next to
`build_provenance.py`. A receipt missing a key field fails closed with
`receipt lacks the fields of the engine input key`.
