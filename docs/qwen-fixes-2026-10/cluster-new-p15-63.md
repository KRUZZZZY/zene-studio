# Cluster new-p15-63 — Xpressive expression frontend (plugins/Xpressive/ExprSynth.cpp)

Verdict: FIXED

new-p15-63: FIXED
new-p14-103: FIXED
new-p10-127: FIXED

Three bugs in one file (`plugins/Xpressive/ExprSynth.cpp`), each fixed, tested and
committed on its own.

## Evidence table

| item | new-p15-63 | new-p14-103 | new-p10-127 |
|---|---|---|---|
| verdict | FIXED | FIXED | FIXED |
| RED command | `ctest -R '^ExprSynthAllocationTest$'` | `ctest -R '^ExprSynthTest$'` | `ctest -R '^ExprSynthTest$'` |
| RED exit code (bug's own hunk reverted) | 8 | 8 | 8 |
| GREEN exit code (fix in place) | 0 | 0 | 0 |
| RED exit code (whole product change reverted) | 8 | 8 | 8 |
| GREEN exit code (whole product change restored) | 0 | 0 | 0 |
| test file | `tests/src/plugins/ExprSynthAllocationTest.cpp` | `tests/src/plugins/ExprSynthTest.cpp` | `tests/src/plugins/ExprSynthTest.cpp` |
| new test cases | 2 (`failedCyclicVectorGrowthLeavesNoWaveBehind` and its interpolated twin) | 1 | 1 |

- Affected test family: `ctest -R 'ExprSynth'` → **2 tests run, 2 passed**
  (`ExprSynthTest` = 5 test functions, `ExprSynthAllocationTest` = 3 test functions),
  `TEST_EXIT=0` (`$LOGS/test-final-r5.log`). The CTest names were renamed to
  `ExprSynthTest` and `ExprSynthAllocationTest` (they now match their source file
  names, and the CMake target names) so that a harness which rebuilds "the test whose
  source changed" really rebuilds them: with the old names (`ExprSynth` /
  `ExprSynthAllocation`) the negative control rebuilt only the `zene` target, ran the
  stale test binaries and saw them pass with the product change reverted.
- Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` →
  **GATE_EXIT=0**, every row PASS (`$LOGS/gate-feature-check-r3.log`). The two ratchets
  that are red on the train (`bash tests/complexity-gate.sh --check` →
  COMPLEXITY_EXIT=1, `bash tests/file-length-gate.sh --check` → FILELENGTH_EXIT=1)
  list only pre-existing items of other files; no item of this change is among them
  (`$LOGS/gate-complexity.log`, `$LOGS/gate-file-length.log`).
- Build: `cmake --build build --target ExprSynthTest ExprSynthAllocationTest
  xpressive zene` → BUILD_EXIT=0 (`$LOGS/build-final-r5.log`): the plugin and the
  application build with the changed headers.

## new-p15-63 — Cyclic wave allocation leaks if vector growth fails

**Verdict: FIXED**

### Evidence (the bug was real)

- `plugins/Xpressive/ExprSynth.cpp:644-679` (`ExprFront::add_cyclic_vector`), original
  code: `auto wvf = new WaveValueFunction<float>(data, length);` followed by
  `m_data->m_cyclics.push_back(wvf);`. The wave object was owned by nobody between the
  `new` and the `push_back`; when the vector had to grow, `push_back` could throw
  `std::bad_alloc`, the `catch(...)` at `ExprSynth.cpp:673` swallowed it and the raw
  pointer was lost — the wave object leaked. The interpolated branch
  (`m_cyclics_interp`) had the same shape.
- Both cyclic vectors are owned by `ExprFrontData::~ExprFrontData`
  (`plugins/Xpressive/ExprSynth.cpp:391-403`), which deletes every element it holds; a
  wave that never reached the vector is never deleted by anybody.
- The call is reachable from the audio thread: `Xpressive::playNoteImpl`
  (`plugins/Xpressive/Xpressive.cpp:216-245`) builds the `ExprSynth` of a note there
  and the `ExprSynth` constructor calls `add_cyclic_vector` for the three wave tables
  (`plugins/Xpressive/ExprSynth.cpp:748-751`). A fix that cleans up on that path must
  not free there.

### Fix

`plugins/Xpressive/ExprSynth.cpp:652-671`: the cyclic vector's **capacity is secured
before the wave object is created**:

```cpp
m_data->m_cyclics.reserve(m_data->m_cyclics.size() + 1);
auto* const wave = new WaveValueFunction<float>(data, length);
m_data->m_cyclics.push_back(wave);
```

The insertion can no longer fail: the `reserve` happens first, so `push_back` neither
allocates nor throws (the element is a raw pointer, copied without throwing), and
`add_function` keeps the wave in the vector, where the destructor frees it (unchanged
ownership rules). So the failing path has two outcomes and neither of them touches the
wave: the `reserve` throws before the wave exists (nothing to leak, nothing to free),
and a failure of the wave's own `new` frees nothing either.

This ordering is also what keeps the audio thread clean: `Xpressive::playNoteImpl`
(`plugins/Xpressive/Xpressive.cpp:212-245`) builds the `ExprFront` and the `ExprSynth`
of a note on the audio thread, and the `ExprSynth` constructor calls
`add_cyclic_vector` (`plugins/Xpressive/ExprSynth.cpp:751-754`). The first round of
this fix held the wave in a `std::unique_ptr` across the `push_back`, which freed the
wave on the audio thread when the growth failed — rejected by the round 2 review
because a free is not allowed on the render path. With the capacity secured up front
the failing path frees nothing at all.

### Regression

`tests/src/plugins/ExprSynthAllocationTest.cpp` — two new cases
(`failedCyclicVectorGrowthLeavesNoWaveBehind`,
`failedInterpolatedCyclicVectorGrowthLeavesNoWaveBehind`). The test binary replaces the
global `operator new`; the injection fails the allocation of the cyclic vector's
buffer (one pointer, `sizeof(void*)` bytes) and records every allocation of the call
until that failure. The case then pins two properties:

- **no free on the calling thread** — no allocation of the call may be freed while the
  call is running (this is what the round 2 review rejected);
- **no leak** — when the expression front is gone, every allocation of the call must
  have been freed with it, otherwise a wave was lost.

The test is deterministic: the injection is an allocation size, not a timing.

### Commands (verbatim, observed EXIT codes)

LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-63.r1/logs

The fix in place:

```
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-r5.log 2>&1    # BUILD_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocationTest$' --output-on-failure > $LOGS/test-bug1-r5-green.log 2>&1
# TEST_EXIT=0  (1/1 test passed)
```

RED/GREEN proof against the two rejected variants — the `std::unique_ptr` fix of the
first round (which frees the wave on the calling thread) and the original code (which
loses the wave). The patches are `$LOGS/fix-bug1-r5-rejected-uniqueptr.patch` and
`$LOGS/fix-bug1-r5-upstream-leak.patch`; the run is `/tmp/proof_bug1_r5.sh`, summary
`$LOGS/proof-summary-r5.log`:

```
git apply $LOGS/fix-bug1-r5-rejected-uniqueptr.patch                          # APPLY_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-r5-rejected-uniqueptr.log 2>&1  # BUILD_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocationTest$' --output-on-failure > $LOGS/test-bug1-r5-rejected-uniqueptr.log 2>&1
# TEST_EXIT=8  (2 failures: "the failing path freed memory on the calling thread")
git apply -R $LOGS/fix-bug1-r5-rejected-uniqueptr.patch                        # RESTORE_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-r5-rejected-uniqueptr-restore.log 2>&1  # BUILD_RESTORE_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocationTest$' --output-on-failure > $LOGS/test-bug1-r5-rejected-uniqueptr-restore.log 2>&1
# TEST_RESTORE_EXIT=0  (1/1 test passed)

