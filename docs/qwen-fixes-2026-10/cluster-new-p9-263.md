# Cluster new-p9-263

**Verdict: FIXED**

`Microtuner::keyToFreq()` used truncating integer division for both the mapped degree and the base degree. The remainder was normalized to a valid scale index, but negative degrees still lost the downward octave. Both divisions now use floor division for negative values; nonnegative behavior and the unmapped `-1` sentinel are unchanged.

Evidence: [Microtuner.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-263/src/core/Microtuner.cpp:88) computes the note octave and line 100 computes the base octave. The regression fixtures and assertions are in [SessionTuningTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-263/tests/src/core/SessionTuningTest.cpp:202) (helper), line 339 (negative mapped degree), and line 357 (negative base degree).

## Verification evidence

The affected ctest family is `SessionTuningTest`: 1/1 registered ctest executed. Its RED output reported 14 passed and the two new assertions failed; the GREEN ctest passed. No tests were skipped in the RED run.

| Purpose | Command | Exit | Log |
| --- | --- | ---: | --- |
| Build app and affected test with its real-instrument fixture | `cmake --build build --target zene SessionTuningTest tripleoscillator` | 0 | `build-2.log`, `build-3.log`, `build-6.log` |
| RED on original production code | `ctest -R '^SessionTuningTest$' --output-on-failure` (from `build/tests`) | 8 | `test-2.log` |
| GREEN with the fix | `ctest -R '^SessionTuningTest$' --output-on-failure` (from `build/tests`) | 0 | `test-6.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

The first test attempt ran before `tripleoscillator` was built and also failed the preexisting real-instrument slot. After building that fixture, the definitive RED run failed only the two new assertions: degree `-2` returned 783.991 Hz instead of 391.995 Hz, and base degree `-2` returned 246.942 Hz instead of 493.883 Hz.

The patch reversal proof used these exact operations, in order:

1. `git diff -- src/core/Microtuner.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-263.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-263.r1/logs/fix.patch` — exit 0.
2. `cmake --build build --target zene SessionTuningTest tripleoscillator` — exit 0 (`build-4.log`).
3. `ctest -R '^SessionTuningTest$' --output-on-failure` from `build/tests` — exit 8 (`test-4.log`); the same two assertions failed.
4. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-263.r1/logs/fix.patch` — exit 0.
5. `cmake --build build --target zene SessionTuningTest tripleoscillator` — exit 0 (`build-5.log`).
6. `ctest -R '^SessionTuningTest$' --output-on-failure` from `build/tests` — exit 0 (`test-5.log`).

## Files committed

- `src/core/Microtuner.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-263.md`
