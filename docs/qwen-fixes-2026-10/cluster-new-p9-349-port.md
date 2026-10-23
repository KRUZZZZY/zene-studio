# Cluster new-p9-349-port — "Loading a Patman patch races with audio-thread sample selection"

**Verdict: FIXED** — re-applied by hand on the newest line (the accepted
`bugs/qwen-new-p9-349` fix rebased onto `050/m0-train-12`); the race is real and the
patch state is now replaced under the engine's audio-safe model-change mechanism.

## The bug (evidence on this tree)

`plugins/Patman/Patman.cpp` — `PatmanInstrument::setFile()` replaced the patch state
(`m_patchFile`, and through `loadPatch()` the whole `m_patchSamples` bank) from the
control thread while the audio thread reads exactly that state while it renders:

- `plugins/Patman/Patman.cpp:207-238` `PatmanInstrument::playNoteImpl()` reads
  `m_patchFile` on the audio thread (the empty-path check and the note render).
- `plugins/Patman/Patman.cpp:483-513` `PatmanInstrument::selectSample()` iterates
  `m_patchSamples` and copies a `std::shared_ptr<Sample>` out of it on the audio
  thread.
- `plugins/Patman/Patman.cpp:233-266` (pre-fix) `PatmanInstrument::setFile()` assigned
  `m_patchFile` (line 254) and called `loadPatch()`, which swapped `m_patchSamples`
  (line 430) with no synchronisation at all (`git show 77d08360f^:plugins/Patman/Patman.cpp`).

Nothing serialised the two sides: the control thread could replace the bank while the
audio thread was iterating it (an invalid `QVector` iteration and a `shared_ptr` copy out
of a container being mutated), and it could replace the `QString` path while the audio
thread was reading it.

## The fix (re-applied by hand)

The fork already has the contract for this: `AudioEngine::renderNextPeriod()`
(`src/core/AudioEngine.cpp:973` and its `std::lock_guard{m_changeMutex}` on line 975)
holds `m_changeMutex` for a whole render period, and `AudioEngine::requestChangesGuard()`
(`include/AudioEngine.h:552`) is the RAII handle a control thread takes before it replaces
model state.

- `plugins/Patman/Patman.cpp:105-116` — new `patchChangeGuard()` helper: the engine's
  request-changes guard, or an empty guard when there is no engine (shutdown).
- `plugins/Patman/Patman.cpp:252-258` — the empty-path branch of `setFile()` clears the
  bank and the path under the guard.
- `plugins/Patman/Patman.cpp:272-277` — the patch path is replaced under the guard.
- `plugins/Patman/Patman.cpp:279-293` — the patch file is still read *outside* the guard
  (file I/O must not stall the render thread); the new sample set is published under the
  guard (line 292).
- `plugins/Patman/Patman.cpp:160-166` — the destructor drops the bank under the guard too.
- `plugins/Patman/Patman.h:113-117` and `plugins/Patman/Patman.cpp:301-303` —
  `loadPatch()` now hands the parsed sample set to its caller instead of publishing it
  itself, so only the publication is guarded.

Nothing changed on the audio thread: `playNoteImpl()` and `selectSample()` are untouched,
no allocation, lock, wait or log was added to the render path, no undo step and no control
command was added.

Behaviour deliberately preserved: a project or patch that fails to load still leaves the
previously loaded bank in place while the path names the new file (the swap is still only
on `LoadError::OK`); the instrument-track rename, the `fileChanged()` signal, the save
format (`src` attribute) and the empty-path branch are unchanged, so old projects and
saved data load exactly as before.

## The ported engine seam (round 4 of the accepted fix)

The train's `include/AudioEngine.h`/`src/core/AudioEngine.cpp` conflicted with the accepted
branch, so the seam was re-applied by hand:

- `include/AudioEngine.h:557-565` — `modelChangeWaiters()` reads
  `m_modelChangeWaiters`: control threads parked inside `requestChangeInModel()`. It is
  declared unconditionally (`include/AudioEngine.h:803-804`) so every translation unit
  agrees on the class layout; the rendering thread short-circuits before it is touched.
- `src/core/AudioEngine.cpp:1448-1461` — `requestChangeInModel()` increments the counter
  around the `m_changeMutex.lock()`, so a test can decide on an event ordering instead of
  a clock deadline. The `LMMS_TESTING` nesting-depth bookkeeping (`isChangeInModelHeld()`)
  from `cluster new-p14-98` is preserved unchanged.

## The regressions

Two deterministic regressions, both using the engine's model-change mutex as the
synchronisation hook (the same idiom `tests/src/core/InstrumentRoutingTest.cpp` and
`tests/src/core/MixerConcurrencyTest.cpp` use): the test thread holds the mutex — a render
period in flight — and a second thread loads a patch. While the period owns the model
state the load either parks inside the guard (counted by `modelChangeWaiters()`) or
completes without waiting; whichever happens first decides. No sleeps, no wall-clock
answer.

