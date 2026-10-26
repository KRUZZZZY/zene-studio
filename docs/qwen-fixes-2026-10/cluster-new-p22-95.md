FIXED

new-p22-95: FALSE-POSITIVE
Evidence: `Rack::chainCount()` in `src/core/Rack.cpp:71-74` always returns `1 + m_parallelChains.size()`, so a resolved rack cannot have count zero. With count >= 1, `index == -1` cannot equal `chainCount() - 1`; `journalChainRemoval()` in `src/core/ControlDeviceMoves.cpp:157-163` returns false before recording a redo. `Rack::removeChain()` also refuses indices below 1 (`src/core/Rack.cpp:188-190`). No regression test was added for an unreachable state; a test with a real rack would already pass without a change.
Evidence command: `rg -n -C 2 'Rack::chainCount|journalChainRemoval|Rack::removeChain' src/core/ControlDeviceMoves.cpp src/core/Rack.cpp` (EXIT=0).

new-p21-83: FIXED
Evidence: `recreateDevices()` now returns an empty `QJsonArray` immediately when its resolver returns null (`src/core/ControlDeviceMoves.cpp:99-103`). The pointer overload and resolver callers in `ControlCommandsDeviceCopy.cpp`, `ControlCommandsDeviceTools.cpp`, and `ControlCommandsRackWrap.cpp` all use this shared function; a missing destination chain now follows the existing empty-result path without dereferencing it. The added deterministic test injects a resolver that always returns null and a nonempty state list (`tests/src/core/DeviceMovesUndoTest.cpp:104-110`).
Evidence command: `rg -n -C 2 'recreateDevices|recreationWithoutAChainReturnsNoIds' src/core/ControlDeviceMoves.cpp tests/src/core/DeviceMovesUndoTest.cpp src/core/ControlCommandsDeviceCopy.cpp src/core/ControlCommandsDeviceTools.cpp src/core/ControlCommandsRackWrap.cpp` (EXIT=0).

Regression proof (run from `build/tests`):

- RED on the original code: `./DeviceMovesUndoTest recreationWithoutAChainReturnsNoIds > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (test EXIT=139, segmentation fault).
- GREEN with the fix: `./DeviceMovesUndoTest recreationWithoutAChainReturnsNoIds > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (test EXIT=0).
- 4b captured and reversed only the product patch: `git diff -- src/core/ControlDeviceMoves.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/fix.patch` (EXIT=0). Rebuilt original code with `cmake --build build --target zene DeviceMovesUndoTest` (EXIT=0), then ran `./DeviceMovesUndoTest recreationWithoutAChainReturnsNoIds > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (test EXIT=139). Restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/fix.patch` (EXIT=0).
- Fixed-code builds: `cmake --build build --target zene DeviceMovesUndoTest` (EXIT=0); after restoring for the final run, `cmake --build build --target zene DeviceMovesUndoTest UndoHonestyStateTest` (EXIT=0).

new-p21-84: FALSE-POSITIVE
Evidence: the undo lambda at `src/core/ControlDeviceMoves.cpp:70-75` passes its possibly null chain to `removeRecordedEffect()`. That function calls `madeEffect()`; `src/core/ControlStructuralSupport.cpp:599-603` explicitly returns null before reading `chain->effects()` when the chain is null. The subsequent `chain->removeEffect()` is inside a branch that only runs when `madeEffect()` returned a real effect (`ControlStructuralSupport.cpp:613-620`). There is no null dereference to fix.
Evidence command: `rg -n -C 3 'recordRecreation|removeRecordedEffect|madeEffect|if \(chain == nullptr\)' src/core/ControlDeviceMoves.cpp src/core/ControlStructuralSupport.cpp` (EXIT=0).

## Test matrix

| Bug | Normal path | Failure or edge path | Lifetime/reconfiguration | Audio-thread/build scope |
|---|---|---|---|---|
| new-p21-83 | Existing device-copy undo cases | Null resolver plus nonempty state returns no IDs | Resolver is already null before recreation begins | Control/undo only; `DeviceMovesUndoTest` |
| new-p21-84 | Existing device undo/redo tests | Null chain is guarded by `madeEffect()` | `recordRecreation` can run after chain disappearance; callee returns safely | Control/undo only; false-positive, no test needed |
| new-p22-95 | `UndoHonestyTest::removingTheLastRackChainIsUndone` | Negative index is refused for every reachable rack count | A zero-chain `Rack` is impossible by `chainCount()`'s implementation | Control/undo only; false-positive, no regression test needed |

## Test and gate commands

- The first affected-family run exposed unbuilt test fixtures: `ctest -R '^(DeviceMovesUndoTest|UndoHonestyStateTest)$' --output-on-failure` from `build/tests` (EXIT=8; `DeviceMovesUndoTest` could not load amplifier/bassbooster; the new regression itself passed). Built the declared module targets with `cmake --build build --target amplifier bassbooster` (EXIT=0), then reran the same ctest command from `build/tests` (EXIT=0; 2/2 tests passed).
- Final affected-family build: `cmake --build build --target zene DeviceMovesUndoTest UndoHonestyStateTest UndoHonestyTest` (EXIT=0). Final family command from `build/tests`: `ctest -R '^(DeviceMovesUndoTest|UndoHonestyStateTest|UndoHonestyTest)$' --output-on-failure` (EXIT=0; 3/3 tests passed). Focused normal-path command from `build/tests`: `./UndoHonestyTest removingTheLastRackChainIsUndone > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (EXIT=0; 0 skipped).
- The complete build and ctest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-95.r1/logs/`; final ctest log `test-6.log` records 3 tests run and 3 passed.
- The first `feature_check.py` run was EXIT=1 because the existing `tests/all-sources.txt` differed from its own header recipe by 1148 vendored `plugins/SfzPlayer/sfizz` entries. Re-derived the entry list from that published recipe (0 added, 1148 removed), registered the manifest maintenance in `tests/upstream-modifications.txt`, and reran the gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p22-95` (EXIT=0; all eight gates PASS, including complexity and file-length with no new-item failures). `gate-1.log` records the initial failure; `gate-2.log` through `gate-4.log` record the all-PASS reruns.
- Log searches used `rg` because no fff grep tool was available in this session.

## Self-check

- RED/GREEN proof: the deterministic injected null resolver test failed with exit 139 on the original product code and again after reversing the product patch; it passed with the patch restored (exit 0). Build and test exit codes are listed above.
- Regression scope: `recreationWithoutAChainReturnsNoIds` proves the only reachable behavior changed by the fix: no chain means no IDs and no dereference. The other two findings are false positives by the cited implementation invariants, so no failing-before-fix regression exists for them.
- Undo and commands: the guard returns before any effect is created or undo step is recorded. No undo structure or control command changed; existing undo behavior remains covered by the three passing ctests.
- Realtime and saved data: the helper is used on control/undo paths, not audio processing. The guard performs no audio-thread work. It changes no serialized data, limits, or valid live-chain behavior, so old projects and saved data are unaffected.
- False-positive evidence: `Rack::chainCount()` cannot be zero, and `madeEffect()` returns before dereferencing a null chain; the source commands and line references are given with each verdict above.
- Committed files: `src/core/ControlDeviceMoves.cpp`, `tests/src/core/DeviceMovesUndoTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-new-p22-95.md`.
