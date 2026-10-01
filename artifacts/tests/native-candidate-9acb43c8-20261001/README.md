# Candidate 9acb43c8 native gates

The correct native Chrome/TestingProfile editor target executes both unchanged
regressions: 2/2 SUCCESS, one job/no retries, exit 0. Disabled editor starts no
compiler and retains its draft; off/on closes the owned compiler, rejects the
old response and permits only a new explicit save. These are actual UI-object
lifetime assertions using injected compiler/store interfaces, not source analysis.

Fifteen other required native programs run serially on this exact candidate and
exit 0; per-program counts and result hashes are recorded. Known skips, if any,
remain explicit. Exact unchanged core/Mojo/worker executables and portable
component manifest prove the prior 127/20/3 cases carry; the unchanged sidebar
binary proves its 191 cases carry. No changed fixture/program is silently accepted.

Remaining pure Views/popup/sidebar-search gates run separately under UI ownership;
installed acceptance and full Master/worker scope remain open. No installation,
actual user profile change, external API or release occurred in these runs.
