FIXED

## Decision and evidence

**REAL.** In [OpulenZ.cpp](../../plugins/OpulenZ/OpulenZ.cpp#L396), `playImpl` computes `chunkSize` from `frameCount` at line 401 and loops while `offset < out.size()`. When `frameCount` is zero and `out` is non-empty, the old code called `theEmulator->update(..., 0)` and added zero to `offset`, so it could spin indefinitely. The new zero check and `break` at lines 402–405 exit before the emulator update at line 407. Control then reaches the existing mutex unlock at line 419.

`frameCount` is assigned from `framesPerPeriod()` in the constructor at line 165 and has no other assignment. The only render loop using it is `playImpl`. The render caller at `src/core/InstrumentPlayHandle.cpp:66` invokes `m_instrument->play(buffer)`. `reloadEmulator()` does not change `frameCount`.

## Test matrix

| Case | Proof | Result |
|---|---|---|
| Normal positive chunking | `opulenzRendersTheProvidedSpanSize` renders a shorter-than-period span and a two-period span. | CTest `OpulenZSpanSize` passed. |
| Zero-sized chunk with non-empty output | `opulenzZeroFrameCountStopsLoop` requires the zero guard and `break` between chunk calculation and emulator update. It inspects source so the original infinite loop is never invoked by the test. | RED before fix and in the reverse-patch proof; GREEN after fix. |
| Empty output | The loop condition is false at entry when `out.size() == 0`; the existing unlock remains after the loop. | Confirmed by code path. |
| Period reconfiguration | `frameCount` is assigned only in the constructor; emulator reload does not alter it. The new guard handles zero without limiting positive frame counts. | Caller and assignment search completed. |
| Audio-thread constraints | The fix adds only a comparison and loop exit. It adds no allocation, lock, wait, I/O, logging, signal, or Qt call. | Confirmed by diff. |
| Platform/build | No new platform API or dependency. The new source check uses the existing Qt test harness and does not load a module; the existing span test retains its platform skip behavior. | Linux build and CTest passed. |

## Commands and results

Commands ran in the worktree unless a `build/tests` working directory is stated. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs`.

| Command | Exit | Evidence |
|---|---:|---|
| `cmake --build build --target zene PluginPortsMigrationTest` | 0 | Pre-fix test build; `build-2.log`. |
| `ctest -R '^OpulenZZeroFrameCount$' --output-on-failure` from `build/tests` | 8 | RED: assertion reported the missing zero guard; 1 test failed; `test-1.log`. |
| `cmake --build build --target zene opulenz PluginPortsMigrationTest` | 0 | Rebuilt after fix; also used for reverse-patch and restore builds; `build-3.log`, `build-4.log`, `build-5.log`. |
| `ctest -R '^OpulenZZeroFrameCount$' --output-on-failure` from `build/tests` | 0 | GREEN: 1/1 passed; `test-2.log`. |
| `ctest -R '^OpulenZ' --output-on-failure` from `build/tests` | 0 | 3/3 passed: `OpulenZ`, `OpulenZSpanSize`, `OpulenZZeroFrameCount`; `test-7.log`. |
| `git diff -- plugins/OpulenZ/OpulenZ.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs/fix.patch` | 0 | Removed only the product hunk for the required 4b proof. |
| `cmake --build build --target zene opulenz PluginPortsMigrationTest` with product hunk reversed | 0 | `build-4.log`. |
| `ctest -R '^OpulenZZeroFrameCount$' --output-on-failure` from `build/tests`, with product hunk reversed | 8 | Same test failed again; 1 test failed; `test-4.log`. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs/fix.patch` | 0 | Restored product hunk. |
| `cmake --build build --target zene opulenz PluginPortsMigrationTest` after restore | 0 | `build-5.log`. |
| `ctest -R '^OpulenZZeroFrameCount$' --output-on-failure` from `build/tests`, after restore | 0 | Same test passed; 1/1 passed; `test-5.log`. |
| `cmake --build build --target zene opulenz PluginPortsMigrationTest` after registering span-size CTest | 0 | `build-6.log`. |
| `ctest -R '^OpulenZ' --output-on-failure` from `build/tests` | 0 | Final family run: 3/3 passed; `test-7.log`. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-186` before source-manifest re-derivation | 1 | `gate-3.log` showed only `fork-sources` and `all-sources-reproduce` failing because the manifest still listed 1,148 vendored sfizz paths excluded by its own recipe. |
| `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \| grep -vE '^(src/3rdparty/\|tests/reference/\|plugins/NeuralAmp/(rtneural\|nam\|tests)/\|plugins/RnnoiseDenoiser/rnnoise/\|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } \| LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs/all-sources-entries-1.txt` | 0 | Ran the exact `Regenerate with` recipe from `tests/all-sources.txt`. |
| `python3 -c 'from pathlib import Path; manifest=Path("tests/all-sources.txt"); lines=manifest.read_text().splitlines(); first=next(i for i,line in enumerate(lines) if line.strip() and not line.lstrip().startswith("#")); manifest.write_text("\n".join(lines[:first]) + "\n" + Path("/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-186.r1/logs/all-sources-entries-1.txt").read_text())'` | 0 | Replaced only the manifest entry list with the generated output; 1,148 excluded sfizz entries removed. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-186` after re-derivation | 0 | All eight rows PASS; `gate-4.log`. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-186 new-p23-186` | 0 | First post-commit check passed; `submit-1.log`. |

The final feature-check table was:

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |

## Self-check

- **Regression proof:** `OpulenZZeroFrameCount` failed before the fix (exit 8), passed after it (exit 0), failed after reversing only the product hunk (exit 8), and passed after restoring the hunk (exit 0). Each run rebuilt the affected targets before CTest.
- **Determinism:** the regression reads `OpulenZ.cpp` and asserts the zero-chunk `break` occurs before emulator update. It uses no timeout, clock, sleep, thread, race, or test-only runtime hook.
- **Completeness:** searched all `frameCount` references and `playImpl` paths; checked constructor assignment, the `InstrumentPlayHandle` caller, and `reloadEmulator`. Positive-size chunk behavior and the later mutex unlock are unchanged.
- **Collateral and realtime:** projects/settings are untouched. Large spans remain processed in existing chunks; no limit was added. The audio path adds only a zero comparison and `break`.
- **Conditional verdict evidence:** not applicable; verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.
