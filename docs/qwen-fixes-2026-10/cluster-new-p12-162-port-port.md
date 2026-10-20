# Cluster new-p12-162-port-port: Multitap Echo reads uninitialized low-pass entries

**Verdict: ALREADY-FIXED** - the newest line already removed the defect, so this
branch changes no product code. It adds the guard test that locks the fixed
behaviour in (the test is RED against the historical code and GREEN on this
line) and the report.

## The finding, and the state of the newest line

The finding (Luna's patrol, against the historical line) says:

> The initial graph has 16 steps, so it initializes `m_lpFreq[0..15]`;
> `updateFilters(0, 19)` also reads entries 16 through 19, which are
> uninitialized.

Both halves of that premise are false on this branch:

| The finding's premise | This line (file:line) |
| --- | --- |
| `updateFilters(0, 19)` in the constructor | `plugins/MultitapEcho/MultitapEcho.cpp:68` - the constructor calls `applyPendingControls( m_controls.m_steps.value() )`, i.e. the graph's own length, and there is no `updateFilters( 0, 19 )` call anywhere in the plugin |
| `m_lpFreq[16..19]` uninitialized | `plugins/MultitapEcho/MultitapEcho.h:70` - `float m_lpFreq [32]{};` is value-initialised, so no entry of the array is indeterminate |

The whole chain of the reading is closed on this line:

- `plugins/MultitapEcho/MultitapEchoControls.cpp:145-154` - `lpSamplesChanged()` publishes the graph's low-pass values into `m_pendingLpFreq` (an `std::atomic<float>` array, `plugins/MultitapEcho/MultitapEcho.h:68`, also value-initialised).
- `plugins/MultitapEcho/MultitapEcho.cpp:91-109` - `applyPendingControls( steps )` loops `for( int i = 0; i < steps; ++i )`, i.e. over the graph's own entries only, and writes `m_lpFreq[i]` and the tap's filter coefficients for them.
- `plugins/MultitapEcho/MultitapEcho.cpp:68` - the constructor calls it with `m_controls.m_steps.value()` (16 at the default graph length), so it touches `m_lpFreq[0..15]`.
- `plugins/MultitapEcho/MultitapEcho.cpp:78-88` - `updateFilters( begin, end )` reads `m_lpFreq[i]`, and it has exactly **one** caller: `plugins/MultitapEcho/MultitapEcho.cpp:142`, `updateFilters( 0, steps - 1 )`, with `steps = m_controls.m_steps.value()` (`plugins/MultitapEcho/MultitapEcho.cpp:128`). The entries it reads were written by `applyPendingControls( steps )` two lines earlier (`plugins/MultitapEcho/MultitapEcho.cpp:139`), and `steps` is at most 32 (`m_steps` is an `IntModel` with the range 4..32, `plugins/MultitapEcho/MultitapEchoControls.cpp:41`), which is the size of both arrays.

The defect and its cause were removed by commit `36bb0682a` ("Fix cluster
new-p12-160 Multitap Echo race"), which is an ancestor of this branch's HEAD
(`git merge-base --is-ancestor 36bb0682a HEAD` -> EXIT 0, `evidence-1.log`).
That commit replaced the constructor's `updateFilters( 0, 19 )` with
`applyPendingControls( m_controls.m_steps.value() )` and value-initialised
`m_amp[32]` / `m_lpFreq[32]`. The historical state is visible in the same log:
`36bb0682a^:plugins/MultitapEcho/MultitapEcho.cpp` has `updateFilters( 0, 19 );`
in the constructor and `36bb0682a^:plugins/MultitapEcho/MultitapEcho.h` has
`float m_lpFreq [32];` with no initialiser.

The accepted fix on the old line (`06da11335` in this job's history) clamped
`updateFilters()`'s end index at the graph's last step. On this line that
clamp would be a no-op: the only caller already passes `steps - 1`, which is
inside the graph. Re-applying it would change no behaviour, so this branch
does not touch the product.

### One place that still carries the historical pattern - and why it stays

`tests/reference/MultitapEcho/MultitapEcho.cpp:66` (the frozen upstream
reference the migration test compares the migrated plugin against) still has
`updateFilters( 0, 19 );` and `tests/reference/MultitapEcho/MultitapEcho.h:66`
still has an uninitialised `float m_lpFreq [32];`. That file is not the
product: it is the pre-migration baseline, and the migration test *expects* the
migrated plugin to differ from it (`tests/src/plugins/PluginPortsMigrationTest.cpp:1854-1859`,
"the fixed two-stage render must differ from the single-stage old reference").
It is a verbatim upstream copy (`tests/reference/ORIGIN.tsv`), so it was left
untouched - changing it would move the baseline the comparison is made against.

## What this branch adds

`tests/src/plugins/MultitapEchoBoundsTest.cpp` (new, fork-NEW): a guard test
for the fixed behaviour.

- The plugin sources are compiled into the test binary (the SlicerTZoomTest /
  DelayControlsTest pattern), so the effect is constructed **in the test** by a
  placement new into a byte-poisoned buffer. The bytes the constructor reads
  are the poison, so the outcome does not depend on the heap, the allocator, the
  loader or the platform: no runtime module is loaded and no allocator is
  interposed.
- `constructorLeavesTheTapsPastTheGraphLengthUntouched()` - the taps past the
  graph length (16..19) must keep the untouched `OnePole` default (a
  pass-through: an impulse comes back as it went in), and the taps inside the
  graph length must still carry their low-pass, so the first check cannot pass
  vacuously.
- `everyLowPassEntryIsInitialised()` - every low-pass entry is initialised
  before it is used; the entries are checked by their object representation
  (not by evaluating an indeterminate float as a value).

The test is deterministic: no wall-clock timing, no sleeps, no races, no
threads. Its only seam is the poisoned storage.

### Registration

- `tests/CMakeLists.txt` - the new `MultitapEchoBoundsTest` target (and the
  `PLUGIN_STATIC_DEFINE` the compiled-in plugin sources need).
- `tests/fork-sources.txt` - the new test source and this report (fork-NEW).
- `tests/all-sources.txt` - the new test source in the whole-tree manifest.
- `tests/upstream-modifications.txt` - the `tests/CMakeLists.txt` divergence.

No product file is modified, so no product entry is needed in the divergence
ledger.

## Commands and results

All build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-162-port-port.r1/logs`.
CTest ran from `<worktree>/build/tests`.

| Stage | Exact command | EXIT | Result / log |
| --- | --- | ---: | --- |
| Reconfigure the build tree (the lane tree is already configured; a target that does not exist yet needs the generated makefiles refreshed) | `cmake --build build --target zene -j 12 > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | `build-4.log` (the re-run of cmake picks the new `tests/CMakeLists.txt` up) |
| Build the new test | `cmake --build build --target MultitapEchoBoundsTest -j 12 > LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 | `build-5.log` |
| GREEN on this line | `cd build/tests && ctest -R '^MultitapEchoBoundsTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | 0 | `test-1.log`: `100% tests passed, 0 tests failed out of 1` |
| Save the historical-code patch (the RED proof) | `git diff -- plugins/MultitapEcho/ > LOGS/historical-reintro.patch; echo EXIT=$?` | 0 | `historical-reintro.patch`: the constructor's `applyPendingControls( m_controls.m_steps.value() );` -> `updateFilters( 0, 19 );` and the header's `float m_lpFreq [32]{};` -> `float m_lpFreq [32];` (the state of `36bb0682a^`) |
| RED: rebuild with the historical code | `cmake --build build --target MultitapEchoBoundsTest -j 12 > LOGS/build-6.log 2>&1; echo EXIT=$?` | 0 | `build-6.log` |
| RED: run | `cd build/tests && ctest -R '^MultitapEchoBoundsTest$' --output-on-failure > LOGS/test-2-red.log 2>&1; echo EXIT=$?` | 8 | `test-2-red.log`: `FAIL! ... constructorLeavesTheTapsPastTheGraphLengthUntouched() 'std::isfinite(output)' returned FALSE. (the constructor read a low-pass entry past the graph length)` and `FAIL! ... everyLowPassEntryIsInitialised() 'representation != PoisonPattern' returned FALSE.` (`Totals: 2 passed, 2 failed`) |
| Restore this line | `git apply -R LOGS/historical-reintro.patch; echo EXIT=$?` | 0 | `git diff --stat -- plugins/` is empty again |
| GREEN again: rebuild | `cmake --build build --target MultitapEchoBoundsTest -j 12 > LOGS/build-7.log 2>&1; echo EXIT=$?` | 0 | `build-7.log` |
| GREEN again: run | `cd build/tests && ctest -R '^MultitapEchoBoundsTest$' --output-on-failure > LOGS/test-3-green.log 2>&1; echo EXIT=$?` | 0 | `test-3-green.log`: `100% tests passed, 0 tests failed out of 1` |
| Build the affected family | `cmake --build build --target PluginPortsMigrationTest -j 12 > LOGS/build-8.log 2>&1; echo EXIT=$?` | 0 | `build-8.log` |
| Affected family run (2 CTest targets) | `cd build/tests && ctest -R '^(MultitapEchoBoundsTest\|PluginPortsMigrationTest)$' --output-on-failure > LOGS/test-4-family.log 2>&1; echo EXIT=$?` | 8 | `test-4-family.log`: `MultitapEchoBoundsTest` Passed; `PluginPortsMigrationTest` Failed - the only failing slot is the pre-existing `nonRealtimeLadspaEffectsAreInvalid()` ("SWH Decimator test fixture was not loaded"); `Totals: 56 passed, 1 failed, 2 skipped` |
| The code evidence | `git merge-base --is-ancestor 36bb0682a HEAD; git show 36bb0682a^:plugins/MultitapEcho/MultitapEcho.cpp; ... > LOGS/evidence-1.log 2>&1; echo EXIT=$?` | 0 | `evidence-1.log` |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1.log 2>&1; echo EXIT=$?` | 0 | `gate-1.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length - all PASS, `RESULT: PASS` |
| Complexity ratchet (required scope, unpiped) | `bash tests/complexity-gate.sh --check > LOGS/gate-2-complexity.log 2>&1; echo EXIT=$?` | 1 | 6 `REGRESSION: new function over target` lines, all pre-existing and in files this branch does not touch (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`) |
| File-length ratchet (required scope, unpiped) | `bash tests/file-length-gate.sh --check > LOGS/gate-3-filelength.log 2>&1; echo EXIT=$?` | 1 | 15 `REGRESSION: new file over 500 lines` lines, all pre-existing (`src/core/TakeLane.cpp`, `tests/src/core/*`, `tests/src/gui/ArrangerTimelineTest.cpp`); the new test file is 199 lines and is not listed |

The two ratchets are red on the train by design; `feature_check.py` compares
them against the branch's base (`2da2ed356`) and reports "no new item in your
files" for both, which is why the gate table is green. `lizard` measures the
new test's functions at CCN 1-2 (target 10) and the file at 199 lines (limit
500). No baseline was re-anchored and no limit was changed.

The ctest exit code stays 8 in the family run because of the pre-existing
`nonRealtimeLadspaEffectsAreInvalid()` failure: this build tree has no SWH
LADSPA plugin binaries (`build/plugins/LadspaEffect/swh/` contains no `.so`,
only CMake artefacts), and this branch changes neither the product nor that
test.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with
  it (exit codes).** There is no product change to revert: the verdict is
  ALREADY-FIXED. The equivalent proof is the historical-code reintroduction,
  which is the state the finding was filed against (`36bb0682a^`):
  `git apply` of `LOGS/historical-reintro.patch` -> the test FAILS
  (`ctest -R '^MultitapEchoBoundsTest$'`, EXIT 8, `test-2-red.log`, failing
  assertion "the constructor read a low-pass entry past the graph length" at
  `tests/src/plugins/MultitapEchoBoundsTest.cpp:168` and "a low-pass entry is
  still indeterminate after construction" at
  `tests/src/plugins/MultitapEchoBoundsTest.cpp:192`); `git apply -R` of the
  same patch -> the test PASSES (EXIT 0, `test-3-green.log`). The product
  files are byte-identical to HEAD (`git diff --stat -- plugins/` is empty).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the
  seam or injected values it uses).** The seam is the storage the effect is
  built in: the test fills it with the byte pattern `0xFF` through a `volatile`
  pointer and constructs the effect with a placement new, so the bytes the
  constructor reads are known. The graph length (16) and the tap count (20) are
  compile-time constants of the test; the low-pass entries are compared by
  their object representation, never evaluated as an indeterminate float. No
  clock, no sleep, no thread, no allocation-order dependence.
- **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** No product code was changed. The
  callers were checked anyway: `updateFilters()` has exactly one caller
  (`plugins/MultitapEcho/MultitapEcho.cpp:142`), `applyPendingControls()` is
  called from the constructor (`:68`) and from the audio thread (`:139`), and
  the graph that feeds `m_pendingLpFreq` is `MultitapEchoControls`
  (`lpSamplesChanged`, `lengthChanged`), which writes exactly the entries the
  graph covers. `m_stages` (1..4) bounds the inner loop over
  `m_filter[i][s]` (`StereoOnePole m_filter[32][4]`), and `steps` (4..32)
  bounds both arrays.
- **No collateral damage: old projects and saved data still load as before,
  and any limit or check you added does not hit other callers that legitimately
  exceed it.** No product behaviour changed at all: the branch adds one test
  binary and four registration lines. Saved data is untouched (the plugin's
  `saveSettings`/`loadSettings` are not modified), and the migrated plugin
  still renders exactly as before - the migration test's multitapecho case
  passes (`test-4-family.log`: "multitapecho: 32768 samples | sha256
  migrated=f98ccd7c..." with the test's expected divergence from the
  single-stage upstream reference).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
  the intended contract that shows it.** `plugins/MultitapEcho/MultitapEcho.cpp:68`
  (the constructor applies the graph's own length, not twenty taps),
  `plugins/MultitapEcho/MultitapEcho.h:70` (the low-pass array is
  value-initialised), `plugins/MultitapEcho/MultitapEcho.cpp:142` and `:128`
  (the only `updateFilters()` caller asks for `steps - 1`, with `steps` the
  graph length), `plugins/MultitapEcho/MultitapEcho.cpp:95` (`applyPendingControls`
  loops over the graph's entries only), and the removal commit
  `36bb0682a` ("Fix cluster new-p12-160 Multitap Echo race"), an ancestor of
  HEAD (`evidence-1.log`).

## Files in the commit

- `tests/src/plugins/MultitapEchoBoundsTest.cpp` - the guard test (new).
- `tests/CMakeLists.txt` - registers the new test target.
- `tests/fork-sources.txt` - registers the new fork-NEW files.
- `tests/all-sources.txt` - registers the new test source in the whole-tree
  manifest.
- `tests/upstream-modifications.txt` - the `tests/CMakeLists.txt` divergence.
- `docs/qwen-fixes-2026-10/cluster-new-p12-162-port-port.md` - this report.

No product file is in the commit: the product no longer has the defect.
