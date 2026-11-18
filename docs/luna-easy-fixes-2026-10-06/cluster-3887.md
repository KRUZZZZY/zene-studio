# Cluster 3887 — A no-op meter arm still adds an undo step

Branch/base tip: `bugs/bunny-cmds1-2026-10-05` / `9e77a0188`.
Files: `src/core/ControlCommandsMeter.cpp`, `tests/control-meter-commands.py`, `cluster-3887-red.md`, `cluster-3887-green.md`.
Verdict: **FIXED**. The premise held: before the fix, `handleArm` unconditionally called `addUndoStep` and emitted a reversible transaction even when `requested == previous` (`src/core/ControlCommandsMeter.cpp:185-231`).

The handler now skips the setter, undo step, and transaction payload for a no-op. The existing socket regression checks that repeated disarming adds no transaction, and one undo still reverses the preceding real disarm; a repeated arm preserves its measurement. See [the regression](../../tests/control-meter-commands.py:35) and [implementation](../../src/core/ControlCommandsMeter.cpp:185).

RED: `cmake --build build --parallel 12 --target zene MeterTapTest audiofileprocessor` — EXIT 0 before the product edit; `ctest -R '^(ControlMeterCommands|MeterTapTest)$' --output-on-failure` from `build/tests` — EXIT 8, 2 tests ran, with both new no-op assertions failing.

GREEN: the same targeted build — EXIT 0; the same targeted CTest — EXIT 0, 2/2 passed. The detailed excerpts are in [RED](cluster-3887-red.md) and [GREEN](cluster-3887-green.md).

`bash tests/complexity-gate.sh --check` — EXIT 0; `bash tests/file-length-gate.sh --check` — EXIT 0. No shared headers changed; `audiofileprocessor` was rebuilt for the socket fixture.

Unverified: the full suite, gate sweep, other platforms, and CI were not run, as scoped by the request.
