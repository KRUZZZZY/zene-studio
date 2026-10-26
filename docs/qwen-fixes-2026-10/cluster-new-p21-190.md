# Cluster new-p21-190: InputMeter take and live dereference null outputs

**Verdict: FIXED**

`inputmeter::take` and `inputmeter::live` wrote `*left` and `*right` unconditionally, so a null output faulted (SIGSEGV). Both now skip a side whose output pointer is null, as the sibling `inputmeter::measure` already guards its null buffer. A skipped side is not read, so its held peak stays for the next reader.

The crash is latent: no caller in the current tree passes null. The two callers pass locals:

- `src/core/ControlCommandsMixerTools.cpp:392` calls `inputmeter::take(&left, &right)`.
- `src/gui/InputLevelMeter.cpp:89` calls `inputmeter::live(&left, &right)`.

## Evidence

- Fix, `src/core/InputMeter.cpp:93-103`: `take` and `live` each read a side only when its pointer is non-null, `:96-97` and `:102-103`. The sibling guard is `src/core/InputMeter.cpp:79` in `measure`.
- Contract, `include/InputMeter.h:48-55`: a null output is documented as not read, with the peak held.
- Regression, `tests/src/core/InputMeterTest.cpp:148-180`: `aNullOutputSkipsItsSideOfTheTake` (`:151`) and `aNullOutputSkipsItsSideOfTheLiveRead` (`:167`).
- Realtime: `take` and `live` run on the command and GUI threads, not the audio thread (see the callers above). `measure` is untouched, and `measuringAllocatesNothing` still passes.

## Design choice for review

A null side is skipped entirely, so its peak is held for the next reader and is not consumed and discarded. The alternative, consuming the peak and throwing it away, would lose a reading the caller never asked for. Reviewers can redirect this; the test pins the held-peak behaviour.

## Evidence table

All builds and tests ran from the worktree, with `CCACHE_TEMPDIR` set to a writable directory (see the first build below). ctest ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-190.r1/logs/`.

| Check | Exact command | EXIT |
| --- | --- | --- |
| Build attempt, default ccache temp dir | `cmake --build build --target InputMeterTest -j4 > build-1-red.log 2>&1` | **2** (ccache could not write its temp file under `dsh/home/ccache-tmp-r6`: Permission denied; environmental, no code error) |
| Build, RED, with ccache temp dir set | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p21-190 cmake --build build --target InputMeterTest -j4 > build-2-red.log 2>&1` | **0** |
| RED run, before fix | `ctest -R '^InputMeterTest$' --output-on-failure > test-1-red.log 2>&1` | **8** (SEGFAULT; crash in `InputMeterTest::aNullOutputSkipsItsSideOfTheTake`) |
| Build, GREEN | `cmake --build build --target InputMeterTest -j4 > build-3-green.log 2>&1` | **0** |
| GREEN run | `ctest -V -R '^InputMeterTest$' > test-3-green-verbose.log 2>&1` | **0** (`Totals: 9 passed, 0 failed`) |
| Build of consumers after header/source change | `cmake --build build --target zene -j4 > build-4-zene.log 2>&1` | **0** |
| 4b: reverse the fix | `git diff -- src/core/InputMeter.cpp include/InputMeter.h > fix.patch && git apply -R fix.patch` | **0** |
| 4b: rebuild reverted | `cmake --build build --target InputMeterTest -j4 > build-5-reverted.log 2>&1` | **0** |
| 4b: RED with fix reverted | `ctest -V -R '^InputMeterTest$' > test-4-reverted-RED.log 2>&1` | **8** (SEGFAULT in `aNullOutputSkipsItsSideOfTheTake`, same as before the fix) |
| 4b: reapply the fix | `git apply fix.patch` | **0** |
| 4b: rebuild reapplied | `cmake --build build --target InputMeterTest -j4 > build-6-reapplied.log 2>&1` | **0** |
| 4b: GREEN with fix reapplied | `ctest -V -R '^InputMeterTest$' > test-5-reapplied-GREEN.log 2>&1` | **0** (`9 passed, 0 failed`) |
| Affected family: InputMeterTest | `ctest -V -R '^InputMeterTest$'` | **0**, 1 target executed, 9 passed |
| Consumer family: MixerMeteringTest | `cmake --build build --target MixerMeteringTest -j4 > build-7-mixermetering.log 2>&1`, then `ctest -V -R '^MixerMeteringTest$' > test-6-mixermetering.log 2>&1` | build **0**, ctest **0** (`23 passed, 0 failed`) |
| Gate script, as specified | `python3 .../qwen/feature_check.py <worktree> > gates-1.log 2>&1` | **1**: `PermissionError` creating `<worktree>-feature-check/` beside the worktree, outside the write-jail. Not a gate result. |
| Gate, fork-sources (run by hand, same command) | `bash tests/fork-sources-gate.sh > gate-fork-sources.log 2>&1` | **0** |
| Gate, all-sources-reproduce (by hand) | `bash tests/all-sources-reproduce.sh > gate-all-sources-reproduce.log 2>&1` | **0** |
| Gate, no-upstream-regression (by hand) | `bash tests/no-upstream-regression-gate.sh > gate-no-upstream-regression.log 2>&1` | **0** |
| Gate, check-namespace (by hand) | `tests/scripted/check-namespace > gate-check-namespace.log 2>&1` | **0** |
| Gate, no-tautology (by hand) | `bash tests/no-tautology-gate.sh > gate-no-tautology.log 2>&1` | **0** |
| Gate, doc-figures (by hand) | `python3 tests/doc-figures-gate.py > gate-doc-figures.log 2>&1` | **0** |
| Ratchet, complexity (by hand) | `bash tests/complexity-gate.sh --check > gate-complexity.log 2>&1` | raw **1**; see below |
| Ratchet, file-length (by hand) | `bash tests/file-length-gate.sh --check > gate-file-length.log 2>&1` | raw **1**; see below |
| Ratchet rule applied | `feature_check.py`'s own comparison, run read-only against the cached base `cf3618e61`: no REGRESSION line is an item in a changed file | complexity: 7 REGRESSION lines, 0 in changed files; file-length: 19 REGRESSION lines, 0 in changed files. **PASS** by the rule |

