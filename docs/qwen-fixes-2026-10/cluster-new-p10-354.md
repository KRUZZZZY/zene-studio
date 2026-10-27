FIXED

# Cluster new-p10-354 — Tone generation produces NaN frames at a zero sample rate

## Verdict and evidence

**FIXED.** The defect is real at the helper: `sampleops::generate(Kind::Tone, 440.0, 1.0f, 16, 0)`
returned 16 NaN-valued frames before the fix.

- The division by the rate is `src/core/SampleOperators.cpp:93`
  (`step = frequencyHz * TwoPi / sampleRate`). With rate 0, `step` is `+inf`, and frame 0 computes
  `inf * 0 = NaN`. Every later frame is `sin(inf) = NaN`.
- The fix is a guard at the top of `tone()`, `src/core/SampleOperators.cpp:89-91`:
  `if (sampleRate <= 0) { return out; }`. A tone at a non-positive rate yields no frames. It
  does not yield silence and it does not yield NaN.
- The contract is documented in `src/core/SampleOperators.h:55-58`. The other kinds (silence,
  white, pink) ignore the rate and are unchanged.

**Reachability, stated plainly.** The only production caller is `sample.generate`
(`src/core/ControlCommandsSample.cpp:262`). It cannot reach the defect: `sizeFrames()` refuses a
tone unless `0 < frequency < sampleRate * 0.5` (`src/core/ControlCommandsSample.cpp:145-146`), and
with rate 0 that interval is empty. So the defect is reachable only by calling the helper directly.
This is the same conclusion as the earlier note in `docs/bunny-fixes-2026-10-05/engine/batch-14.md`
(cluster 3512, FALSE-POSITIVE), but that note treated a guard in the helper as dead code. This fix
takes the position that the helper is the cause, and a helper that returns NaN for an input its
own contract does not exclude is the defect. The command boundary is left unchanged.

**Caller and sibling check.**

- `generate()` has one caller, `sample.generate` (`src/core/ControlCommandsSample.cpp:262`), which
  is unchanged. Its `sample_rate` result still comes from `Engine::audioEngine()->outputSampleRate()`.
- The sibling kinds (`silence`, `white`, `pink`) do not read the rate. They are proved unchanged by
  the test (16 frames at rate 0).
- No new control command was added. The canary and `ControlCommandsSnapshot` are not affected.
- The change is control-thread only (the header says so, `src/core/SampleOperators.h:44-47`).
  Nothing on the audio thread is touched, and the guard neither allocates nor locks.

## Regression test

`tests/src/core/ControlSampleOperatorTest.cpp:617-626`, slot
`toneWithoutAPositiveSampleRateGeneratesNoFrames`. It is an existing registered test file, so no
new file is registered. It checks:

1. tone at rate 0 yields an empty vector,
2. tone at rate -44100 yields an empty vector,
3. tone at rate 44100 yields 16 frames, all finite,
4. silence, white and pink at rate 0 still yield 16 frames.

The `#include "../../../src/core/SampleOperators.h"` line follows the style of
`tests/src/core/DeviceDspTest.cpp:149`. No wall-clock time, sleep or race is involved.

## Commands run and exit codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-354.r1/logs/`.

**Environment workaround.** Build commands were run with `CCACHE_DISABLE=1`. Without it, the first
build failed (`build-1-red.log`, EXIT=2) because ccache could not create a temp file under
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied). This is a
write-jail limit, not a code error. The variable was set on the command line only and the config
was not changed.

