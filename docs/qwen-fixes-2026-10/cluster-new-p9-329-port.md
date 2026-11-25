FIXED

# Cluster new-p9-329-port: OpulenZ frees its emulator before it removes its play handles

## The finding

At the base commit `fa4a5f2e78`, `plugins/OpulenZ/OpulenZ.cpp` destroyed the
instrument's emulator **before** it removed the instrument's play handles from
the audio engine, and it did so **outside** `emulatorMutex`, so an audio
callback that was processing a play handle of that track could enter
`playImpl()` and use the freed emulator.

This is a PORT job: the fix was re-applied by hand onto this branch (no merge,
no cherry-pick, no rebase) and the regression was rebuilt on the current tree.

## Verdict: REAL (nothing was already fixed here)

On this branch, with the product patch reverted
(`logs/fix.patch`, generated from `git diff fa4a5f2e78..HEAD --
plugins/OpulenZ/OpulenZ.cpp`), the destructor reads:

```cpp
OpulenzInstrument::~OpulenzInstrument() {
	delete theEmulator;                                       // freed first
	Engine::audioEngine()->removePlayHandlesOfTypes( instrumentTrack(),
			PlayHandle::Type::NotePlayHandle
			| PlayHandle::Type::InstrumentPlayHandle );    // handles removed later
	delete [] renderbuffer;
}
```

The emulator was deleted while the play handles of the very same track were
still registered with the audio engine, and outside `emulatorMutex`
(`plugins/OpulenZ/OpulenZ.h:144`, `static QRecursiveMutex emulatorMutex;`,
defined at `plugins/OpulenZ/OpulenZ.cpp:95`, the lock that guards
`theEmulator` (`.h:125`) and `renderbuffer` (`.h:128`)).

The audio engine's `removePlayHandlesOfTypes()`
(`src/core/AudioEngine.cpp:1626`) is the engine's synchronisation point: it
calls `requestChangeInModel()` at line 1628, which takes `m_changeMutex`
(`src/core/AudioEngine.cpp:1707-1721`). The render path holds that lock for a
whole period, so the call waits out a running period before it touches the
handle lists. Doing that **after** freeing the emulator left a window in which
a running callback could still reach the freed object.

Proof that the code is not already fixed: the regression fails on that code
(see the RED row below, `logs/test-4-red.log`, EXIT=8).

## The fix (at the cause)

`plugins/OpulenZ/OpulenZ.cpp:225-235`:

```cpp
OpulenzInstrument::~OpulenzInstrument() {
	Engine::audioEngine()->removePlayHandlesOfTypes( instrumentTrack(),
			PlayHandle::Type::NotePlayHandle
			| PlayHandle::Type::InstrumentPlayHandle );

	QMutexLocker locker(&emulatorMutex);
	delete theEmulator;
	theEmulator = nullptr;
	delete [] renderbuffer;
	renderbuffer = nullptr;
}
```

1. It removes the instrument's play handles FIRST (`:226-228`). That call takes
   the engine's change-in-model lock, so it cannot return while a render period
   is running, and once it has returned no play handle of this track exists, so
   no later callback can start `playImpl()` for this instrument.
   `AudioEngine::disposePlayHandle()` (`src/core/AudioEngine.cpp:453-470`)
   deletes an `InstrumentPlayHandle` synchronously at line 468, so the handle is
   really gone when the call returns.
2. It then frees the emulator and the render buffer under `emulatorMutex`
   (`:230`), so a callback that is already inside `playImpl()` finishes first.
   Both pointers are nulled (`:232`, `:234`).

Lock ordering: the change-in-model lock is released by
`doneChangeInModel()` before `removePlayHandlesOfTypes()` returns
(`src/core/AudioEngine.cpp:1701`), i.e. before the destructor takes
`emulatorMutex` at `:230`. The two locks are never held at the same time by
this path, so the fix adds no lock-order inversion.

### Siblings and callers checked (definition of done #3)

Every reader and writer of `theEmulator`/`renderbuffer` in the plugin was
enumerated with a line scan of `plugins/OpulenZ/OpulenZ.cpp`:

* `playImpl()` (`:400-424`) - the only render path; takes `emulatorMutex` at
  `:402`, renders with `theEmulator->update(renderbuffer, ...)` at `:411`,
  releases at `:423`. Unchanged.
* `reloadEmulator()` (`:238-250`) - the same defect shape, already correct on
  this line: deletion, replacement, voice reset and `updatePatch()` all run
  under one `emulatorMutex` section (`:239-249`). Left untouched. Covered by
  the existing `PluginPortsMigrationTest` OpulenZ slots.