The `feature_check.py` run itself did not complete, so "gates pass" is established only by running its six simple gates and its two ratchets by hand, with the same commands and the same comparison. The script's log-folder path is outside the write-jail, which is why it failed.

Files changed (explicit paths):

- `src/core/InputMeter.cpp`
- `include/InputMeter.h`
- `tests/src/core/InputMeterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p21-190.md`

All three product and test files are already registered in `tests/fork-sources.txt` and `tests/all-sources.txt`; the files are fork-owned, so no `tests/upstream-modifications.txt` row applies.

## Self-check

- **The regression test fails with the product change reverted and passes with it.** RED: `test-1-red.log`, ctest EXIT=8, SIGSEGV in `aNullOutputSkipsItsSideOfTheTake`. Reverted: `test-4-reverted-RED.log`, ctest EXIT=8, the same crash. Reapplied: `test-5-reapplied-GREEN.log`, ctest EXIT=0, 9 passed.
- **The test is deterministic.** It has no timing, sleeps, threads or races. It calls `measure` on a fixed `period()` buffer and then calls `take` or `live` directly, with null and non-null outputs. The values are exact constants (0.5, 0.25).
- **The fix is at the cause and complete.** The cause is the unconditional store through each output pointer. Both functions are changed, and the sibling guard in `measure` (`src/core/InputMeter.cpp:79`) matches the convention. Both callers pass non-null locals (`ControlCommandsMixerTools.cpp:392`, `InputLevelMeter.cpp:89`), so their behaviour does not change. The header contract is updated at `include/InputMeter.h:48-55`.
- **No collateral damage.** For non-null pointers, each side does the same `exchange` in the same order as before, so the existing `InputMeterTest` cases and the `MixerMeteringTest` consumer pass (23/23). No saved data or project format is involved, and no limit or check was added.
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.** The finding's trigger (a caller passing null) does not occur in the current tree, and the report says so: the defect is latent.

The `submit_check.py` gate item will still fail for the `feature_check.py` reason above (the write-jail blocks its log folder). That is an environment limit, and I did not work around it by changing the gate.