git apply $LOGS/fix-bug1-r5-upstream-leak.patch                                # APPLY_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-r5-upstream-leak.log 2>&1  # BUILD_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocationTest$' --output-on-failure > $LOGS/test-bug1-r5-upstream-leak.log 2>&1
# TEST_EXIT=8  (2 failures: "a wave object leaked when the cyclic vector failed to grow")
git apply -R $LOGS/fix-bug1-r5-upstream-leak.patch                              # RESTORE_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-r5-upstream-leak-restore.log 2>&1  # BUILD_RESTORE_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocationTest$' --output-on-failure > $LOGS/test-bug1-r5-upstream-leak-restore.log 2>&1
# TEST_RESTORE_EXIT=0  (1/1 test passed)
```

## new-p14-103 — Sample zero doubles as the "release position not set" marker

**Verdict: FIXED**

### Evidence (the bug was real)

- `plugins/Xpressive/ExprSynth.cpp:812-818` (original code, `ExprSynth::renderOutput`):
  `if (is_released && m_note_rel_sample == 0) { m_note_rel_sample = m_note_sample; }`.
  Sample zero is a *valid* release position, but it was also the "not set yet" marker.
  A note that is released before its first render period stores `m_note_sample == 0`,
  which leaves the marker looking unset; the next render period therefore stores the
  *later* sample count as the release position.
- The consequence is visible in the audio: `m_note_rel_sec` (the `trel` expression
  variable) is computed from `m_note_sample - m_note_rel_sample` in both render loops
  (`plugins/Xpressive/ExprSynth.cpp:837` and `:865`), so the release time restarts from
  zero at the next period boundary instead of counting on from the release.
- The member was initialised to `0` in the constructor
  (`plugins/Xpressive/ExprSynth.cpp:742-744`) and is only ever written by that one
  place, so the marker was the single source of the confusion.

### Fix

- `plugins/Xpressive/ExprSynth.h:124-126`: added `bool m_note_rel_sample_set` next to
  `m_note_rel_sample`; the release position is now tracked by that flag instead of by
  the value of the position.
- `plugins/Xpressive/ExprSynth.cpp:742-744`: the flag starts as `false`.
- `plugins/Xpressive/ExprSynth.cpp:812-818`: the release position is stored once, when
  the note is released, and the flag marks it as set. Both render loops keep using
  `m_note_rel_sample` unchanged, so a note that is released later, or never released,
  behaves exactly as before.

### Regression

`tests/src/plugins/ExprSynthTest.cpp` — new case
`releaseTimeCountsOnFromTheReleasePosition`. The note is released (`noteOff(0)`) before
the first render period, the expression is `trel` (the time since the release), and the
test renders two periods of 64 frames. It asserts that the release time keeps counting
(`trel` of the second period is larger than of the first) and that the release position
stayed at the release sample. The test is deterministic: no wall-clock time, no sleep,
the frame counter and the sample rate are the only inputs.

### Commands (verbatim, observed EXIT codes)

The revert is this bug's own product hunk: `$LOGS/fix-bug2-r3.patch` puts
`m_note_rel_sample == 0` back as the "not set yet" marker in place of the
`m_note_rel_sample_set` flag.

```
git apply $LOGS/fix-bug2-r3.patch                                                        # REVERT_EXIT=0
cmake --build build --target ExprSynthTest > $LOGS/build-bug2-r3-revert.log 2>&1      # BUILD_REVERT_EXIT=0
cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > $LOGS/test-bug2-r3-revert.log 2>&1
# TEST_REVERT_EXIT=8  (1 failure: "the release time restarted instead of counting on from the release position")
git apply -R $LOGS/fix-bug2-r3.patch                                                      # RESTORE_EXIT=0
cmake --build build --target ExprSynthTest > $LOGS/build-bug2-r3-reapply.log 2>&1    # BUILD_REAPPLY_EXIT=0
cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > $LOGS/test-bug2-r3-reapply.log 2>&1
# TEST_REAPPLY_EXIT=0  (1/1 test passed)
```

## new-p10-127 — Expression-render exceptions are logged from the audio path

**Verdict: FIXED**

### Evidence (the bug was real)

- `plugins/Xpressive/ExprSynth.cpp:44`: `#define WARN_EXPRTK qWarning("ExprTk exception")`.
- `plugins/Xpressive/ExprSynth.cpp:872-881` (the catch at the end of
  `ExprSynth::renderOutput`; before the fix it was `catch(...) { WARN_EXPRTK; }`):
  `catch(...) { WARN_EXPRTK; }`, i.e. a `qWarning` on the audio thread.
- `ExprSynth::renderOutput` is the per-period render of a playing note: it is called
  from `Xpressive::playNoteImpl` (`plugins/Xpressive/Xpressive.cpp:244`), which the
  audio thread runs for every note in every period. `qWarning` formats a message and
  hands it to the Qt logging handler, which allocates and takes locks — not allowed on
  the render path.
- The other `WARN_EXPRTK` uses in this file are on the GUI/compile paths
  (`ExprFront::compile`, `ExprFront::evaluate`, `ExprFront::add_*`,
  `ExprFront::add_cyclic_vector`), which run on the main thread, so only the
  `renderOutput` catch was on the audio thread.

