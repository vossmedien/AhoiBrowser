# Installed split matrix: reorder RED, later focus cancellation

Exact installed Build71/source6bf4c233, trusted native startup setters, real
windowed provider, no mock. Fifteen assertions reached: fourteen pass, pane
reorder fails (focus1 stays A, focus2 B after Cmd+Ctrl+Shift+Right). Drafts and
marks remain, no reload inferred. Later focus loss during four-pane shortcuts
yields8/pass:false; all owned roots terminal, lock released. Unreached checks
are OPEN, never inferred from the passing two/three/four-pane setup.

Current native browser test invokes AcceleratorPressed directly on NTPs and
passes, so it does not prove this actual keyboard/live-page path. Mac symbolic
hotkey readback contains no enabled matching Cmd+Ctrl(+Shift) arrow binding.
Source also shows moved split tabs notifying generic OnNativeChanged, whereas
CaptureSplits adopts topology only for changed_native_splits; this is a
reversion hypothesis until actual command delivery is traced. Do not build a
guessed fix or waive the reorder assertion. Retain own session/profile and
original snapshots for focused diagnosis; no replay of fourteen passed checks.
