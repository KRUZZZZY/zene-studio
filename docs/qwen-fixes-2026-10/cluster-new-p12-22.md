# Cluster new-p12-22

Verdict: **FIXED**

## Evidence

The six undoable mixer commands already left an undo step on the journal, so the registry created a fallback `control.transactions` row. That fallback had empty `before` and `inverse` objects. The handlers now supply a transaction payload describing the relevant prior state and naming `control.undo` as the inverse:

- `src/core/ControlCommandsMixerRename.cpp:80` — channel id and previous name.
- `src/core/ControlCommandsMixerTools.cpp:129` — previous send-enabled value; `:180` — newly created return channel and source.
- `src/core/ControlCommandsMixerSnapshots.cpp:141` — prior named snapshot (including its channels when replacing); `:162` — mixer state before recall; `:187` — removed snapshot's prior state.

The regression checks the newest transaction row's command, non-empty `before`, `control.undo` inverse, and `reversible` flag in `MixerStripTest`, `MixerToolsTest`, and `ProjectStateTest`.

## Commands and results

All build output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-22.r1/logs/`. CTest commands ran from `build/tests`.

| Command | Exit | Evidence |
|---|---:|---|
| `cmake --build build --target zene MixerStripTest MixerToolsTest ProjectStateTest` | 0 | `build-1.log` (baseline test build) |
| `ctest -R '^(MixerStripTest|MixerToolsTest|ProjectStateTest)$' --output-on-failure` | 8 | `test-1-red.log`; all 3 selected CTest targets failed at the empty-before assertion on the original production code |
| `cmake --build build --target zene MixerStripTest MixerToolsTest ProjectStateTest` | 2 | `build-2.log`; initial edit had a const reply object, corrected before proceeding |
| `cmake --build build --target zene MixerStripTest MixerToolsTest ProjectStateTest` | 0 | `build-3.log` |
| `ctest -R '^(MixerStripTest|MixerToolsTest|ProjectStateTest)$' --output-on-failure` | 0 | `test-2-green.log`; 3/3 selected CTest targets passed |
| `git diff -- src/core/ControlCommandsMixerRename.cpp src/core/ControlCommandsMixerTools.cpp src/core/ControlCommandsMixerSnapshots.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-22.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-22.r1/logs/fix.patch` | 0 | Production-only patch was removed for the proof run |
| `cmake --build build --target zene MixerStripTest MixerToolsTest ProjectStateTest` | 0 | `build-4-reversed.log` |
| `ctest -R '^(MixerStripTest|MixerToolsTest|ProjectStateTest)$' --output-on-failure` | 8 | `test-3-reversed-red.log`; the same three targets failed again with the production fix removed |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-22.r1/logs/fix.patch` | 0 | Production fix restored |
| `cmake --build build --target zene MixerStripTest MixerToolsTest ProjectStateTest` | 0 | `build-5-restored.log` |
| `ctest -R '^(MixerStripTest|MixerToolsTest|ProjectStateTest)$' --output-on-failure` | 0 | `test-4-restored-green.log`; 3/3 selected CTest targets passed |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log` |

The affected test family ran 3 CTest targets; no test selection was empty. The RED/GREEN result repeated with the production patch removed and restored, respectively.

## Files in the commit

- `src/core/ControlCommandsMixerRename.cpp`
- `src/core/ControlCommandsMixerTools.cpp`
- `src/core/ControlCommandsMixerSnapshots.cpp`
- `tests/src/core/MixerStripTest.cpp`
- `tests/src/core/MixerToolsTest.cpp`
- `tests/src/core/ProjectStateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-22.md`
