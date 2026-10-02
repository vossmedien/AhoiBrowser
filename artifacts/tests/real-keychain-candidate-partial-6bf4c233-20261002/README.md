# Real provider output-candidate partial evidence, 2 October 2026

Signed output6bf4c233 (not installed) starts windowed, idle309, no mock-keychain
switch, owned ready loopback fixture/profile. Server receives **two PaneA GETs**:
independent readiness plus the actual browser, each HTTP200; favicon also reaches
it. This differs from the prior default-provider hang with readiness only.

The protocol driver times out on **Network.enable**, before DOM samples or
explicit PaneB navigation. Thus no committed-document, cookie/restart or real
provider recovery pass is claimed. The output volume also logs clonefile EXDEV
against the macOS code-sign-clone directory; that is retained as a separate
location/launch finding, not silently ignored or proven as this protocol cause.
Shutdown deadline occurs; owned groups clean up, no remaining app/root.

The HTTP improvement is useful partial evidence for the native fallback. Final
proof still needs all necessary candidate native gates, canonical installation
on /Applications' filesystem, then real cookie/encryption/restart and visible
journeys WITHOUT mock-keychain. Raw hashes, tool/source/mode and actual failure
are bound in receipt.json. Original installed Build70 is unchanged.