- `tests/src/plugins/PatmanLoadTest.cpp:191-241`
  `PatmanLoadTest::patmanPatchLoadWaitsForTheRenderPeriodToReleaseTheModel()` — the
  cross-platform one; it drives the Patman implementation compiled into the target and
  watches the published patch path through the public `saveSettings()`.
- `tests/src/plugins/AudioPluginTest.cpp:674-747`
  `AudioPluginTest::patmanPatchLoadWaitsForTheRenderPeriod()` — the bank-level one; it
  loads the *shipped* `libpatman` module and checks that `m_patchSamples` is not replaced
  while the render period owns the model state, and that the deferred load lands after.

## Evidence

All build/test output is in `build/qwen-logs/new-p9-349-port.r1/` (the briefing's log
folder under `~/.local/share/zene-bunny-fix/` was not writable from this session — the
same sandbox that denies writes outside the worktree and `/tmp`; see Notes).

| Check | Command | Exit |
|---|---|---:|
| Build, tests + product | `CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target PatmanLoadTest AudioPluginTest patman -j20 > build/qwen-logs/new-p9-349-port.r1/build-1.log 2>&1` | 0 |
| RED, original code | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest)$' --output-on-failure > .../test-red-2.log 2>&1` | 8 |
| RED assertion | `test-red-2.log` | `FAIL!  : lmms::PatmanLoadTest::patmanPatchLoadWaitsForTheRenderPeriodToReleaseTheModel() '!completedWhileRenderPeriodOwned' returned FALSE. (the patch was replaced while the render period owned the model state)` and the same for `AudioPluginTest::patmanPatchLoadWaitsForTheRenderPeriod()` |
| Build, fix applied | `CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target PatmanLoadTest AudioPluginTest patman zene -j20 > .../build-3.log 2>&1` | 0 |
| GREEN, fixed code | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest)$' --output-on-failure > .../test-green-1.log 2>&1` | 0 |
| Save patch | `git diff -- plugins/Patman/Patman.cpp plugins/Patman/Patman.h > .../fix.patch` | 0 |
| Reverse patch | `git apply -R .../fix.patch` | 0 |
| Build, fix reverted | `CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target PatmanLoadTest AudioPluginTest patman -j20 > .../build-4.log 2>&1` | 0 |
| RED, reverse-patch proof | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest)$' --output-on-failure > .../test-red-3.log 2>&1` | 8 |
| Restore patch | `git apply .../fix.patch` | 0 |
| Build, restored | `CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target PatmanLoadTest AudioPluginTest patman zene -j20 > .../build-5.log 2>&1` | 0 |
| GREEN, restored | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest)$' --output-on-failure > .../test-green-2.log 2>&1` | 0 |
| Affected family | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest\|ControlDeviceCatalogueTest)$' --output-on-failure > .../test-final.log 2>&1` | 0 (3/3) |
| QtTest function counts | `cd build/tests && ctest -R '^(PatmanLoadTest\|AudioPluginTest)$' -V --output-on-failure > .../test-family-verbose.log 2>&1` | 0 — `PatmanLoadTest`: 4 passed, 0 failed; `AudioPluginTest`: 24 passed, 0 failed |
| Gates | `PYTHONPATH=/tmp/qwen-gate-shim python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gate-2.log 2>&1` | 0 — all 8 rows PASS |
| Completion check | `PYTHONPATH=/tmp/qwen-gate-shim python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-349-port > .../submit-1.log 2>&1` | 0 — `RESULT: PASS - submit` |

The RED and GREEN runs each executed 2 CTest tests; the family run executed 3 (the third,
`ControlDeviceCatalogueTest`, renders a Patman note and asserts a truncated patch keeps the
previously loaded sound).

## Notes on the environment

- The briefing's LOG FOLDER
  (`~/.local/share/zene-bunny-fix/qwen/runs/new-p9-349-port.r1/logs`) exists but this
  session's file sandbox denies creating any file in it (or anywhere outside the worktree
  and `/tmp`): `touch` there returns `Permission denied`, and the `write` tool returns
  `EACCES`. Every log named above therefore lives in the worktree's build tree, which is
  git-ignored (`build*/`), and the exact content is unchanged.
- `feature_check.py` writes its per-gate logs to `<worktree>-feature-check` beside the
  worktree; that directory is equally unwritable here. A run-local `sitecustomize.py` in
  `/tmp/qwen-gate-shim` redirects only that log directory to `/tmp/qwen-gate-logs`; the
  gate commands, limits, baselines and results are untouched. Run without it, the tool
  aborts with `PermissionError` before running any gate (`gate-1.log`).
