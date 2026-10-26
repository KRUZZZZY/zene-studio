Verdict: FIXED

Cluster new-p21-205: `LadspaControls::updateLinkStatesFromGlobal` divides by `m_processors` without a zero check.

## Evidence

- Before the fix, `plugins/LadspaEffect/LadspaControls.cpp:215` and `:222` evaluated `m_controlCount / m_processors` in the loop conditions with no zero check. `m_processors` is `ch_cnt_t` (`include/LmmsTypes.h:44`), so the division is integer division and traps (SIGFPE) on zero.
- Zero processors is reachable in production: `plugins/LadspaEffect/LadspaEffect.cpp:334` sets `m_processors = DEFAULT_CHANNELS / effect_channels`. `DEFAULT_CHANNELS` is 2 (`include/lmms_constants.h:38`). The check at `LadspaEffect.cpp` before it rejects only `effect_channels == 0`, so a plugin with three or more inputs yields 0 processors and is not rejected. The sub-plugin browser filters these out (`LadspaSubPluginFeatures.cpp:145`), but a saved sub-plugin key can still name one.
- Only caller of `updateLinkStatesFromGlobal`: the `dataChanged` connection in the constructor (`LadspaControls.cpp:53`, DirectConnection) and the test seam `setGlobalLinkState`. It runs on model change (UI or settings load), not in the audio processing path.
- Fix: `LadspaControls.cpp:211-221` computes `portCount = m_processors > 0 ? m_controlCount / m_processors : 0` once, and both loops use it. The `m_noLink = false` reset at the end is kept, so state is unchanged when processors >= 1.
- Regression: `tests/src/plugins/LadspaGlobalLinkTest.cpp` gains two test functions, `globalStereoLinkOfAPluginWithoutProcessorsWalksNoPortsAndDoesNotDivideByZero` (controls `(0, 4)`) and `globalStereoLinkOfAPluginWithoutProcessorsOrControlsDoesNotDivideByZero` (controls `(0, 0)`). They use the existing test seam (`setLinkPortVisitor`, `LadspaControls(ch_cnt_t, int)`), with no timing, sleeps or threads.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-205.r1/logs`

Build note: ccache could not write its temp files from this sandbox (`build-1-red.log`, EXIT=2, `ccache: error: failed to create temporary file ... Permission denied`). Every later build used `CCACHE_DISABLE=1` to skip ccache. Sources and compiler flags are unchanged.

| Step | Command (abbreviated) | Log | EXIT |
| --- | --- | --- | --- |
| RED build (unfixed product, new tests) | `CCACHE_DISABLE=1 cmake --build build --target LadspaGlobalLinkTest -j4` | `build-2-red.log` | 0 |
| RED run | `cd build/tests && ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | `test-2-red.log` | 8 |
| GREEN build (fixed) | `CCACHE_DISABLE=1 cmake --build build --target LadspaGlobalLinkTest -j4` | `build-3-green.log` | 0 |
| GREEN run | `ctest -R '^LadspaGlobalLinkTest$' --output-on-failure` | `test-3-green.log` | 0 |
| GREEN run, verbose | `ctest -R '^LadspaGlobalLinkTest$' -V` | `test-4-green-verbose.log` | 0 |
| 4b: save diff | `git diff -- plugins/LadspaEffect/LadspaControls.cpp > logs/fix.patch` | - | 0 |
| 4b: revert fix | `git apply -R logs/fix.patch` | - | 0 |
| 4b: rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target LadspaGlobalLinkTest -j4` | `build-4b-unfixed.log` | 0 |
| 4b: run unfixed | `ctest -R '^LadspaGlobalLinkTest$' -V` | `test-4b-unfixed.log` | 8 |
| 4b: reapply fix | `git apply logs/fix.patch` | - | 0 |
| 4b: rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target LadspaGlobalLinkTest -j4` | `build-4b-fixed.log` | 0 |
| 4b: run fixed | `ctest -R '^LadspaGlobalLinkTest$' -V` | `test-4b-fixed.log` | 0 |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target LadspaChannelCountsTest LadspaManagerTest LadspaPortDialogTest -j4` | `build-5-family.log` | 0 |
| Family run | `ctest -R 'Ladspa' -V` | `test-5-family.log` | 0 |
| Product build | `CCACHE_DISABLE=1 cmake --build build --target zene -j4` | `build-6-zene.log` | 0 |
| Gate (harness) | `python3 .../qwen/feature_check.py <worktree>` | `gate-1-feature-check.log` | 1 (see below) |
| Completion check | `python3 .../qwen/submit_check.py <worktree> new-p21-205` | `gate-2-submit-check.log` | 1 (see below) |

Test counts, from the verbose runs (`test-4-green-verbose.log`, `test-5-family.log`):

- RED (`test-2-red.log`): `initTestCase`, `theControlTotalOfASixtyFiveThousandControlPluginIsNotTruncated`, `globalStereoLinkCoversEveryPortOfASixtyFiveThousandControlPlugin` and `globalStereoLinkCoversThePortsOfAPluginBelowTheSixteenBitLimit` PASS. `globalStereoLinkOfAPluginWithoutProcessorsWalksNoPortsAndDoesNotDivideByZero` then dies with `Received signal 8 (SIGFPE), code FPE_INTDIV` at `ctest` EXIT 8.
- GREEN: `LadspaGlobalLinkTest` Totals 7 passed, 0 failed (`test-4-green-verbose.log:27`).
- RED with the fix reverted (`test-4b-unfixed.log:24`): the same SIGFPE, EXIT 8.
- Ladspa family (`ctest -R 'Ladspa'`, `test-5-family.log`): 4 of 4 tests pass, 100%. LadspaChannelCountsTest 4 passed, LadspaManagerTest 5 passed, LadspaPortDialogTest 4 passed, LadspaGlobalLinkTest 7 passed.

## Gates

`feature_check.py` could not run from this sandbox: it creates `<worktree>-feature-check/` next to the worktree (`feature_check.py:71`), and that sibling directory is outside the write-jail (`PermissionError`, `gate-1-feature-check.log`). I ran the same gate commands by hand, from the worktree, with the logs in the log folder:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 |

The two ratchet gates are red on the train on purpose. None of their 7 complexity or 19 file-length `REGRESSION:` lines names `LadspaControls.cpp` or `LadspaGlobalLinkTest.cpp` (`gate-complexity.log`, `gate-file-length.log`), so no item is new in the files this change touches. The harness's ratchet baseline cache sits outside the jail, so this is a manual reading and not the harness's own PASS row.

`submit_check.py` reported FAIL at the time of the run for: nothing committed yet, the tree dirty, this report missing, the Self-check section missing, and the gates row (`gate-2-submit-check.log`). The first four clear with the commit; the gates row is the sandbox limit above.

## Scope

- Fixed here: `LadspaControls::updateLinkStatesFromGlobal`, the cause in the finding.
- Not fixed here, sibling with the same unguarded division: `plugins/LadspaEffect/LadspaControlDialog.cpp:82-83` (`updateEffectView`, called from the dialog constructor at `:56`) computes `m_controlCount / m_processors`. With zero processors the real constructor gives `m_controlCount == 0` (the controls are appended only inside the per-processor loop at `LadspaEffect.cpp:347`, `:509-510`; the nesting is inferred from the loop header and not read end to end), so this would be `0/0` when the effect view opens. I did not change it: no built test covers `LadspaControlDialog` (the copy under `tests/reference/` is not compiled), so there is no deterministic RED for it in this lane. It needs its own cluster with a GUI-capable test target.
- Not changed on purpose: `LadspaEffect.cpp:334` still accepts zero processors. Rejecting them would change loading of saved projects (rule 4). The guard at the divisions keeps the existing load behaviour and only changes the zero-processor case, which previously crashed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: `test-2-red.log`, EXIT 8, SIGFPE in the new test. 4b with `git apply -R`: `test-4b-unfixed.log`, EXIT 8. With the fix reapplied: `test-4b-fixed.log`, EXIT 0, 7 passed. Exit codes are listed in the table above.
- **The test is deterministic.** It uses the existing visitor seam (`LadspaControls::setLinkPortVisitor`), explicit constructor values `(0, 4)` and `(0, 0)` and synchronous `setGlobalLinkState` calls. No timing, sleep, thread or race.
- **The fix is at the cause and complete.** The only caller of `updateLinkStatesFromGlobal` is the constructor connection (`LadspaControls.cpp:53`) plus the test seam. Both divisions in the function are replaced by one `portCount`. `linkPort` and `applyLinkToPort` do not divide. The sibling in `LadspaControlDialog.cpp:82` is reported above and not changed.
- **No collateral damage.** With processors >= 1 the computed port count is the same `m_controlCount / m_processors` as before, and `m_noLink = false` still runs at the end. Saved projects load as before because nothing changed in loading (`LadspaEffect.cpp:334` is untouched). The only changed behaviour is for zero processors, which previously trapped.
- **Not ALREADY-FIXED or FALSE-POSITIVE.** Not applicable; the verdict is FIXED with the evidence above.

## Files committed

- `plugins/LadspaEffect/LadspaControls.cpp`
- `tests/src/plugins/LadspaGlobalLinkTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p21-205.md`

No new source files, so no `tests/fork-sources.txt` entry is needed. `LadspaControls.cpp` already has its `tests/upstream-modifications.txt` entry (line 4860), and `no-upstream-regression` passed.
