# 136 — Complete the debug Web Extension spike with a Files folder

Status: ready source patch; native/visible WebKit behavior and App Review
decision pending
Owner: mobile
Base: current Mobile source after 090; product paths were not edited by Crest.
Patch SHA-256:
`b672d73d97bebb105186677ab04530cf0cc4010d397562b714eb4fa3c0ec9702`.

## Scope and source basis

ADR 0012 step 1 requires one bundled MV3 extension **and one unpacked from
Files**. Integrated 090 loads only the bundled `WebExtensionSpike` folder.
Apple's [`WKWebExtension(resourceBaseURL:)`](https://developer.apple.com/documentation/webkit/wkwebextension/init%28resourcebaseurl%3A%29?changes=__9)
accepts a local directory containing `manifest.json` or a ZIP. SwiftUI's
[`fileImporter`](https://developer.apple.com/documentation/swiftui/view/fileimporter%28ispresented%3Aallowedcontenttypes%3Aallowsmultipleselection%3Aoncompletion%3Aoncancellation%3A%29?language=o_3)
provides the Files picker and a security-scoped URL. The target's debug build
already uses the public `WebPage.Configuration.webExtensionController` seam.

This patch adds a **DEBUG-only** Files button visible only under the existing
`-AhoiWebExtensionSpike` launch argument. It selects a `.folder`, acquires its
security scope, reads five known fixture resources and copies only those
byte-identical files into a unique local temporary folder. A modified script
or manifest is rejected before WebKit sees it. It then loads the unpacked
folder as a second context on the same non-persistent controller, with the
same spike-only grants as the bundled fixture. Unload removes the context and
its staged copy. Existing normal/default-store attachment and private/
separated exclusion remain unchanged; a general install or permission UI is
**not** introduced. The proposed unit tests cover load beside the bundled
context, unload, disabled runtime and changed-code refusal.

The exact-fixture restriction is deliberate: it tests Files access and
unpacked WebKit loading without opening arbitrary user-supplied code execution
in a shipping app. It does not establish the Step-2 general Files/ZIP installer
or App Store acceptance. Apple's current [App Review guideline 2.5.2](https://developer.apple.com/app-store/review/guidelines/uk/)
raises a specific risk for executing downloaded or installed code that changes
app functionality; applying that rule to a proposed general Web Extension
installer is an **inference** requiring the owner's product/legal review, not
an approval granted by the existence of WebKit's API. The DEBUG-only fixture
must remain out of release builds and the user still decides Step 2 after the
full spike evidence.

## Validation and owner acceptance

`136-mobile-webextension-files-spike.patch` touches only the existing runtime,
settings section and focused test file. Full review mirrors are under
`files/`; apply the patch, never replace newer Mobile sources. Three Swift
mirrors pass `swiftc -frontend -parse`; `git apply --check` passes. This lane
ran no iOS typecheck, XCTest, simulator, GUI, WebKit runtime, network or paid
API. Mobile's concurrent retention work remains separate.

In the owner's next bounded simulator slot, run the two added focused XCTest
methods and the existing 090 class against an exact source/binary receipt.
Then, with `-AhoiWebExtensionSpike`, choose an unpacked copy from Files and
record the second context actually loading on normal pages while private and
separated pages remain unaffected. Complete the existing 090 checklist with
observed content-script marker, DNR blocked request, storage across navigation,
action/popup behavior or precise adapter gap, permission callbacks, update
replacement, privacy report and App Review decision. A successful `load(_:)`
call alone is insufficient for these effects. Record the Files provider and
security-scope outcome, teardown the imported context and bind all visible
evidence to the exact candidate. No general Step-2 enablement follows from
this handoff.
