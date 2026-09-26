# 064 – Developer toolkit bubbles blur before destruction (as cc26c51)

Status: integrated 6456b1a (reviewed; focus_manager include sorted; syntax-checked; acceptance on build 38)
Owner lane: desktop (apply, build, test)
Base: HEAD `1c6e2ad` (`git apply --check` passes). By the crest-hardening lane;
syntax-checked read-only (0 errors), not built.

## Sweep for the cc26c51 pattern

`cc26c51` fixed NativeWidgetMac's focus DCHECK ("!!focused_now ||
!new_text_input_client || !is_top_level"), hit when a client-owned Widget is
destroyed while its Textfield is still the input method's text input client.
It covered the Workspace dialog, the group dialog and the archive search.
The sweep of the other client-owned Ahoi Widgets:

| Owner | Textfield | Result |
| --- | --- | --- |
| `DeveloperToolkitController`: cookie manager, profile editor (also header secret and asset policy editors inside it) | yes | **affected**: `On…Closed` moves the Widget into a posted `reset()`, and the destructor resets directly, without blurring |
| `DeveloperToolkitController`: main bubble, cache status | no or indirect | patched too (same helper, harmless) |
| `PrivacyModeController` bubble | no | not affected |
| sidebar discovery and recent links | Views inside the sidebar, not separate client-owned Widgets | not affected |

## Change

`BlurBeforeDestruction(widget)` (`FocusManager::ClearFocus`) in the
controller's destructor before each `reset()`, at the start of each
`On…Closed`, and in the posted deleters, as in `cc26c51`.

## Tests

Visible: open the cookie manager, focus the search or edit field, and close
it with Escape and with the close button; do the same with the profile
editor's name field. No DCHECK on a DCHECK build.