- The first gate run was red on `fork-sources` and `all-sources-reproduce` on the
  **pristine** base as well: train commit `07d6d02bd` regenerated `tests/all-sources.txt`
  with a broader command than the manifest's own recipe, adding 1148 vendored
  `plugins/SfzPlayer/sfizz/external/**` paths the recipe excludes on purpose (the vendored
  sfizz tree is explicitly out of scope in the manifest header). The identical failure
  reproduces in the sibling `zene-cachyos` checkout at the same commit. `tests/all-sources.txt`
  was re-derived from its own published `Regenerate with` command — 1148 vendored lines
  removed, nothing added, no entry the recipe derives removed, and no gate limit or
  baseline changed. `tests/all-sources-reproduce.sh` now prints `REPRODUCES`.

## Self-check

- **Does the regression FAIL with the product change reverted and PASS with it (exit
  codes)?** Yes. Original code: build exit 0 (`build-1.log`), ctest **exit 8**
  (`test-red-2.log`), both new tests failing on `'!completedWhileRenderPeriodOwned'
  returned FALSE. (the patch was replaced while the render period owned the model state)`.
  Fix applied: build exit 0 (`build-3.log`), ctest **exit 0** (`test-green-1.log`).
  Reverse-patch proof: `git diff -- plugins/Patman/Patman.cpp plugins/Patman/Patman.h >
  .../fix.patch` exit 0, `git apply -R .../fix.patch` exit 0, rebuild exit 0
  (`build-4.log`), ctest **exit 8** (`test-red-3.log`), same two failures; `git apply
  .../fix.patch` exit 0, rebuild exit 0 (`build-5.log`), ctest **exit 0**
  (`test-green-2.log`).
- **Is the test deterministic (no wall-clock timing, sleeps or races)?** Yes. The seam is
  the engine's own model-change mutex: the test thread takes it with
  `AudioEngine::requestChangeInModel()` — exactly what `renderNextPeriod()` holds for a
  render period — and releases it with `doneChangeInModel()`. The loader thread publishes
  completion through one `std::atomic<bool>`; the pass/fail decision is the boolean "did
  the load finish while the period owned the model state", decided against
  `AudioEngine::modelChangeWaiters()`, never an elapsed time. Nothing in the test races:
  the instrument's state is read only after the loader thread has been joined.
- **Is the fix at the cause and complete?** Yes: the cause is the unguarded replacement of
  the patch state the audio thread reads. `loadPatch()` has one caller (`setFile()`) and
  its declaration changed with its definition (`Patman.h:113-117` / `Patman.cpp:301-303`);
  `unloadCurrentPatch()` has two callers (the empty branch of `setFile()` and the
  destructor) and both are guarded now. The audio-thread readers (`playNoteImpl()`,
  `selectSample()`) needed no change — the state they read is now replaced atomically with
  respect to a render period. The one other reader, `AudioPluginTest.cpp:657-662`, still
  sees the same `m_patchSamples` member.
- **No collateral damage?** The save format (`src` attribute), the track rename, the
  `fileChanged()` signal, the empty-path branch and the load-failure behaviour (the
  previously loaded bank is kept while the path names the new file) are unchanged, so old
  projects and saved data load exactly as before. No limit or check was added, so no other
  caller can trip over one. `ControlDeviceCatalogueTest` (renders a Patman note and asserts
  a truncated patch keeps the previously loaded sound) passes.
- **Realtime?** The guard is taken on the control thread; the render path is unchanged. No
  allocation, lock, wait, I/O or log was added to the audio thread, and the patch file is
  read outside the guard so the render thread is never stalled by file I/O. The
  `requestChangeInModel()` counter is two relaxed atomic ops on control-thread paths only;
  the rendering thread returns before them.
- **Undo and commands?** No new undo step and no new control command; the fix does not
  touch the `ControlCommandsSnapshot` canary.
- **Every caller and sibling checked?** `loadPatch()` — one caller, signature changed with
  its definition. `unloadCurrentPatch()` — two callers, both guarded. `m_patchFile` —
  readers are `playNoteImpl()`, `saveSettings()`, `PatmanView` GUI paths and
  `PatmanView::dragEnterEvent`; all are control-thread except the audio-thread read, which
  the guard now orders against the writes. `setFile()` — GUI open, drag-and-drop and
  `loadSettings()`; all take the guard through the same helper.
- **Old projects and saved data?** Unchanged save format and load semantics; the family
  tests plus `ControlDeviceCatalogueTest` cover the round trip and the load-failure path.
- **Other tests in the family?** `PatmanLoadTest` (4 functions) and `AudioPluginTest`
  (24 functions) both pass, plus `ControlDeviceCatalogueTest`.
- **ALREADY-FIXED / FALSE-POSITIVE?** Neither: the pre-fix unguarded assignment and swap
  are shown above at `Patman.cpp:254` and `Patman.cpp:430` (before the change), and the
  RED runs fail on exactly that.

## Files committed

- `plugins/Patman/Patman.cpp`
- `plugins/Patman/Patman.h`
- `include/AudioEngine.h`
- `src/core/AudioEngine.cpp`
- `tests/src/plugins/PatmanLoadTest.cpp`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (re-derived from its own recipe; train-wide defect, see Notes)
- `docs/qwen-fixes-2026-10/cluster-new-p9-349-port.md`