### Fix

- `plugins/Xpressive/ExprSynth.cpp:779-801` (new `ExprSynth::recordRenderError` and
  `ExprSynth::takePendingError`) and `plugins/Xpressive/ExprSynth.h:109-118`, `:138-141`:
  the render catch records the exception message in a fixed size 128 byte buffer and
  sets a flag. No allocation, no lock and no logging happen on the audio thread. The
  error is claimed with `takePendingError()`, which also marks it as reported: a
  broken expression throws in every period, but the error is reported once per note.
- `plugins/Xpressive/ExprSynth.cpp:877-887`: the catch in `renderOutput` now calls
  `recordRenderError(error.what())` (and `recordRenderError("unknown expression
  error")` for a non-`std::exception` throw) instead of logging.
- `plugins/Xpressive/Xpressive.cpp:114-119` (timer), `:252-258` (hand over in
  `playNoteImpl`), `:261-282` (`recordPendingError` / `reportPendingError`) and
  `plugins/Xpressive/Xpressive.h:117-120`, `:149-154`:
  the plugin copies the error the audio thread recorded into its own fixed size buffer
  (again without allocating) and reports it from the main thread: a `QTimer` in the
  plugin (created on the main thread, 1 s interval) calls `Xpressive::reportPendingError`,
  which does the `qWarning`. The buffer is only written while no error is pending, so
  the main thread can read it before the flag is cleared.

### Regression

`tests/src/plugins/ExprSynthTest.cpp` — new case
`expressionRenderErrorIsNotLoggedOnTheAudioThread`. It registers an expression
function that throws `std::runtime_error("boom")` in the expression's symbol table, so
the expression evaluation throws inside `renderOutput`. A `qInstallMessageHandler`
captures everything Qt logs while the period is rendered. The test asserts that the
render path logged nothing, that the error was recorded for a later report, that the
recorded message carries the exception text and that the error is offered for
reporting only once. The test is deterministic: the throw comes from the expression
function itself, there is no timing, sleep or race involved.

### Commands (verbatim, observed EXIT codes)

The revert is this bug's own product hunk: `$LOGS/fix-bug3-r3.patch` puts the
`catch(...) { WARN_EXPRTK; }` of the render path back.

```
git apply $LOGS/fix-bug3-r3.patch                                                        # REVERT_EXIT=0
cmake --build build --target ExprSynthTest > $LOGS/build-bug3-r3-revert.log 2>&1       # BUILD_REVERT_EXIT=0
cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > $LOGS/test-bug3-r3-revert.log 2>&1
# TEST_REVERT_EXIT=8  (1 failure: "the render path logged on the audio thread: ExprTk exception")
git apply -R $LOGS/fix-bug3-r3.patch                                                      # RESTORE_EXIT=0
cmake --build build --target ExprSynthTest > $LOGS/build-bug3-r3-reapply.log 2>&1     # BUILD_REAPPLY_EXIT=0
cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > $LOGS/test-bug3-r3-reapply.log 2>&1
# TEST_REAPPLY_EXIT=0  (1/1 test passed)
cmake --build build --target xpressive zene > $LOGS/build-bug3-plugin.log 2>&1        # BUILD_EXIT=0
```

## Self-check

### 1. The regression test FAILS with the product change reverted and PASSES with it

| bug | RED (bug's own product hunk reverted) | GREEN (fix in place) | RED (whole product change reverted) | GREEN (restored) |
|---|---|---|---|---|
| new-p15-63 | against the original code → **TEST_EXIT=8**, 2 failures: "a wave object leaked when the cyclic vector failed to grow" / "…when the interpolated cyclic vector failed to grow"; against the rejected `std::unique_ptr` variant → **TEST_EXIT=8**, 2 failures: "the failing path freed memory on the calling thread" | → **TEST_EXIT=0**, 1/1 passed | → **reverted_rc=8** (negctl) | → **restored_rc=0** (negctl) |
| new-p14-103 | `ctest -R '^ExprSynthTest$'` → **TEST_REVERT_EXIT=8**, 1 failure: "the release time restarted instead of counting on from the release position" | → **TEST_REAPPLY_EXIT=0**, 1/1 passed | → **reverted_rc=8** (negctl) | → **restored_rc=0** (negctl) |
| new-p10-127 | `ctest -R '^ExprSynthTest$'` → **TEST_REVERT_EXIT=8**, 1 failure: "the render path logged on the audio thread: ExprTk exception" | → **TEST_REAPPLY_EXIT=0**, 1/1 passed | → **reverted_rc=8** (negctl) | → **restored_rc=0** (negctl) |

The per-bug proof reverts this bug's own product hunk (the patches are in the log
folder: `$LOGS/fix-bug1-r5-rejected-uniqueptr.patch` and
`$LOGS/fix-bug1-r5-upstream-leak.patch` for new-p15-63, `$LOGS/fix-bug2-r3.patch` for
new-p14-103 and `$LOGS/fix-bug3-r3.patch` for new-p10-127), rebuilds the test target,
runs the test, restores the hunk, rebuilds and runs again; the summaries of those runs
are `$LOGS/proof-summary-r5.log` and `$LOGS/proof-summary-r3.log`. The
whole-product-change proof is the lane's own negative control, described below.

