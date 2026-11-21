# Cluster new-p21-35

Verdict: **FALSE-POSITIVE**

Luna's finding says `MixerSnapshots::Parts parts;` in `recallSnapshot()` is left indeterminate when `parts` is omitted. It is not. `Parts` declares every member with a default initializer of `true`, so the default-constructed value is all-true, and that is exactly the "every part when left out" contract the code documents. No product change is needed; a fix would be a no-op.

## File evidence

- [include/MixerSnapshots.h:79](include/MixerSnapshots.h:79): `struct Parts` declares `volume`, `pan`, `mute`, `solo`, `sends` each as `bool ... = true;` (lines 81-85). A default-constructed `Parts` is therefore all-true; no member is indeterminate.
- [src/core/ControlCommandsMixerSnapshots.cpp:57](src/core/ControlCommandsMixerSnapshots.cpp:57): the helper's comment states the contract: the parts `args` names, "every part when left out".
- [src/core/ControlCommandsMixerSnapshots.cpp:60](src/core/ControlCommandsMixerSnapshots.cpp:60): when `parts` is absent `partsOf` returns `true` without writing `*parts`, so the caller's default-initialized value is used. This is the intended path, not a missing assignment.
- [src/core/ControlCommandsMixerSnapshots.cpp:156](src/core/ControlCommandsMixerSnapshots.cpp:156) and [:158](src/core/ControlCommandsMixerSnapshots.cpp:158): `MixerSnapshots::Parts parts;` followed by `partsOf(args, &parts, &error)`. Line 159 (the finding's cited line) is `const MixerSnapshot target = *found;`, not the `Parts` declaration; the finding's line number is stale.
- [src/core/ControlCommandsMixerSnapshots.cpp:161](src/core/ControlCommandsMixerSnapshots.cpp:161): `MixerSnapshots::apply(target, parts)` receives the all-true value when `parts` is omitted.
- [src/core/ControlCommandsMixerSnapshots.cpp:162](src/core/ControlCommandsMixerSnapshots.cpp:162): the undo step uses `MixerSnapshots::Parts{}`, which is also all-true, so undo restores every part of the captured mixer state, as intended.
- [src/core/ControlCommandsMixerSnapshots.cpp:213](src/core/ControlCommandsMixerSnapshots.cpp:213)-[:214](src/core/ControlCommandsMixerSnapshots.cpp:214): the registered command documents "every part, or only the parts named".
- [include/MixerSnapshots.h:68](include/MixerSnapshots.h:68): the class contract says recall "writes the parts it is asked for"; omitting `parts` means asking for all of them.
- [tests/src/core/ProjectStateTest.cpp:122](tests/src/core/ProjectStateTest.cpp:122)-[:125](tests/src/core/ProjectStateTest.cpp:125): the existing test recalls `Verse` with no `parts` argument and asserts the muted channel is unmuted (`!m_muteModel.value()`) and the send level is restored (`0.25f`). That is the "parts omitted" path the finding says is broken, and it passes on this tree (see test-1 below).

## Evidence

Commands run from the worktree. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-35.r1/logs/`.

| Check | Command | Exit | Result |
|---|---|---:|---|
| Build, first attempt | `cmake --build build --target zene ProjectStateTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | 2 | Failed in ccache, not in the compiler: `ccache: error: failed to create temporary file ... Permission denied` (ccache temp dir points at a path outside this sandbox). No source error. |
| Build, retry | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p21-35 cmake --build build --target zene ProjectStateTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | 0 | `zene` and `ProjectStateTest` built. The ccache override applies to this command only; nothing persistent changed. |
| Affected test | `cd build/tests && ctest -R ProjectStateTest --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed, 0 tests failed out of 1`. The one CTest target covers `ProjectStateTest`, including the recall-without-`parts` assertions at lines 122-125. |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | 0 | PASS |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh > .../gate-all-sources.log 2>&1` | 0 | PASS |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > .../gate-no-upstream.log 2>&1` | 0 | PASS |
| Gate: check-namespace | `tests/scripted/check-namespace > .../gate-check-namespace.log 2>&1` | 0 | PASS |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh > .../gate-no-tautology.log 2>&1` | 0 | PASS |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py > .../gate-doc-figures.log 2>&1` | 0 | PASS |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | 1 | Train-wide grandfathered list (67 functions over CCN 10 in the fork scope). None is in `MixerSnapshots`, `ControlCommandsMixerSnapshots` or `ProjectState` (grep of the log: no match). This branch changes no product file, so no item is new in a changed file. |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | 1 | Train-wide list (25 files over 500 code lines). Same reasoning: no changed file is listed. |
| `feature_check.py` | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gate-1.log 2>&1` | 1 | Could not run: it creates `<worktree>-feature-check/` next to the worktree, outside the write-jail (`PermissionError`, `gate-1.log`). The eight gate commands above are the same commands it runs, executed directly. |

Not run: a RED/GREEN regression and the 4b reversal. Both apply only to a real defect with a fix. This verdict is supported by the code and by the passing existing test.

## Self-check

- **Regression test fails with the product change reverted and passes with it:** not applicable. The verdict is FALSE-POSITIVE, so there is no product change to revert and no new test. The existing `ProjectStateTest` passes on the current tree (test-1, EXIT=0), and its recall-without-`parts` assertions (`tests/src/core/ProjectStateTest.cpp:122-125`) are the proof that this path behaves as documented.
- **Test is deterministic:** not applicable; no test was added or changed.
- **Fix at the cause and complete:** not applicable; no fix. Every use of `Parts` in the path was checked: `partsOf` is called only at `src/core/ControlCommandsMixerSnapshots.cpp:158`, and the two `Parts` values that reach `MixerSnapshots::apply` (line 161, from the default-initialized `parts`, and line 162, from `Parts{}`) are both all-true (`include/MixerSnapshots.h:81-85`).
- **No collateral damage:** unchanged, because no product code changed. Projects and saved data are not touched by this branch.
- **FALSE-POSITIVE evidence:** the code is cited above (`include/MixerSnapshots.h:79-86`, `src/core/ControlCommandsMixerSnapshots.cpp:57-60`, `:156-161`, `:213-214`) and the intended contract is cited at `include/MixerSnapshots.h:68-69`.
- **Gate status:** `submit_check.py` reports `gates (feature_check.py)` as FAIL (`submit-1.log`) because `feature_check.py` cannot run here. It writes `<worktree>-feature-check/` beside the worktree, which is outside the write-jail (`gate-1.log`, `PermissionError`). I ran the same eight gate commands directly: the six simple gates exit 0, and complexity and file-length exit 1 on the train-wide ratchets, with no listed item in any file this branch touches. The gate runner itself has not been run to completion, so this FAIL stands until someone with access to that folder runs it.
- **Claim-only closure:** this report makes no product claim beyond the cited code and the passing existing test. Independent QA should re-verify `include/MixerSnapshots.h:79-86` and `tests/src/core/ProjectStateTest.cpp:122-125`.
