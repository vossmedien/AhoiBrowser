#!/bin/bash
printf "%s\n" "$*" >> '/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/artifacts/tests/split-focus-yield-spaces-20261001/ax-calls.log'
[ "$1" != focused ] || printf "frontmostApp: Foreign pid=999 target=%s\n" "$2"