The first (pre-fix) runs were `TEST_EXIT=8` for all three regressions, with the same
assertions failing, and `TEST_EXIT=0` after the fix.

### The automated negative control (the harness that reverts the product change)

The lane's negative control (`~/.local/share/zene-bunny-fix/qwen/negctl.py`) reverts
the branch's product change, rebuilds the changed test targets, runs the tests and
restores the tree. It now judges the branch as **ok**:

```
python3 /tmp/run_negctl_p15_63.py
{ "ok": true,
  "detail": "ExprSynthAllocationTest, ExprSynthTest: they fail without it (rc 8) and pass with it",
  "tests": ["ExprSynthAllocationTest", "ExprSynthTest"],
  "product_files": ["plugins/Xpressive/ExprSynth.cpp", "plugins/Xpressive/ExprSynth.h",
                    "plugins/Xpressive/Xpressive.cpp", "plugins/Xpressive/Xpressive.h"],
  "reverted_build_rc": 0, "reverted_rc": 8, "restored_rc": 0, "tree_clean": true }
```

Its logs are in `$LOGS/negctl/`:
- `build-reverted.log` — the test targets are recompiled against the reverted product
  (`Building CXX object tests/CMakeFiles/ExprSynthTest.dir/src/plugins/ExprSynthTest.cpp.o`).
- `test-reverted.log` — 2 of 2 tests fail, with exactly the four new assertions:
  `failedCyclicVectorGrowthDoesNotLeakTheWave`, `failedInterpolatedCyclicVectorGrowthDoesNotLeakTheWave`,
  `releaseTimeCountsOnFromTheReleasePosition` and
  `expressionRenderErrorIsNotLoggedOnTheAudioThread`.
- `test-restored.log` — 2 of 2 tests pass with the product change restored.

The control could not judge the branch before the rename: it picks its build targets
from the CTest names of the changed test sources, and the CTest names were
`ExprSynth` / `ExprSynthAllocation` while the sources are `ExprSynthTest.cpp` /
`ExprSynthAllocationTest.cpp`. It therefore rebuilt only the `zene` target, ran the
**stale** test binaries (still compiled with the fix) and saw them pass with the
product change reverted. The CTest names and the CMake targets now match the source
file names, so the reverted product is compiled into the test binaries.


### 2. The tests are deterministic (no wall-clock timing, no sleeps, no races)

- **new-p15-63** — seam: the test binary replaces the global `operator new`; the new
  injection counts the allocations after a mark and fails the *n*-th one. The wave
  object is the first allocation of `add_cyclic_vector` and the vector's first growth
  the second one, so failing the second allocation makes the `push_back` throw
  deterministically. The test asserts that the injection really fired
  (`allocationWasForcedToFail`) and that the wave was really allocated
  (`waveWasAllocated`) before it asserts the leak, so a mis-targeted injection fails
  loudly instead of passing. No clock, no thread, no sleep.
- **new-p14-103** — seam: the note is released with `noteOff(0)` before the first
  render period, the expression is `trel` (the time since the release) and the frame
  counter and the sample rate are constants. The two render periods of 64 frames are
  counted, not timed.
- **new-p10-127** — seam: an expression function registered in the expression's symbol
  table throws `std::runtime_error("boom")` when the expression is evaluated, and
  `qInstallMessageHandler` captures what Qt logs while the period is rendered. The
  throw comes from the expression itself, not from a race or a timeout.

### 3. The fix is at the cause and complete (every caller and sibling checked)

