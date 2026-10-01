# Installed lifecycle setup RED

Verified installed 9acb43c8, owned fixture profile, actual 389 seconds input idle
and no running real app at launch. The journey exits 4 with pass:false before
lifecycle checks: PaneA is not the only visible page. The CDP snapshot contains
two blank-title documents, one visible, and AX shows two untitled/new tabs. The
local server log has no recorded requests. This is an unresolved startup/URL/
fixture readiness finding, not proof of an installed close crash or a PASS.
The runner is terminal, local fixture quit/cleanup ran and owner lock released.
Native split15, other gate results and the atomic installation receipt remain
separate valid evidence. Next diagnostics must bind actual HTTP readiness,
CDP URLs/navigation errors and app startup intent; do not weaken the expected
PaneA/layout/close assertions or promote the process exit to acceptance.
