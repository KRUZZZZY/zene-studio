# Cluster new-p8-33

**Verdict: FIXED**

`note.add` used `DefaultVolume` for the created note when `velocity` was omitted, but `noteArgsOf` converted the absent JSON value to `0` for the transaction's embedded note description. The fallback in [ControlCommandsNotes.cpp](../../src/core/ControlCommandsNotes.cpp#L65) now uses `DefaultVolume`, matching the construction path at lines 129–130. The regression in [UndoHonestyTest.cpp](../../tests/src/core/UndoHonestyTest.cpp#L378) calls `note.add` without velocity and checks both the returned note velocity and the recorded `before.notes.velocity`.

## Evidence

The corrected regression failed against the original production code: `before.notes.velocity` was **0**, expected **100** (`DefaultVolume`). Reversing the production patch reproduced the same failure; restoring it made the same test pass.

The affected ctest family contains **1 registered test** (`UndoHonestyTest`). The original-code RED run reported 9 QtTest passes, 1 failure, and 4 skips; GREEN runs reported 1/1 ctest test passed.

| Stage | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial harness probe, superseded because it read `before.notes` as an array (the payload is an object) | `cmake --build build --target zene UndoHonestyTest -j12` | 0 | `build-1.log` |
| Initial harness probe | `ctest -R '^UndoHonestyTest$' --output-on-failure` (cwd: `build/tests`) | 8 | `test-1.log`; assertion-shape error, not regression evidence |
| Corrected test, original production code | `cmake --build build --target zene UndoHonestyTest -j12` | 0 | `build-2.log` |
| **RED**, original production code | `ctest -R '^UndoHonestyTest$' --output-on-failure` (cwd: `build/tests`) | 8 | `test-2.log`; actual 0, expected 100 |
| Fixed production code | `cmake --build build --target zene UndoHonestyTest -j12` | 0 | `build-3.log` |
| **GREEN**, fixed production code | `ctest -R '^UndoHonestyTest$' --output-on-failure` (cwd: `build/tests`) | 0 | `test-3.log`; 1/1 passed |
| Reverse-proof patch removal | `git diff -- src/core/ControlCommandsNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-33.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-33.r1/logs/fix.patch` | 0 | Command result |
| Rebuilt without production fix | `cmake --build build --target zene UndoHonestyTest -j12` | 0 | `build-4.log` |
| **RED**, fix removed | `ctest -R '^UndoHonestyTest$' --output-on-failure` (cwd: `build/tests`) | 8 | `test-4.log`; actual 0, expected 100 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-33.r1/logs/fix.patch` | 0 | Command result |
| Rebuilt with production fix restored | `cmake --build build --target zene UndoHonestyTest -j12` | 0 | `build-5.log` |
| **GREEN**, production fix restored | `ctest -R '^UndoHonestyTest$' --output-on-failure` (cwd: `build/tests`) | 0 | `test-5.log`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-33.r1/logs`. The fff MCP search tool was not available in this session, so log searches used `rg`.

## Files committed

- `src/core/ControlCommandsNotes.cpp`
- `tests/src/core/UndoHonestyTest.cpp` (already registered in `tests/fork-sources.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p8-33.md`
