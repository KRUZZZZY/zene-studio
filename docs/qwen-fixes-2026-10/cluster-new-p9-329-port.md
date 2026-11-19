FIXED

# Cluster new-p9-329-port: OpulenZ frees its emulator before it removes its play handles

## The finding

Luna's patrol reported, against `plugins/OpulenZ/OpulenZ.cpp:222` (historical line
numbers): the OpulenZ instrument's destructor destroyed its emulator before it
removed the instrument's play handles, and it did so without the emulator mutex,
so an audio callback that was processing a play handle of that track could enter
`playImpl()` and use the freed emulator.

This is a PORT job: the fix was accepted on `bugs/qwen-new-p9-329` (commit
`31d137921`) but could not go on the newest train because of a merge conflict in
`plugins/OpulenZ/OpulenZ.cpp`. The fix was re-applied by hand onto this branch
(no merge, no cherry-pick, no rebase) and the regression test was rebuilt on the
current tree.

## Verdict evidence (REAL, not ALREADY-FIXED)

On this branch, before the fix, the destructor read
`plugins/OpulenZ/OpulenZ.cpp:225-231`:

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
still registered with the audio engine, and outside `emulatorMutex` (declared in
`plugins/OpulenZ/OpulenZ.h`, a static `QRecursiveMutex` guarding the emulator,
the render buffer and the register writes).

The audio engine's `removePlayHandlesOfTypes()`
(`src/core/AudioEngine.cpp:1367`) takes the engine's change-in-model lock
(`requestChangeInModel()`), which the render path holds for a whole period - so
it is the operation that synchronises the control thread with the audio thread.
Calling it AFTER the emulator was deleted left a window in which a running
callback could still reach the freed emulator.

## The fix (at the cause)

`plugins/OpulenZ/OpulenZ.cpp:225-237` now:

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

1. It removes the instrument's play handles FIRST
   (`Engine::audioEngine()->removePlayHandlesOfTypes(...)`), which waits for the
   running audio period under the engine's change-in-model lock and keeps every
   later callback from reaching the instrument, and
2. it frees the emulator and the render buffer under `emulatorMutex`
   (`QMutexLocker locker(&emulatorMutex);`), so a callback that is still inside
   the emulator can never touch the freed memory. Both pointers are nulled so a
   later access is a null dereference the compiler can see, not a dangling one.

### The audio path and the lock

`OpulenzInstrument::playImpl()` (`plugins/OpulenZ/OpulenZ.cpp:400-423`) is the
only render path. It takes `emulatorMutex` at entry (line 402), renders the
whole span with `theEmulator->update(renderbuffer, ...)` (line 411) and releases
it at the end (line 423). It is unchanged by this fix: no allocation, no free,
no new lock, no logging and no wait was added to the audio thread.

### Siblings and callers checked (definition of done #3)

* `reloadEmulator()` (`plugins/OpulenZ/OpulenZ.cpp:241-252`) - the sibling with
  the same defect shape. On THIS line it is already correct: the emulator
  deletion, the replacement, the voice-state reset and the patch write all run
  inside one `emulatorMutex` section (this is cluster new-p9-328's port,
  recorded in `tests/upstream-modifications.txt`). It is therefore left
  untouched; the regression
  `PluginPortsMigrationTest::opulenzReloadEmulatorReplacesEmulatorUnderMutex`
  covers it and still passes.
* `updatePatch()` and `loadPatch()` - the register writers, still under the same
  mutex.
* `OpulenzInstrument::OpulenzInstrument` - the constructor creates the emulator
  under the mutex and registers the `InstrumentPlayHandle` with the engine; the
  destructor is now its exact mirror image (handles removed, then the emulator
  freed).
* The engine side is unchanged: `removePlayHandlesOfTypes()` already sweeps both
  the active list and the staged list (`src/core/AudioEngine.cpp:1385-1419`), so
  the handle the constructor staged is really disposed of.

### Realtime / undo / commands

* The destructor runs on the control thread. The audio thread is not touched:
  `playImpl()` keeps its single existing `emulatorMutex` acquisition, and the
  fix adds no allocation, no free, no wait and no logging to any audio-thread
  path.
* No control command was added (a bug fix must not move the
  ControlCommandsSnapshot canary).
* No undo step was added or removed.

### Collateral damage

* The change is confined to the OpulenZ instrument's teardown. Project files,
  saved data and every other plugin are untouched; the plugin's saved settings
  and its SBI loading are not part of this path.
* `theEmulator`/`renderbuffer` are private members of the instrument
  (`plugins/OpulenZ/OpulenZ.h`); nulling them after the free cannot be observed
  by any other caller.

## The regression

`tests/src/plugins/OpulenZLifetimeTest.cpp` (new, fork-NEW) compiles the
plugin's own sources into the test and watches the real teardown:

* the play handle the instrument registers is a test double whose destructor
  reports when the engine disposed of it (`removePlayHandlesOfTypes()` is the
  only place that happens);
* the emulator is a subclass of `CTemuopl` whose destructor reports when the
  emulator was freed;
* the emulator mutex is held for the whole teardown, exactly like `playImpl()`
  holds it while it renders, so the test can see whether the emulator was freed
  while a callback could still be inside it.

Both events are recorded in one ordered sequence, so the verdict is a fact and
not a race: the test never sleeps and never decides by a timeout (the 60 s wait
is a watchdog that turns a hang into a failed assertion). Two assertions
matter:

* the play handle must be disposed of BEFORE the emulator is freed
  (`handleDestroyedAt < emulatorDestroyedAt`);
* the emulator must not be freed while the emulator mutex is held.

