Verdict: FIXED

new-p21-142: sfizz `EffectBus::effectView` read one slot past the end of its effect list

## The defect

`plugins/SfzPlayer/sfizz/src/sfizz/Effects.cpp:105` (before the fix) checked `if (index > _effects.size()) return {};`
and then returned `_effects[index].get()` at line 108. An index equal to `size()` passed the check and read the slot
past the last element. For an empty bus (`size() == 0`) index 0 dereferences the vector's null storage.

The finding's claim that "any caller" hits this is latent: a grep of the worktree finds no caller of `effectView` outside
its declaration (`plugins/SfzPlayer/sfizz/src/sfizz/Effects.h:107`) and definition. The defect is real and reachable from
any future host of the sfizz bus, but nothing in Zene calls it today.

## The fix

One comparison: `plugins/SfzPlayer/sfizz/src/sfizz/Effects.cpp:105` changed from `>` to `>=`. Index equal to the effect
count now returns `nullptr`, the same as any out-of-range index. Other callers: none. The sibling `numEffects()` and
`addEffect()` are unchanged. The file is vendored sfizz; the change is registered in `tests/upstream-modifications.txt`
(new line for `Effects.cpp`), so it is not silently an inherited-file edit.

## The regression test

`tests/src/plugins/SfzEffectBusViewTest.cpp` (new), registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`tests/all-sources.txt`. It links the build's static `sfzplayer_sfizz` library and calls `sfz::EffectBus` directly. The
test does not use the two suggested tests (`EffectSelectDialogFilterTest`, `ScriptDawEffectsTest`): they cover Zene's
GUI and script layers and have nothing to do with the sfizz bus.

Cases:

- `emptyBusHasNoEffectAtIndexZero` - empty bus, `effectView(0)` must be `nullptr`. Deterministic RED: SIGSEGV at address 0.
- `indexEqualToEffectCountIsRejected` - two effects, `effectView(2)` must be `nullptr`. RED: FAIL (non-null returned).
  Note: this reads heap memory past the end, so its RED is not guaranteed by the language; it failed here.
- `indexPastEffectCountIsRejected` - control (passes before and after).
- `inRangeIndexReturnsTheAddedEffect` - control (passes before and after).

## Evidence

All commands were run from the worktree root unless noted. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-142.r1/logs/`.

| Step | Command | Result |
| --- | --- | --- |
| RED build (1st attempt, missing `simde` include) | `cmake --build build --target SfzEffectBusViewTest -j4 > build-4-red-target.log` | make Error 2 (include error only) |
| RED build (after include fix) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SfzEffectBusViewTest -j4 > build-5-red-target.log` | EXIT=0 |
| RED run, all cases | `cd build/tests && QT_QPA_PLATFORM=offscreen ./SfzEffectBusViewTest > test-1-red-direct.log` | EXIT=139 (SIGSEGV in `emptyBusHasNoEffectAtIndexZero`) |
| RED run, `indexEqualToEffectCountIsRejected` alone | `./SfzEffectBusViewTest indexEqualToEffectCountIsRejected > test-1-red-indexEqualToEffectCountIsRejected.log` | EXIT=1 (`bus.effectView(2) == nullptr` returned FALSE, test line 57) |
| RED run, `indexPastEffectCountIsRejected` alone | `./SfzEffectBusViewTest indexPastEffectCountIsRejected > ...log` | EXIT=0 (control) |
| RED run, `inRangeIndexReturnsTheAddedEffect` alone | `./SfzEffectBusViewTest inRangeIndexReturnsTheAddedEffect > ...log` | EXIT=0 (control) |
| GREEN build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SfzEffectBusViewTest -j4 > build-6-green-target.log` | EXIT=0 |
| GREEN run | `cd build/tests && ./SfzEffectBusViewTest > test-2-green.log` | EXIT=0, 6 passed, 0 failed |
| 4b: save fix | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/Effects.cpp > fix.patch` | exit not captured separately; file contains the one-line change |
| 4b: revert fix | `git apply -R fix.patch` | empty `git diff` afterwards (checked) |
| 4b: reverted build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SfzEffectBusViewTest -j4 > build-7-proof-reverted.log` | EXIT=0 |
| 4b: reverted run | `./SfzEffectBusViewTest > test-3-proof-reverted.log` | EXIT=139 (SIGSEGV) |
| 4b: reapply fix | `git apply fix.patch` | `git diff` shows the `>=` line again |
| 4b: reapplied build | `... > build-8-proof-reapplied.log` | EXIT=0 |
| 4b: reapplied run | `./SfzEffectBusViewTest > test-4-proof-reapplied-direct.log` | EXIT=0, 6 passed |
| ctest family (1 test) | `cd build/tests && ctest -R '^SfzEffectBusViewTest$' --output-on-failure > test-5-ctest.log` | EXIT=0, 100% passed (1/1) |

Affected family: `SfzEffectBusViewTest` is the only test that exercises `EffectBus::effectView`; its family count is 1.
No other test in the tree references the sfizz bus.

## Gates

`feature_check.py` cannot run from this lane: it writes its logs to `<worktree-parent>/<name>-feature-check`, which the
write-jail forbids. The run's stderr is in `gate-feature-check-1.log` (`PermissionError`, EXIT=1). The same gate
scripts were run by hand from the worktree, each to its own log:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > gate-fork-sources.log` | 1 | Manifest must be re-derived after this change is committed (the recipe reads `git diff ... HEAD`); see the commit that follows. |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > gate-all-sources.log` | 1 | Same: the recipe reads `git ls-files`, so the untracked test file is absent until committed. |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > gate-no-upstream-regression.log` | 0 | |
| check-namespace | `tests/scripted/check-namespace > gate-check-namespace.log` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh > gate-no-tautology.log` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py > gate-doc-figures.log` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > gate-complexity.log` | 1 | 7 train-wide REGRESSION lines, none in a file this branch changed (checked by name). |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > gate-file-length.log` | 1 | 19 train-wide REGRESSION lines, none in a file this branch changed (checked by name). |

