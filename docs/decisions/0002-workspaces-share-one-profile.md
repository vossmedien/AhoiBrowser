# ADR 0002: Workspaces share one normal profile

Status: accepted

## Decision

All normal workspaces use one Chromium `Profile`. Workspaces contain tree,
session, ordering, and appearance data only. Incognito uses a true
off-the-record profile; Quick Window uses the shared normal profile.

## Consequences

Logins, cookies, history, permissions, extensions, and password managers remain
consistent while users swipe between workspaces. A workspace is not a security
boundary and the UI must never imply that it is.
