# Crest 142 owner fixes: bounded C++ syntax check

Same method as [the fc29437e check](../cpp-syntax-fc29437e-20260928/README.md)
on the overlay of `d7c9109c`: **11/11 files pass** pinned-Clang
`-fsyntax-only` (at most two processes, owner `build.lock` held and released).
Covered: R1 structure-commit retry and its backoff test, R2 sidebar discovery
activation, R5 popup promotion/split abort, R6 empty move group handling in
drop, context move, move command and group dialog, plus the new drop test and
its test-support field. Syntax/type only; nothing linked or run.
Receipt: [receipt.json](receipt.json).