The complexity and file-length exits are the train-wide red the task describes. By `feature_check`'s rule (only items in
changed files count) they pass.

## Self-check

- **Fails with the product change reverted, passes with it.** Yes. Reverted (`git apply -R` of `fix.patch`): empty-bus
  case SIGSEGV, EXIT=139 (`test-3-proof-reverted.log`); index==size case FAIL, EXIT=1 when run alone
  (`test-1-red-indexEqualToEffectCountIsRejected.log`). Fix applied: 6 passed, EXIT=0 (`test-2-green.log`,
  `test-4-proof-reapplied-direct.log`); ctest `^SfzEffectBusViewTest$` 1/1, EXIT=0 (`test-5-ctest.log`).
- **Deterministic: partly.** The empty-bus case is deterministic: it reads address 0 without the fix, on every platform.
  The index==size case reads the slot past the vector's end, which the language leaves undefined; its RED happened to
  return non-null here, but nothing in the test forces that. The test uses no wall-clock, sleeps, threads or injected
  seams: every value is constructed in the test (fixed effect counts and pointers returned by `addEffect` for identity).
  I did not add a sanitizer or `_GLIBCXX_ASSERTIONS` build to make the index==size RED guaranteed; the empty-bus case
  carries the deterministic guarantee.
- **At the cause, complete.** The comparison at `Effects.cpp:105` is the only bounds check in `effectView`. Callers:
  none in the tree (`rg -n effectView` over the worktree: only `Effects.h:107` and `Effects.cpp:103`). Siblings
  `numEffects()` (`Effects.h`) and `addEffect()` (`Effects.cpp:98`) do not index the vector. `operator[]` on
  `_effects` elsewhere is not reachable from `effectView`.
- **No collateral damage.** The only behaviour change is that an index equal to the effect count now returns `nullptr`
  instead of reading one slot past the end. No caller legitimately passes that index (there are none), no saved data
  or project format involves the sfizz bus (it is in-memory only), and no limit was added.
- **Verdict is FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE evidence item does not apply.
- **Feature-check gate: not run as `feature_check.py`.** It exits 1 with `PermissionError` creating
  `<worktree-parent>/zene-qwen-new-p21-142-feature-check` (`gate-feature-check-1.log`), outside the write-jail. The gate
  scripts it calls were run by hand (table above); after committing the test, `fork-sources` and `all-sources-reproduce`
  now exit 0 (`gate-fork-sources-2.log`, `gate-all-sources-2.log`). The complexity and file-length ratchets stay
  red train-wide, with no item in a changed file.