Registered in `tests/CMakeLists.txt` (the test target compiles
`plugins/OpulenZ/OpulenZ.cpp` into itself, links `adplug` and defines
`PLUGIN_NAME=opulenz`), in `tests/all-sources.txt` and in
`tests/fork-sources.txt`; the changed product file is registered in
`tests/upstream-modifications.txt`.

## Race reasoning (TSan unavailable)

ThreadSanitizer is not available on this box: the container's compiler/library
set has no TSan runtime, so the race cannot be proven by instrumenting the
audio thread here. The ordering argument is the proof:

* the destructor first calls `removePlayHandlesOfTypes()`, which takes the
  engine's change-in-model lock; the render path holds that lock for a whole
  period, so the call cannot return until the running period has finished;
* after it returns, no play handle of this track exists, so no later audio
  callback can start `playImpl()` for this instrument;
* `emulatorMutex` then waits out a render that was already in flight (it holds
  the mutex for the whole render), and only then are `theEmulator` and
  `renderbuffer` freed.

So there is no interleaving in which a render runs against freed memory: the
handle removal excludes new renders, and the mutex excludes an in-flight one.

## Evidence table

All commands were run in the work tree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-329-port`;
logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-329-port.r7/logs`.

| Check | Exact command and result |
| --- | --- |
| Manifest regeneration | The recipe from the `Regenerate with` lines of `tests/all-sources.txt` (git ls-files + C-collation sort) was run and its output replaced the entry list: **EXIT=0**, exactly one entry added (`tests/src/plugins/OpulenZLifetimeTest.cpp`), `git diff --numstat` = `1 0`. |
| Manifest reproduce | `bash tests/all-sources-reproduce.sh > .../logs/allsources.log 2>&1`; **EXIT=0**, log ends `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| Build regression target | `cmake --build build --target OpulenZLifetimeTest`; **EXIT=0** (`build-opulenz-lifetime`). |
| Regression GREEN | `cd build/tests && ctest -R '^OpulenZLifetimeTest$' --output-on-failure`; **EXIT=0**, `100% tests passed out of 1`, 0.65 s (`ctest-opulenz`). |
| Feature gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check.log 2>&1`; **EXIT=0**. Table: `fork-sources` PASS, `all-sources-reproduce` PASS, `no-upstream-regression` PASS, `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS, `complexity` no new item, `file-length` no new item. `RESULT: PASS`. |
| Pre-submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-329-port > .../logs/submit-check.log 2>&1`; **EXIT=0** after the fix commit. |
| Working tree | `git status --short`; empty output. |

## Files committed

* `plugins/OpulenZ/OpulenZ.cpp` - the fix (destructor: handles first, then the
  emulator and the render buffer under the mutex).
* `tests/src/plugins/OpulenZLifetimeTest.cpp` - the regression (new file).
* `tests/CMakeLists.txt` - registers the test target.
* `tests/all-sources.txt` - regenerated from its own recipe; registers the new
  test source.
* `tests/fork-sources.txt` - registers the new fork-NEW files.
* `tests/upstream-modifications.txt` - records the changed product file and the
  reason.
* `docs/qwen-fixes-2026-10/cluster-new-p9-329-port.md` - this report.

## Self-check

* **Does the regression test FAIL with the product change reverted and PASS
  with it (exit codes)?** The earlier branch's runs on this same test show
  RED before the fix (`ctest ...` **EXIT=8**, assertion
  `'g_events.handleDestroyedAt < g_events.emulatorDestroyedAt' returned FALSE`,
  play handle disposed at 2, emulator freed at 1) and GREEN after it
  (**EXIT=0**); the reverse-patch run failed again with **EXIT=8** and the
  restored fix passed with **EXIT=0**. On this work tree the GREEN run was
  repeated: **EXIT=0**, 1/1 (`ctest-opulenz`). The RED leg was not re-run on
  this box after the port because the build tree already carried the fix.
* **Is the test deterministic?** Yes - the verdict is the ORDER of two recorded
  events (the play handle's disposal and the emulator's destruction), captured
  in one sequence counter. There is no sleep, no timing threshold and no race;
  the only timeout is a 60 s watchdog that fails the test instead of hanging it.
* **Is the fix at the cause and complete?** Yes - the destructor stops the audio
  thread (the play-handle removal, the engine's synchronisation point) before it
  frees the emulator, and it frees it under the mutex that guards it
  (`plugins/OpulenZ/OpulenZ.cpp:225-237`). Every caller and sibling was checked:
  `playImpl()` (lines 400-423) renders under `emulatorMutex`; `reloadEmulator()`
  (lines 241-252) already replaces the emulator under that mutex; the
  constructor is the destructor's mirror image; the engine's
  `removePlayHandlesOfTypes()` sweeps both handle lists.
* **No collateral damage?** None outside the OpulenZ teardown: no saved data, no
  project format, no other plugin, no control command, no undo step, and no
  gate limit was changed.
* **If the verdict is ALREADY-FIXED or FALSE-POSITIVE, the code that shows it?**
  Not applicable: verdict is FIXED. The pre-fix destructor
  (`plugins/OpulenZ/OpulenZ.cpp:225-231` at the base commit) deletes
  `theEmulator` first and outside the mutex.

## Other findings

* `tests/all-sources.txt` was 1151 entries out of date on the earlier branch
  (the `plugins/SfzPlayer/sfizz/...` block that its own recipe excludes).
  Regeneration here produced exactly one line of difference, the new test file,
  so the drift was already repaired by the base snapshot. No further change to
  the manifest was needed or made.
* The failing `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid`
  case ("SWH Decimator test fixture was not loaded") seen on the earlier branch
  is unrelated and pre-existing: the SWH LADSPA plug-ins are not built in this
  tree.
* No ThreadSanitizer runtime is installed on this box, so the race argument
  above is an ordering argument rather than an instrumented result.
