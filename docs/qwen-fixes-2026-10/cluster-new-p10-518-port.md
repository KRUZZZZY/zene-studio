# Cluster new-p10-518-port

**Verdict: BLOCKED at the required ratchet gates.** The zero-cutoff behavior is fixed and its regression is red without the production patch and green with it. Both required `--check` gates still fail on unrelated, pre-existing fork-scope entries; their baselines were not changed.

## Finding and fix

`LinkwitzRiley::setCoeffs()` used the requested cutoff directly in the angular-frequency and tangent calculations. At zero, `wc / tan(pi * freq / sampleRate)` evaluates as `0 / 0`, making coefficients and processed output non-finite. The method now clamps finite cutoffs to a positive value below Nyquist and maps NaN to the lower bound before either calculation.

Evidence:

- `include/BasicFilters.h:74-106` bounds the requested cutoff before calculating `wc` and `k`.
- `tests/src/core/BasicFiltersSampleRateTest.cpp:69-83` exercises zero-cutoff low-pass and high-pass processing. Both assertions fail on the original implementation and pass with the fix.
- `tests/upstream-modifications.txt` records the inherited header change. The test source was already registered in `tests/fork-sources.txt`.

## Verification

One affected CTest family entry ran per ctest command: `BasicFiltersSampleRateTest` (1/1). Both zero-cutoff assertions failed on the original header, passed with the fix, failed again after inverse-applying only the production patch, and passed after restoring it.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Build original-code regression | `cmake --build build --target BasicFiltersSampleRateTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/build-red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Original-code RED | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/test-red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Build fixed code | `cmake --build build --target zene BasicFiltersSampleRateTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/build-green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Fixed-code GREEN | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/test-green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Save patch and inverse-apply production change | `git diff -- include/BasicFilters.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Rebuild without production patch | `cmake --build build --target zene BasicFiltersSampleRateTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/build-proof-red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Inverse-patch RED | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/test-proof-red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Rebuild restored fix | `cmake --build build --target zene BasicFiltersSampleRateTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/build-proof-green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Inverse-patch GREEN | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/test-proof-green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/gate-complexity-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-518-port.r1/logs/gate-filelength-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 1 |

The gate failures do not name the changed header or regression test. Complexity reports six unbaselined functions over CCN 10 (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`). File length reports thirteen unrelated unbaselined files over 500 lines, including `SmfInterchangeReader.cpp`, `TakeLane.cpp`, `DeviceDspTest.cpp`, `VideoTrackTest.cpp`, and `ArrangerTimelineTest.cpp`. The working diff contains no changes to those sources or the gate baselines; no gate was re-anchored.

The requested `fff` log-search tool was not available in this session (`command -v fff` returned 1); logs were searched with `rg -n` instead.

## Files committed

- `include/BasicFilters.h`
- `tests/src/core/BasicFiltersSampleRateTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-518-port.md`