| Step | Command (run from the worktree or `build/tests`) | Log | EXIT |
|---|---|---|---|
| Build, first try | `cmake --build build --target ControlSampleOperatorTest -j2` | build-1-red.log | 2 (ccache temp file, see above) |
| Build, second try | `CCACHE_DISABLE=1 cmake --build build --target ControlSampleOperatorTest -j2` | build-2-red.log | 2 (test include path, `SampleOperators.h` not found) |
| Build, RED | `CCACHE_DISABLE=1 cmake --build build --target ControlSampleOperatorTest -j2` | build-3-red.log | 0 |
| **RED** (no fix) | `ctest -R ControlSampleOperatorTest --output-on-failure` in `build/tests` | test-1-red.log | **8** (`FAIL! ... a tone at rate 0 produced 16 frames`; 14 passed, 1 failed) |
| Build, GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene ControlSampleOperatorTest -j2` | build-4-green.log | 0 |
| **GREEN** (fix) | `ctest -R ControlSampleOperatorTest --output-on-failure` in `build/tests` | test-2-green.log | **0** |
| Save fix | `git diff -- src/core/SampleOperators.cpp src/core/SampleOperators.h > fix.patch` | fix.patch | 0 |
| Reverse fix | `git apply -R fix.patch` | apply-reverse.log | 0 |
| Build, unfixed | `CCACHE_DISABLE=1 cmake --build build --target zene ControlSampleOperatorTest -j2` | build-5-4b-unfixed.log | 0 |
| **4b RED** (unfixed) | `ctest -R ControlSampleOperatorTest --output-on-failure` | test-3-4b-unfixed.log | **8** (`FAIL! ... produced 16 frames`) |
| Re-apply fix | `git apply fix.patch` | apply-forward.log | 0 |
| Build, fixed | `CCACHE_DISABLE=1 cmake --build build --target zene ControlSampleOperatorTest -j2` | build-6-4b-fixed.log | 0 |
| **4b GREEN** (fixed) | `ctest -R ControlSampleOperatorTest --output-on-failure` | test-4-4b-fixed.log | **0** |
| Final-text proof, RED | (same steps with the compacted test text, reverse, build, run) | test-5-final-unfixed-RED.log | **8** (`'sampleops::generate(sampleops::Kind::Tone, 440.0, 1.0f, 16, 0).empty()' returned FALSE`) |
| Final-text proof, GREEN | (apply, build, run) | test-6-final-fixed-GREEN.log | **0** |
| Family, 9 extra binaries built | `CCACHE_DISABLE=1 cmake --build build --target ControlSampleArrayTest ControlSampleStereoTest SampleBufferResampleTest SampleClipWarpModeMenuTest SampleClipStretchTest SampleClipRubberBandTest SampleClipWarpModesTest SampleClipWarpTest SampleClipWindowTest -j2` | build-7-family.log | 0 |
| Family, final | `ctest -R "ControlSample\|SampleClip\|SampleBuffer" -j2` in `build/tests` | test-7-family-final.log | **0**, 10/10 passed, none Not Run |
| Test cases, verbose | `ctest -R ControlSampleOperatorTest -V` | test-8-cso-verbose.log | 0, `Totals: 15 passed, 0 failed` |

Notes on the table:

- The first family run (`test-5-family.log`, EXIT=8) had 9 of 10 members "Not Run" because their
  binaries were not built. They were built at `build-7-family.log` and then passed.
- The RED message in the final test is shorter (`returned FALSE. ()`) than the one in the
  earlier test text, because it is now a bare `QVERIFY`. The failing expression is in the message.
- The 4b proof was run on the final test text, not only the earlier one.

**Gates.** `feature_check.py` itself could not complete. It exits 1 before running any gate, with
`PermissionError` when it creates `zene-qwen-new-p10-354-feature-check` in the parent folder
(`projects/zene-studio-program/`), which is outside the write-jail. The script was not edited.
Its gate commands were run one by one from the worktree, with logs in the LOG FOLDER:

| Gate | Command | Log | EXIT |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources-final.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-reproduce-final.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-regression-final.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace-final.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology-final.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures-final.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-complexity-final.log | 1 (7 regression lines, none in a changed file) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-file-length-final.log | 1 (22 regression lines, none in a changed file) |

The two ratchet gates exit 1 on the base too: the regression lines are in other files, and they
are red on the train on purpose. `feature_check.py` counts a ratchet row as PASS when no regression
line names a file this branch changed. Checked directly: zero regression lines name
`SampleOperators.cpp`, `SampleOperators.h` or `ControlSampleOperatorTest.cpp`. This is an
inference from the script's rule, not a run of the script's own table.

**File length, exact.** The file-length gate counts code lines (blank and comment-only lines
excluded). `tests/src/core/ControlSampleOperatorTest.cpp` was 489 code lines on the base, and my
first version of the test made it 512, which the gate reported as a new file over 500. The test
was compacted to 11 added code lines, which brings the file to exactly 500, the limit. The gate
allows that. The file has no room left for more code.

## Files committed

- `src/core/SampleOperators.cpp` (guard in `tone()`)
- `src/core/SampleOperators.h` (contract comment)
- `tests/src/core/ControlSampleOperatorTest.cpp` (regression slot and include)
- `docs/qwen-fixes-2026-10/cluster-new-p10-354.md` (this report)

No new files are added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no
change. `ControlSampleOperatorTest.cpp` is already registered in `tests/fork-sources.txt:2386`.

## Notes for the owner

- The ccache temp-directory issue (`dsh/home/ccache-tmp-r6`) blocks builds in this write-jail
  unless `CCACHE_DISABLE=1` is set. This lane worked around it on the command line only.
- `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is not writable
  from a lane worktree. A lane cannot run it to completion as written.
