# Main-navigation header revocation source, 2 October 2026

The Chrome-created URLLoader throttle for main navigations had the same missing
response/redirect retirement as the separately corrected document factory.
Its optional approval callback now captures only saved metadata/references,
actual WebContents/PrefService/helper ownership and activation generation, plus
the staged navigation ID where materialized secrets exist. Request, redirect
and response boundaries revalidate this context; withdrawal irreversibly clears
the request-local profile. Redirects restore originals and clear conflicting
normal/exempt updates. Native request/network/CORS ownership is unchanged, no
timer or arbitrary worker-parent lookup is added, and already sent requests
cannot be retroactively unsent.

Standalone deterministic throttles keep their explicit default without a
context callback; the document factory retains its independently bound native
approval predicate. Main-navigation materialized values remain request-scoped,
not in that callback's captured metadata or a document-wide profile.

Three new methods cover response rules after master off/on, persistent reset
with same-origin redirect/exempt conflict, and retirement of the staged secret
navigation before a late response. Three final serial pinned-Clang syntax checks
pass for implementation, document factory API consumer and these native tests.
Exact source/header/command/log hashes are retained. Methods are **not executed**;
no native main-navigation or full product acceptance is claimed by this source.

Next combined candidate supersedes the previously prepared but unlaunched
0d7b807f plan. It also contains document/Mojo retirement and the native shelf
fixture. Core148/Mojo29, editor5, sidebar and remaining native/browser/installed
gates require actual candidate-bound results. Installed9acb43c8 remains intact.
No real Keychain values, API/device, build/sign/install or release were used.
