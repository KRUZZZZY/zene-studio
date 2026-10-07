# Cluster 4846 — Detaching to another screen can report no change

Verdict: FALSE-POSITIVE

Worktree `zene-qwen-4846`, branch `bugs/qwen-4846`. No production code changed; no test changed.

## Why the finding cannot happen

The finding claims: detach a panel to screen 0, then `window.detach` it with screen 1 —
"the panel moves, but the result says `changed:false`", because `detachTo()` sets
`*changed = s->detachPanel(...)` and never folds `moveFloatingToScreen`'s result into it.

The fold is indeed absent, but it cannot change the answer, because `detachPanel` already
answers `true` for a panel that is floating:

- `src/gui/ControlCommandsLayouts.cpp:212-218` — `detachTo()`: `*changed = s->detachPanel(panel, reason);`
  (`:214`), the guard `if (!*changed && !s->isDetached(panel)) { return false; }` (`:215`),
  then `if (screen >= 0) { s->moveFloatingToScreen(panel, screen); }` (`:216`).
- `src/gui/shell/ShellFloating.cpp:245` — `detachPanel`: `if (m_floating.contains(id)) { return true; }`
  — an already-floating panel returns TRUE, so on the second detach `*changed` is already
  `true` before the move line runs. The result reports `changed:true`, not `changed:false`.
- `src/gui/shell/ShellFloating.cpp:302-313` — `moveFloatingToScreen` returns true only when
  `floatingWindow(id) != nullptr` (`:304`, guard `:306`), and
  `include/Shell.h:147` — `floatingWindow` is `m_floating.value(id)` — the same map
  `detachPanel` tested at `:245`. So whenever the move can return true, `*changed` is
  already true: the suggested `*changed |= s->moveFloatingToScreen(...)` is a provable no-op.
- The only theoretical escape — `detachPanel` false (`frame == nullptr`,
  `ShellFloating.cpp:239-244`) while `isDetached` true — is unreachable: `m_floating.insert`
  happens solely at `ShellFloating.cpp:254`, inside `detachPanel` past that null-frame guard,
  so every floating id has a frame.
- The finding's two-screen scenario additionally cannot be staged headless: `screenArg`
  (`src/gui/ControlCommandsLayouts.cpp:200-208`) refuses indices >= `QGuiApplication::screens().size()`
  (`:206`), and the offscreen platform reports one screen (the earlier bunny run probed
  `screens: 1`; `ControlCustomisation` drives `window.detach` with no screen arg and passes).

Luna's patrol assumed `detachPanel` returns false for an already-floating panel; it returns
true since the file's first commit (`21f1965ff`, 050 L13).

## Commands run (exit codes as observed)

- `cmake --build build --target ShellTest ShellLayoutTest ShellHomesTest > LOGS/build-1.log 2>&1` → BUILD_EXIT=0
- `cd build/tests && ctest -R '^(ShellTest|ShellLayoutTest|ShellHomesTest|ControlCustomisation)$' --output-on-failure > LOGS/test-2.log 2>&1` → TEST_EXIT=0 (4 tests run, all Passed; an earlier run, test-1.log, showed the three C++ binaries Not Run before building them)
- `bash tests/complexity-gate.sh --check` → EXIT=0 (LOGS/gate-complexity-1.log)
- `bash tests/file-length-gate.sh --check` → EXIT=0 (LOGS/gate-length-1.log)

RED: none — the claimed symptom (`changed:false` after a screen move) is unreachable, so no
failing assertion exists to write; the family above passes against the unfixed code, which is
the evidence, not a regression claim.

Commit: this report only.
