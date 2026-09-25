# 005 – Disable the field-trial testing config (H5, N2)

Status: integrated 0981913 (pre-existing lean baseline failures from 75e20c1 remain open)
Owner lane: desktop (build configuration, lean contract)
Base: `f811604`

## Finding

Unbranded Chromium builds, including `is_official_build = true`, apply
`testing/variations/fieldtrial_testing_config.json` at startup
(`components/variations/service/variations_field_trial_creator.cc:143`).
This makes Google's test experiment set part of Ahoi's feature state, and it
can change network behavior. ungoogled-chromium sets
`disable_fieldtrial_testing_config=true`.

## Change

`fieldtrial-testing-config.patch`:

- adds `disable_fieldtrial_testing_config = true` to the lean profiles
  `ahoi-dev.gn` and `ahoi-release.gn`, directly after the existing lean delta
  lines;
- extends `fullBaselineContract.allowedLeanDelta` in
  `config/lean-chromium-components.json`;
- updates `expectedArgsSha256` of `ahoi-release` in
  `config/lean-bundle-measurement.json`.

The GN argument changes Chromium's compiled feature defaults, so it changes
the engine input key and needs the full fresh-profile network audit and the
affected visible journeys on the next candidate.

## Pre-existing red contract test (not caused by this handoff)

`tests/repository/test_lean_chromium.py` already fails at `f811604`:

- `test_full_profiles_are_byte_exact_pre_wave_one_baselines` fails for both
  profiles.
- `test_measurement_manifest_binds_profile_hashes_and_matrix_pin` fails for
  `ahoi-full-release` and `ahoi-release`.

Cause: `75e20c1` appended `enable_ahoi_ubo_classic = true` after the lean delta
lines and also changed the full profiles, whose hashes are pinned as
pre-wave-one baselines. The lean contract ("full + delta at the end") no longer
holds. With this handoff applied, no additional test fails, and the
`ahoi-release` manifest hash becomes correct again. Restoring the contract
(product flags before the delta, new full-profile baselines recorded as a
deliberate decision) is desktop's call.

## Apply

```sh
git apply handoffs/crest-hardening/005-fieldtrial-testing-config/fieldtrial-testing-config.patch
```
