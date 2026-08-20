# ADR 0001: Native Chromium `//chrome` fork

Status: accepted

## Decision

Build AhoiBrowser as a small patch/overlay layer on Chromium `//chrome`, using
Chromium Views and narrow AppKit bridges.

## Consequences

We retain browser correctness, extensions, media, DevTools, password management,
downloads, process isolation, and upstream security updates, at the cost of a
large source/build environment and continuous patch rebasing. Electron, CEF,
WKWebView, `content_shell`, Node, and a web-app shell are rejected.
