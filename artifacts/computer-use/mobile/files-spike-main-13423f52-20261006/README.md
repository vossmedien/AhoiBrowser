# Main candidate: Files spike and bundled fixture

Candidate13423f52ced6744842dfd2fea16f9aadda1e5f3f, clean DebugLocal/adHoc,
Xcode27.0/27A266a, owned iOS27 simulatorA058D204-F3CB-44B7-963C-1F785132EF8B.
Receipt SHA256a23915edfefedb155461c3b16295626cbec9babf66193ae90fb117e47d7aa225;
installed app/tree/binary/Info.plist matched that receipt before execution.
The exact xctestrun carries the receipt and source expectation; original and
exact runner hashes are separately bound in runner-binding.json.

| Visible journey | Result |
| --- | --- |
| Bundled content script, DNR control/blocked probe, distinct navigation token and increasing storage counter | PASS,55.822s |
| Native Files cancellation: no imported context and import still enabled | PASS,31.342s |
| Native Files folder selection, load beside bundled context, unload | PASS,51.207s |
| No launch opt-in: runtime remains off | PASS,33.953s |

Four UI journeys, then existing runtime tests5/5:0failures,0skips,0runtime warnings.
Own simulator Shutdown, staged five-file input removed by hashes, E2E lock released.
Real CloudKit mutation opt-in was NO and container ID empty. No other device,
profile, permissions or GUI selection was changed. Screenshots and their
xcresult attachment manifest are under attachments/; positive token/counter and
Files loaded/unloaded screenshots inspected by Root.

Originals/xcresults remain in ~/inhouse/evidence/ahoi-mobile-files-13423f52-20261006
and the existing task scratch. Text logs normalize whitespace; text-log-binding.json
preserves their raw hashes. This proves the Debug Step1 fixture/Files boundary,
not general extension installation, popup/permission/App Store acceptance or Step2.
Separate native correction review PASS, terminal0/no actionable regressions,
session01a1132d-53b3-71c3-acbc-6d693407572f, scope main318c4eff..13423f52.
Actual main integration/delivery remain open until their receipts are recorded
in the Mobile checkpoint.