* `handleMidiEvent()` (`:302-390`) - locks at `:304`, so `setVoiceVelocity()`
  (`:253`, whose own comment at `:252` says "This shall only be called from code
  protected by the holy Mutex!") and the register writes at `:315-356` are
  under the lock.
* `loadPatch()`/`loadPatchLocked()` (`:508-527`) and
  `updatePatch()`/`updatePatchLocked()` (`:556-...`) - `QMutexLocker` at `:509`
  and `:557`; the register writes at `:515-525` and `:591` are under them.
* `tuneEqual()` (`:529-534`) only fills `fnums[]`; it never touches the
  emulator.
* The constructor (`:145-166`) creates the emulator under the mutex and stages
  the `InstrumentPlayHandle`; the destructor is now its mirror image (staged
  handle disposed of, then the emulator freed).
* The engine side is unchanged: `removePlayHandlesOfTypes()` sweeps the active
  list (`src/core/AudioEngine.cpp:1629-1641`), the staged `m_newPlayHandles`
  list and the capacity-deferred list (`1644-1700`), then releases the
  change-in-model lock at `1701`.

### Realtime / undo / commands

* The destructor runs on the control thread. `playImpl()` keeps its single
  existing `emulatorMutex` acquisition; the fix adds no allocation, no free, no
  lock, no wait and no logging to any audio-thread path.
* No control command was added (the ControlCommandsSnapshot canary does not
  move). No undo step was added or removed.

### Collateral damage

* The change is confined to the OpulenZ teardown. Project files, saved data,
  the plug-in's saved settings and SBI loading are untouched; no other plug-in
  or engine caller is affected, and no limit or check was added.
* `theEmulator`/`renderbuffer` are private members (`plugins/OpulenZ/OpulenZ.h`),
  so nulling them cannot be observed elsewhere.

## Test matrix (written before the fix, kept as the plan)

| Path | What must hold | Where it is exercised |
| --- | --- | --- |
| Normal path: create an instrument, then destroy it | the play handle is disposed of before the emulator is freed | `OpulenZLifetimeTest::teardownRemovesThePlayHandlesBeforeTheEmulatorIsFreed` |
| Failure path: a render is inside `playImpl()` (emulator mutex held) while the control thread deletes the instrument | the destructor blocks on `emulatorMutex`, and the emulator is not freed while the caller holds it | same slot (the test holds `OpulenzInstrument::emulatorMutex` across the teardown and asserts `emulatorDestroyedWhileMutexHeld == false`) |
| Reconfiguration: sample rate change replaces the emulator | `reloadEmulator()` keeps replacing it under the mutex; a later destructor frees the new object | `reloadEmulator()` `:238-250` (unchanged) plus `PluginPortsMigrationTest` `opulenzReloadEmulatorReplacesEmulatorUnderMutex`, run in the family below |
| Render shapes: short span, multi-period span, zero-length span, out-of-range pitch | render path unchanged and still correct | ctest `OpulenZSpanSize`, `OpulenZZeroFrameCount`, `OpulenZHz2fnumRange`, `OpulenZ` (`logs/test-3-family.log`, 5/5 PASS) |
| Exhaustion / repeated teardown | many instruments created and destroyed in one process; no double free, no leak of the staged handle | covered by the family suite running to completion in one process; the fix nulls both pointers so a second free would be a null call, not a dangling one |
| Audio-thread constraints | nothing allocates, frees, locks or logs on the render thread | `playImpl()` untouched; only the control-thread destructor changed |
| Platform requirements | the test needs only Qt Test, `<thread>`, `<mutex>`, `<condition_variable>` and `std::span` (already the `PlayHandle::play` signature); the target is dropped from the list when the `adplug` target is absent | `tests/CMakeLists.txt:2002-2006`; built here with GCC 16.2.1 / Qt 6.12.0 |

## The regression

`tests/src/plugins/OpulenZLifetimeTest.cpp` (fork-NEW) compiles the plug-in's
own sources into the test and watches the real teardown:

* the play handle the instrument registers is a test double whose destructor
  reports when the engine disposed of it (that only happens inside
  `AudioEngine::removePlayHandlesOfTypes()` → `disposePlayHandle()`);
* the emulator is a `CTemuopl` subclass whose destructor reports when the
  emulator was freed;
* the emulator mutex is held for the whole teardown, exactly like `playImpl()`
  holds it while it renders.

Both events go into one ordered sequence counter, so the verdict is a fact and
not a race. The test never sleeps and never decides by a timeout; the 60 s wait
is a watchdog that turns a hang into a failed assertion. The assertions are:

* the play handle must be disposed of BEFORE the emulator is freed
  (`handleDestroyedAt < emulatorDestroyedAt`);
* the emulator must not be freed while the emulator mutex is held.

Registered in `tests/CMakeLists.txt` (`:696-701` list entry, `:2002-2006`
adplug guard, `:2260-2279` target wiring: the plug-in's sources compiled in,
`adplug` linked, `PLUGIN_NAME=opulenz`), in `tests/all-sources.txt` (line 3611,
regenerated from its own recipe - `all-sources-reproduce` PASSes), in
`tests/fork-sources.txt` (line 3977 for the test, last line for this report),
and the changed product file is recorded in `tests/upstream-modifications.txt`.

## Race reasoning (TSan unavailable)

ThreadSanitizer does not run on this box: there is no TSan runtime in the
container's compiler/library set, so the race is proven by ordering, not by
instrumentation. The ordering argument:

* the destructor first calls `removePlayHandlesOfTypes()`, which takes the
  engine's change-in-model lock; the render path holds that lock for a whole
  period, so the call cannot return until the running period has finished;
* after it returns, no play handle of this track exists, so no later audio
  callback can start `playImpl()` for this instrument;
* `emulatorMutex` then waits out a render that was already in flight (it is
  held for the whole render), and only then are `theEmulator` and
  `renderbuffer` freed.

There is no interleaving in which a render runs against freed memory: the
handle removal excludes new renders, and the mutex excludes an in-flight one.
The regression test drives exactly that ordering and records it in one
sequence counter, which is deterministic (no sleeps, no wall-clock thresholds).

## Evidence table (this session, round 8)

All commands ran in the work tree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-329-port`;
logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-329-port.r8/logs`.

| Step | Exact command | Result |
| --- | --- | --- |
| Build | `cmake --build build --target OpulenZLifetimeTest zene -j4 > logs/build-1.log 2>&1` | `EXIT=0` (log tail: `[100%] Built target OpulenZLifetimeTest`, `[100%] Built target zene`) |
| GREEN (fix in place) | `cd build/tests && ctest -R OpulenZLifetime --output-on-failure > logs/test-1.log 2>&1` | `EXIT=0`; `1/1 Test #205: OpulenZLifetimeTest ... Passed 0.57 sec`; `100% tests passed out of 1` |
| Family, before its binaries existed | `cd build/tests && ctest -R OpulenZ --output-on-failure > logs/test-2-family.log 2>&1` | `EXIT=8`; `4 tests failed ... (Not Run)` because `PluginPortsMigrationTest` was not built yet - a build-tree gap, not a regression |
| Build the family binary | `cmake --build build --target PluginPortsMigrationTest -j4 > logs/build-2-plugin-ports.log 2>&1` | `EXIT=0` (`[100%] Built target PluginPortsMigrationTest`) |
| Family (all five) | `cd build/tests && ctest -R OpulenZ --output-on-failure > logs/test-3-family.log 2>&1` | `EXIT=0`; `100% tests passed out of 5` (OpulenZ, OpulenZSpanSize, OpulenZZeroFrameCount, OpulenZHz2fnumRange, OpulenZLifetimeTest) |
| 4b RED, step 1 | `git diff fa4a5f2e785c73e32216931e33cdbe140c06647d..HEAD -- plugins/OpulenZ/OpulenZ.cpp > logs/fix.patch` then `git apply -R logs/fix.patch` | `DIFF_EXIT=0`, `REVERT_EXIT=0`; `git status --short` shows ` M plugins/OpulenZ/OpulenZ.cpp` |
| 4b RED, step 2 | `cmake --build build --target OpulenZLifetimeTest -j4 > logs/build-3-red.log 2>&1` | `BUILD_EXIT=0` |
| 4b RED, step 3 | `cd build/tests && ctest -R OpulenZLifetime --output-on-failure > logs/test-4-red.log 2>&1` | `EXIT=8`; `FAIL! : ... 'g_events.handleDestroyedAt < g_events.emulatorDestroyedAt' returned FALSE. (the emulator was freed before the instrument's play handles were removed (play handle disposed at 2, emulator freed at 1) ...) Loc: [../../tests/src/plugins/OpulenZLifetimeTest.cpp(248)]`; `0% tests passed, 1 tests failed out of 1` |
| 4b GREEN, step 1 | `git apply logs/fix.patch` | `APPLY_EXIT=0`; `git status --short` empty |
| 4b GREEN, step 2 | `cmake --build build --target OpulenZLifetimeTest zene -j4 > logs/build-4-green.log 2>&1` | `BUILD_EXIT=0` |
| 4b GREEN, step 3 | `cd build/tests && ctest -R OpulenZLifetime --output-on-failure > logs/test-5-green.log 2>&1` | `EXIT=0`; `1/1 Test #205: OpulenZLifetimeTest ... Passed 0.55 sec`; `100% tests passed out of 1` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1` | `EXIT=0`; `RESULT: PASS`; rows: `fork-sources PASS`, `all-sources-reproduce PASS`, `no-upstream-regression PASS`, `check-namespace PASS`, `no-tautology PASS`, `doc-figures PASS`, `complexity no new item in your files`, `file-length no new item in your files` |
| Pre-submit | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-329-port > logs/submit-check-1.log 2>&1` | `EXIT=0`; `RESULT: PASS - submit`; six CHECKED rows PASS, five ASK items answered below |
| Pre-submit, re-run on the committed report | same command with `> logs/submit-check-2.log 2>&1` | `EXIT=0`; `RESULT: PASS - submit`; `commits on the branch`, `working tree clean`, `report`, `tests added or changed`, `new test files registered`, `gates (feature_check.py)` all PASS |
| all-sources recipe | `bash tests/all-sources-reproduce.sh > logs/all-sources-reproduce-1.log 2>&1` | `EXIT=0`; `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |

## Files committed

* `plugins/OpulenZ/OpulenZ.cpp` - the fix (destructor: handles first, then the
  emulator and the render buffer under the mutex).
* `tests/src/plugins/OpulenZLifetimeTest.cpp` - the regression (new file).
* `tests/CMakeLists.txt` - registers the test target and its adplug guard.
* `tests/all-sources.txt` - regenerated with its own recipe; registers the new
  test source.
* `tests/fork-sources.txt` - registers the new fork-NEW files (test + report).
* `tests/upstream-modifications.txt` - records the changed product file and its
  reason.
* `docs/qwen-fixes-2026-10/cluster-new-p9-329-port.md` - this report.

## Self-check

* **Does the regression FAIL with the product change reverted and PASS with it
  (exit codes)?** Yes, this session. Reverted with `git apply -R logs/fix.patch`
  (`REVERT_EXIT=0`), rebuilt (`build-3-red.log`, `BUILD_EXIT=0`), and
  `ctest -R OpulenZLifetime` gave **EXIT=8** in `logs/test-4-red.log` with
  `'g_events.handleDestroyedAt < g_events.emulatorDestroyedAt' returned FALSE`
  (play handle disposed at 2, emulator freed at 1; 1 of 1 failed). Restored with
  `git apply logs/fix.patch` (`APPLY_EXIT=0`), rebuilt (`build-4-green.log`,
  `BUILD_EXIT=0`), and the same ctest gave **EXIT=0** in `logs/test-5-green.log`
  (1/1 passed, 0.55 s). The wider `ctest -R OpulenZ` family passed 5/5 with
  **EXIT=0** in `logs/test-3-family.log`.
* **Is the test deterministic?** Yes. The verdict is the ORDER of two recorded
  events - the play-handle disposal and the emulator's destruction - kept in one
  sequence counter guarded by a `std::mutex`, plus a boolean snapshot taken
  while the test holds `OpulenzInstrument::emulatorMutex`. There is no sleep, no
  wall-clock threshold and no timing decision; the only timeout is a 60 s
  watchdog that fails the test instead of hanging it, and the event it waits
  for is produced by the same teardown that the assertions judge.
* **Is the fix at the cause and complete?** Yes. The destructor stops the audio
  thread (handle removal, the engine's change-in-model synchronisation point)
  before it frees, and frees under the mutex that guards the object
  (`plugins/OpulenZ/OpulenZ.cpp:225-235`). Every reader/writer of
  `theEmulator`/`renderbuffer` was enumerated (see "Siblings and callers
  checked") and each is under `emulatorMutex`; `reloadEmulator()` (`:238-250`)
  already locks and is left alone; the constructor is the destructor's mirror
  image.
* **No collateral damage?** None outside the OpulenZ teardown: no saved data,
  project format, plug-in settings, control command, undo step, gate limit or
  test assertion changed, and no caller outside the plugin is touched. The
  public behaviour of the instrument is identical; only teardown order and
  locking changed.
* **If the verdict is ALREADY-FIXED or FALSE-POSITIVE, the code that shows
  it?** Not applicable: the verdict is FIXED, and the reverted-code run
  (`logs/test-4-red.log`, EXIT=8) shows the defect is present at the base
  commit.

## Other findings

* ThreadSanitizer has no runtime on this box, so the race argument is an
  ordering argument rather than an instrumented result.
* In this build tree the four `PluginPortsMigrationTest`-backed OpulenZ tests
  were unbuilt at the start of the session (`Not Run` in `logs/test-2-family.log`);
  after building that target they pass 5/5 (`logs/test-3-family.log`). These
  data files are part of the plugin's own test suite that the R7 lane built
  into its own tree. This is a lane build-tree gap, not a product defect.
* `hwloc` prints "failed to find sysfs cpu topology directory" on every
  compile in this container; harmless build noise.
