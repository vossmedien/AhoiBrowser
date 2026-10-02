# Reentrant native browser settings: SOURCE correction, NOT BUILT

The current native setting projection spans all remote PrefService applications
with applying_product_state_. PublishPermittedProductSetting refuses every key
while that Boolean is true. A native observer triggered by remote A can make a
genuine local B edit: observation changes but no intent is stored. The projection
also consulted its earlier copied pending dictionary, allowing later remote B to
overwrite the unrecorded local value. Same-key local edits were suppressed too.
This contradicts ADR0010's preservation of genuine later local edits and the
native settings contract's suppression of only the original apply's own echo.

The corrected source tracks only the exact synchronous remote key/value. Native
integer/double normalization compares permitted numeric values semantically.
Other keys and different same-key values retain original local HLC intents. Once
a local intent exists, A->X->A remains a new edit even when the final value equals
the remote value. Each later apply and initial seed consults the LIVE pending
intent map. The ThemeService appearance guard remains independent. Generation
revocation clears the expectation; no AutoReset refers into a service across a
native observer that may destroy it. There is no wire/provider/key/consent change.

Five regressions WRITTEN, NOT compiled/run in the existing service suite: local
B edit during remote A prevents later remote B overwrite; different local value
of the same key; normalized numeric own echo; same-key local value bounce; and
reentrant Sync revocation leaves no stale echo scope. They use an explicitly
injected authorized projection on an owned TestingProfile/local backend, not
CloudKit or proof of a real linked Mac. No second-Mac roundtrip is fabricated.

Fresh host reads showed CPU near0%idle and pressure2, so no source compiler,
app/GUI/build/API action ran. Diff/source review only. Next: source syntax once
capacity exists, owner-reviewed coherent integration with the still-unbuilt
candidate, affected installed native settings/actual peer acceptance, then
necessary focused regressions. Frozen desktop72/28c94e0a and installed71 remain
unchanged; this new Sync delta is not claimed to be integrated into either.