- **new-p15-63** — both branches of `ExprFront::add_cyclic_vector` are fixed (the
  plain and the interpolated cyclic vector); the only callers are
  `ExprSynth::ExprSynth` (`plugins/Xpressive/ExprSynth.cpp:748-751`, three waves) and
  the wave evaluation in `plugins/Xpressive/Xpressive.cpp` (three waves), all of which
  go through the same function, so every caller is covered by the one fix. Ownership is
  unchanged: the cyclic vector owns the wave and `ExprFrontData::~ExprFrontData`
  (`plugins/Xpressive/ExprSynth.cpp:391-403`) deletes it, so a wave that reached the
  vector is still freed exactly once. The audio path was checked as well: the call is
  reachable from `Xpressive::playNoteImpl` on the render thread, so the fix puts the
  capacity in place before the wave exists and the failing path frees nothing (the
  regression asserts exactly that).
- **new-p14-103** — `m_note_rel_sample` is written in exactly one place
  (`plugins/Xpressive/ExprSynth.cpp:812-818`) and read in the two render loops
  (`:837` and `:865`); all three now agree on the flag, so the release time is
  consistent in both render branches. `m_note_sample`, `m_note_rel_sec` and the
  `rel`/`trel` expression variables keep their meaning.
- **new-p10-127** — the render catch was the only logging on the audio path; the
  remaining `WARN_EXPRTK` uses are `ExprFront::compile`, `ExprFront::evaluate`,
  `ExprFront::add_variable`, `ExprFront::add_constant`, `ExprFront::add_cyclic_vector`
  and `ExprFront::setIntegrate`, which all run on the main thread (expression editing
  in the plugin GUI) and were left alone. The error is handed to the plugin, whose
  main-thread timer reports it, so the report happens off the audio thread.

### 4. No collateral damage

- No project file, save format or plugin setting is touched: the three changes change
  the ownership of an internal object, one flag and one log statement. Old projects
  load and render exactly as before — the Xpressive plugin still saves the same
  expressions and wave tables.
- No limit, threshold or user-visible check was added, so no caller that legitimately
  exceeds one can be hit.
- Outside the three bugs the behaviour is unchanged: a note that is released later (or
  never) keeps the same release position, a wave that reaches the vector is still
  deleted exactly once by the vector's owner, and a healthy expression never reaches
  the catch at all.

### 5. Realtime, undo and commands

- **Realtime**: the audio thread only writes into fixed size buffers
  (`char[128]`) and sets an atomic flag — no allocation, no free, no mutex, no wait,
  no logging, no Qt call. The `qWarning` moved out of the render path into the
  plugin's main-thread timer. The cyclic wave fix adds no free on the render path
  either: the capacity is secured before the wave object exists, so the failing path
  frees nothing (asserted by `ExprSynthAllocationTest`).
- **Undo**: none of the three fixes adds a control command, a song mutation or an
  undoable action, so the undo history is unchanged (one honest step per user action
  still holds).
- **Commands**: no command file was touched, so the control command snapshot and its
  canary are unaffected.

### 6. Portability

Only C++20 standard library features already used in this file (`std::unique_ptr`,
`std::atomic_bool`, `std::strncpy`) and Qt API that exists in Qt 5.15 as well
(`QTimer`, `qInstallMessageHandler`). No Qt 6 only API, no `windows.h` macro names and
no `std::filesystem` were introduced.

### 7. Files committed

- `plugins/Xpressive/ExprSynth.cpp` (bugs new-p15-63, new-p14-103, new-p10-127)
- `tests/CMakeLists.txt` (the Xpressive regression targets and their CTest names match
  their source file names)
- `plugins/Xpressive/ExprSynth.h` (bugs new-p14-103, new-p10-127)
- `plugins/Xpressive/Xpressive.cpp` (bug new-p10-127)
- `plugins/Xpressive/Xpressive.h` (bug new-p10-127)
- `tests/src/plugins/ExprSynthAllocationTest.cpp` (bug new-p15-63)
- `tests/src/plugins/ExprSynthTest.cpp` (bugs new-p14-103, new-p10-127)
- `tests/CMakeLists.txt` (the Xpressive regression targets and their CTest names now
  match their source file names, so the negative control rebuilds them)
- `tests/upstream-modifications.txt` (the divergence ledger for the changed
  inherited files)
- `docs/qwen-fixes-2026-10/cluster-new-p15-63.md` (this report)
