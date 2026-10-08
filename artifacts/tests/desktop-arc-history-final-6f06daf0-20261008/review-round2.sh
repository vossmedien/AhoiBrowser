#!/bin/bash
"/Users/vossmedien/.codex/packages/standalone/releases/0.160.1-aarch64-apple-darwin/bin/codex" review --base 3c370175712beb90ec414bfd89b29a39d502fd15 > "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-arc-verlauf-m155-e5fbc1f3/artifacts/tests/desktop-arc-history-final-6f06daf0-20261008/native-review-round2.log" 2>&1
result=$?
printf "%s\n" "$result" > "/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser-arc-verlauf-m155-e5fbc1f3/artifacts/tests/desktop-arc-history-final-6f06daf0-20261008/native-review-round2.exit"
