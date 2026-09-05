# AnyChat normal Store flow — installed 4cb622a

Candidate: `/Applications/AhoiBrowser.app`, source
`4cb622a0bffc602051bf72e6e95b6100948f861e` (plist read back this continuation).
Build73875 / install77504 and immutable receipts are in
`artifacts/build/desktop-startup-guard-20260905/README.md`. No new build,
checkout refresh, alternate installer or non-Ahoi profile was used.

## Visible observations

After normal launch/Continue, the native browser window was explicitly Raised
before Cmd+T. An own new tab opened the official Store entry for
`khpefodpgnkegiohbolbaaeabnfdegln`; it redirected to
`https://chromewebstore.google.com/detail/anychat-ai-powered-browsi/khpefodpgnkegiohbolbaaeabnfdegln`.
The Store displayed AnyChat version1.0.8, publisher website tryanychat.com.
That is Store metadata, not installed-package/version verification.

The Store's generic Chrome-switch recommendation was dismissed with "Nein,
danke". Despite its remaining informational banner, the normal "Hinzufügen"
button reached Chromium's real native extension permission sheet in Ahoi.
No user-agent override, Google login, download/repackage or permission bypass
was used. Details disclosed these 14 website origins:

`chat.deepseek.com`, `chat.mistral.ai`, `chatgpt.com`, `claude.ai`, `copilot.com`,
`copilot.microsoft.com`, `gemini.google.com`, `grok.com`, `m365.cloud.microsoft`,
`meta.ai`, `openrouter.ai`, `perplexity.ai`, `www.meta.ai`, `www.perplexity.ai`.

Requested rights: read/change own data on those sites, replace the New Tab page,
and read visited-site icons. No permission was granted by the agent.

The first sheet was deliberately cancelled. The native sheet closed, the Store
returned to "Hinzufügen", and the browser remained visible/usable. This is a
bounded prompt/cancel PASS, not an installation or extension-functionality pass.

## Current action-time approval gate

The same normal Store button was used again and a second native permission
sheet opened. The user was asked asynchronously to approve AnyChat1.0.8 in
Ahoi with the listed website/New Tab/favicon permissions. No reply has yet
authorized those rights. On the next continuation the second sheet was also
cancelled so independent existing Sidebar UI could be exercised; the Store
returned to Hinzufügen. No modal is now left open. After approval, reopen the
normal Store flow and re-read its live scope before pressing "Erweiterung
hinzufügen". A changed version/permission scope needs a new decision.
Do not treat the earlier broad feature goal or another agent's fixture handoff
as this action-time approval. No runtime/UI slot has been handed to another agent.

Installation, action/Side Panel, shortcut, disable/enable, restart and
post-install crash regression are NOT RUN. The actual installed AnyChat state
has not been inferred from the Store description. Arc remains independently
blocked on the running source browser; its journal/backup are untouched.
