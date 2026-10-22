# Cluster 2647: reading the automation ramp took the value-buffer mutex (batch: 2647, 5524, new-p15-273)

Verdict: FIXED (2647: ALREADY-FIXED, 5524: FIXED, new-p15-273: FIXED)

2647: ALREADY-FIXED
5524: FIXED
new-p15-273: FIXED

Same-file batch in `src/core/AutomatableModel.cpp`. One commit per bug:
`6b6813826` (5524), `ff99d9ec0` (new-p15-273), `afd5839a7` (2647 regression).

> **Where the logs are.** The prescribed log folder,
> `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2647.r2/logs`, is read-only in this
> session's sandbox: `touch` there (and in every ancestor up to `~/.local/share/zene-bunny-fix`)
> fails with `EACCES`, and the same restriction stops `feature_check.py` from creating
> `<worktree>-feature-check/`. Every build/test log named below was therefore written to the
> git-ignored `build/2647-logs/` inside the worktree, and the gate commands were run directly
> (see "Gates"). The compile/test commands and exit codes in the tables are the exact ones that
> ran; the command text names `build/2647-logs/NN-*.log` because that is where the output is.

## 2647: ALREADY-FIXED

The finding says `valueBuffer()` acquires `m_valueBufferMutex` before the new ramp check
(historically `src/core/AutomatableModel.cpp:540`). On this branch that is no longer true:

- `src/core/AutomatableModel.cpp:752-777` - `valueBuffer()` takes **no mutex at all**. The
  guarded, control-side query and the realtime branch are both lock-free; the realtime branch
  only reads the period stamp and the prepared cache.
- `src/core/AutomatableModel.cpp:884-919` - the ramp itself is read in
  `prepareValueBufferForRender()` (`m_automationRampPeriod == s_periodCounter`,
  line 885), which the serial render prepass (`AudioEngine::renderStageNoteSetup()`,
  `src/core/AudioEngine.cpp:874`) and the render-owner's own `valueBuffer()` call own. There is
  no `QMutexLocker` and no `.lock()` anywhere in `AutomatableModel.cpp`
  (`rg -n 'QMutexLocker|\.lock\(\)|tryLock' src/core/AutomatableModel.cpp` is empty).
- `include/AutomatableModel.h:663-664` - the mutex survives only as a **test sentinel**
  ("Legacy lock retained as a test sentinel; product code no longer acquires it").

This was landed by the earlier commits on the branch (`c28751cef` "prepare model buffers before
worker jobs" and `8c837f075` "prove prepass render equivalence"), both ancestors of `HEAD`.
The finding is real against the code it was written on; it is fixed here.

Evidence it is fixed, and that the tests can see a regression:

- New deterministic regression `AutomatableModelTest::readingTheAutomationRampTakesNoMutex`
  (`tests/src/core/AutomatableModelTest.cpp:273`). It drives the prepass and the ordinary
  `valueBuffer()` query for a published ramp. At a test seam immediately after the ramp read
  (`src/core/AutomatableModel.cpp:909-916`) it asks the legacy sentinel with
  `QVector`-free `QMutex::tryLock(0)`, which **never blocks**: no thread, sleep, clock or race.
- RED proof by mutation (reinstating the lock the finding describes around the ramp branch
  only, `QMutexLocker mutationLock(&m_valueBufferMutex)` at
  `prepareValueBufferForRender()`'s ramp branch): the new test fails with exactly the cluster's
  assertion; the mutation touched nothing else and every other test in the binary still passed.
- The already-committed `PhaseDSidechainTest` probes also catch a lock reinstated for the whole
  of `valueBuffer()` (3 failures, exit 8), which is the second, independent RED witness.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| baseline build | `cmake --build build --target zene AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/01-build-baseline.log` |
| GREEN build (seam + test) | `cmake --build build --target zene AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/27-build-2647-seam4.log` |
| GREEN test | `ctest -R '^AutomatableModelTest$' --output-on-failure` (cwd `build/tests`) | 0 | `build/2647-logs/28-test-2647-green2.log` |
| RED build (mutation) | `cmake --build build --target AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/31-build-2647-reverted2.log` |
| RED test (mutation) | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 8 | `build/2647-logs/32-test-2647-reverted2.log`; `readingTheAutomationRampTakesNoMutex` failed: "reading the automation ramp took the model's value-buffer mutex (cluster 2647)", 11 passed / 1 failed |
| existing probes under a whole-`valueBuffer()` mutation | `ctest -R '^PhaseDSidechainTest$' --output-on-failure` | 8 | `build/2647-logs/22-test-2647-mutation-existing-probes.log`; `theSidechainAmountReadTakesNoLock`, `mixerFaderBufferLookupDoesNotWaitOnCacheLock`, `pluginParameterBufferLookupDoesNotWaitOrAllocate` all failed, 17 passed / 3 failed |
| GREEN build (mutation reverted) | `cmake --build build --target zene AutomatableModelTest PhaseDSidechainTest AutomationPrepassProofTest -j$(nproc)` | 0 | `build/2647-logs/33-build-2647-restored.log` |
| GREEN family | `ctest -R '^(AutomatableModelTest\|PhaseDSidechainTest\|AutomationPrepassProofTest)$' --output-on-failure` | 0 | `build/2647-logs/34-test-2647-restored-green.log`; 3/3 CTest tests passed |

## 5524: FIXED

`src/core/AutomatableModel.cpp:438-449` (pre-fix) stored `s_lastTouched` for every
non-automated `setValue()`, and both `setInitValue()` (line 963) and `loadSettings()`
(lines 294 and 391) call it. So loading a project left `automation.last_touched` naming
whichever model the file wrote last.

Fix, at the cause and covering every path into the store:

- `src/core/AutomatableModel.cpp:91-119` - `projectIsLoading()` reads `Song::isLoadingProject()`,
  and `InitValueScope` raises/lowers an atomic "this write is not a user touch" flag (atomic
  because the last-touched store is read from any thread, a render included; nested scopes
  restore the previous value).
- `src/core/AutomatableModel.cpp:321-328` - `loadSettings()` opens that scope, so every write it
  makes - the automation-compat `setValue(p->valueAt(0))`, the element `setValue(...)`,
  `setInitValue(...)` and the `reset()` on absence - is a load, not a touch.
- `src/core/AutomatableModel.cpp:434-449` - `setValue()` records the touch only when the flag
  is clear **and** the project is not loading.
- `src/core/AutomatableModel.cpp:995-1002` - `setInitValue()` opens the scope too (constructor
  defaults, plugin defaults, `Song::createNewProject()`).

The value, the journal checkpoint, `dataChanged`/`dataUnchanged` and the save format are
untouched; a person's (or command's) write still claims the store.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/02-build-5524-red.log` |
| RED test | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 8 | `build/2647-logs/03-test-5524-red.log`; `initializationAndLoadingAreNotTouches` failed at the `setInitValue()` comparison (lastTouched = 8388950, expected edited.id() = 8390383) |
| GREEN build | `cmake --build build --target zene AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/04-build-5524-green.log` |
| GREEN test | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 0 | `build/2647-logs/05-test-5524-green.log`; 100% passed |
| step 4b: reverse the product change | `git diff -- src/core/AutomatableModel.cpp > build/2647-logs/fix-5524.patch; git apply -R build/2647-logs/fix-5524.patch` | 0 | patch written, reversed cleanly |
| RED build (reverted) | `cmake --build build --target AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/06-build-5524-reverted.log` |
| RED test (reverted) | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 8 | `build/2647-logs/07-test-5524-reverted.log`; `initializationAndLoadingAreNotTouches` failed, 9 passed / 1 failed |
| restore | `git apply build/2647-logs/fix-5524.patch` | 0 | patch reapplied |
| GREEN build (restored) | `cmake --build build --target zene AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/08-build-5524-restored.log` |
| GREEN test (restored) | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 0 | `build/2647-logs/09-test-5524-restored.log` |

The regression covers the attribute form, the element form and the empty/reset form of a load,
plus `setInitValue()`, and ends by proving a person's write still claims the store. It is
deterministic: it writes DOM elements directly and calls the load entry points; no timing.

## new-p15-273: FIXED

`setControllerConnection()` (pre-fix `src/core/AutomatableModel.cpp:639-651`) assigned
`m_controllerConnection = c` without touching the old value: the old connection was leaked, its
`valueChanged()` was still wired to `dataChanged`, and its `destroyed()` -> 
`unlinkControllerConnection()` handler stayed connected. When it was later destroyed,
`unlinkControllerConnection()` cleared whatever connection the model held then - the NEW one -
and disconnected its signals.

Fix:

- `src/core/AutomatableModel.cpp:732-749` - `setControllerConnection()` captures the previous
  connection, publishes the new one, then `disconnect(this)` and deletes the previous one (the
  model already owns its connection: the destructor deletes it). Disconnecting first removes the
  `destroyed()` handler, so disposal cannot reach this model.
- `src/core/AutomatableModel.cpp:1041-1060` - `unlinkControllerConnection()` ignores a sender
  that is not the connection the model currently holds, so a dying replaced connection can never
  clear its replacement. `sender() == nullptr` (a direct call) keeps the historic meaning.
- Callers/siblings kept consistent:
  `src/gui/AutomatableModelView.cpp:245-254` - `removeConnection()` no longer deletes the
  connection before clearing the pointer (the model disposes of it now, so delete-then-clear
  would be a double free); the `AutomationPrepassProofTest` teardown no longer deletes
  connections it handed to a model (`tests/src/core/AutomationPrepassProofTest.cpp:132-152`).
  `include/AutomatableModel.h:176-183` documents the ownership rule.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/10-build-p15-273-red.log` |
| RED test | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 8 | `build/2647-logs/11-test-p15-273-red.log`; `replacingTheControllerConnectionGivesUpTheOldOne` failed: "the replaced controller connection was not disposed of" |
| GREEN build | `cmake --build build --target zene AutomatableModelTest AutomationPrepassProofTest PhaseDSidechainTest -j$(nproc)` | 0 | `build/2647-logs/12-build-p15-273-green.log` |
| GREEN family | `ctest -R '^(AutomatableModelTest\|AutomationPrepassProofTest\|PhaseDSidechainTest)$' --output-on-failure` | 0 | `build/2647-logs/13-test-p15-273-green.log`; 3/3 passed |
| step 4b: reverse the product change | `git diff -- src/core/AutomatableModel.cpp src/gui/AutomatableModelView.cpp > build/2647-logs/fix-p15-273.patch; git apply -R build/2647-logs/fix-p15-273.patch` | 0 | patch written, reversed cleanly |
| RED build (reverted) | `cmake --build build --target AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/14-build-p15-273-reverted.log` |
| RED test (reverted) | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 8 | `build/2647-logs/15-test-p15-273-reverted.log`; 10 passed / 1 failed |
| restore | `git apply build/2647-logs/fix-p15-273.patch` | 0 | patch reapplied |
| GREEN build (restored) | `cmake --build build --target zene AutomatableModelTest AutomationPrepassProofTest -j$(nproc)` | 0 | `build/2647-logs/16-build-p15-273-restored.log` |
| GREEN test (restored) | `ctest -R '^(AutomatableModelTest\|AutomationPrepassProofTest)$' --output-on-failure` | 0 | `build/2647-logs/17-test-p15-273-restored.log` |
| header rebuild | `cmake --build build --target zene AutomatableModelTest -j$(nproc)` | 0 | `build/2647-logs/18-build-p15-273-header.log` |
| header retest | `ctest -R '^AutomatableModelTest$' --output-on-failure` | 0 | `build/2647-logs/19-test-p15-273-header.log` |

The regression is deterministic: it creates two `ControllerConnection`s, swaps them, and also
connects a third to the model's `unlinkControllerConnection()` slot and deletes it, checking with
`QPointer` that the replaced connection was disposed of while the current one survived. No
threads and no timing.

## Gates

`feature_check.py` wants to write its per-gate logs to `<worktree>-feature-check/`, which this
sandbox refuses (`PermissionError: [Errno 13] ... zene-qwen-2647-feature-check`, exit 1 before
any gate runs - `build/2647-logs/36-feature-check.log`; the same `mkdir` is denied for the
worktree's parent directory at the syscall level, and so is every write outside the worktree).
It was therefore run with only that one log directory redirected into the (git-ignored)
`build/2647-logs/feature-check/` - the script itself, its gate commands, its baseline lookup
and its scoring run unmodified - and the eight gates were also run directly from the worktree.
Both forms agree:

| Gate | Exit | Evidence |
| --- | ---: | --- |
| fork-sources | 0 | `build/2647-logs/46-forksources-repaired3.log` - 1976 fork-NEW, 1214 inherited, 48 tooling, 0 stale |
| all-sources-reproduce | 0 | `build/2647-logs/45-allsources-repaired3.log` - REPRODUCES |
| no-upstream-regression | 0 | `build/2647-logs/38-gate-no-upstream-regression.log` - PASS |
| check-namespace | 0 | `build/2647-logs/gate-check-namespace.log` |
| no-tautology | 0 | `build/2647-logs/gate-no-tautology.log` |
| doc-figures | 0 | `build/2647-logs/gate-doc-figures.log` |
| complexity | 0 | `build/2647-logs/gate-complexity.log` - "no new item in your files" |
| file-length | 0 | `build/2647-logs/gate-file-length.log` - "no new item in your files" |
| full `feature_check.py` (redirected log dir) | 0 | `build/2647-logs/50-feature-check-final-shim.log` - `RESULT: PASS`, all eight rows PASS |
| RT safety sweep | 0 | `build/2647-logs/39-rt-sweep-after-ledger.log` - PASS; the new seam is `#ifdef LMMS_TESTING` and touches no declared audio-thread body |

**The two manifest gates were red on the train before this branch.** `tests/all-sources.txt`
carried 1148 `plugins/SfzPlayer/sfizz/**` entries its own header recipe excludes on purpose.
Reproduced against the untouched base copy of the manifest
(`bash tests/all-sources-reproduce.sh build/2647-logs/all-sources-base.txt` -> exit 1,
`build/2647-logs/37-allsources-base.log`). The manifest's header names exactly one repair -
re-derive with the recipe - so it was re-derived verbatim: **0 additions, 1148 removals, every
removed path under the header's excluded vendor tree, none of them in `tests/fork-sources.txt`**
(`build/2647-logs/42-allsources-repaired.log` is the failed first attempt; `44`-`46` are the
final state). Committed separately as `14e19eb4d` so the train-state repair is not confused with
the bug fixes; it changes no product code, test or gate limit.

## Files committed

- `src/core/AutomatableModel.cpp`, `include/AutomatableModel.h` - the 5524, new-p15-273 and
  2647 changes above.
- `src/gui/AutomatableModelView.cpp` - the double-free fix that follows from the new ownership.
- `tests/src/core/AutomatableModelTest.cpp` - the three regressions.
- `tests/src/core/AutomationPrepassProofTest.cpp` - teardown follows the new ownership.
- `tests/all-sources.txt` - the train-state manifest repair (separate commit `14e19eb4d`).
- `tests/upstream-modifications.txt` - ledger entries for every inherited product file above,
  same commits.
- `docs/qwen-fixes-2026-10/cluster-2647.md` - this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** 5524: reversed the product diff, rebuild exit 0, `ctest` exit **8**; restored,
  rebuild exit 0, `ctest` exit **0**. new-p15-273: reversed, rebuild exit 0, `ctest` exit **8**;
  restored, rebuild exit 0, `ctest` exit **0**. 2647 is ALREADY-FIXED, so the equivalent is a
  mutation that reinstates the historical lock around the ramp read: rebuild exit 0, `ctest`
  exit **8**, restored build exit 0, `ctest` exit **0** (tables above; logs
  `06/07/08/09`, `14/15/16/17`, `31/32/33/34`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
  values it uses).** 5524: DOM elements built in the test are fed to `loadSettings()` directly -
  no clock, no thread. new-p15-273: two `QPointer`-watched connections and one deliberately
  connected "stranger" are created and destroyed on the test thread - no clock, no thread. 2647:
  the test seam `AutomatableModel::setValueBufferRampReadProbeForTest()` runs immediately after
  the ramp read and the probe uses `QMutex::tryLock(0)`, which never blocks; the answer is "is
  the sentinel held at this point", not a race - no clock, no thread, no sleep.
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** 5524: all four load-time writes (automation-compat `setValue`,
  element `setValue`, `setInitValue`, `reset`) run inside the load scope, and `setInitValue()`
  covers the constructor/plugin/project-new default writes; the automation write path
  (`isAutomated`) and `dataChanged` semantics are untouched. new-p15-273: both callers that
  manually deleted a connection (`AutomatableModelView::removeConnection` and the prepass test
  teardown) were migrated so the model's single owner disposes of it; `unlinkControllerConnection`
  guards any sender that is not the held connection, and the destructor still deletes the
  current one. 2647: the seam is `#ifdef LMMS_TESTING` only and null in the product; the only
  changed product call site is the ramp branch it observes.
- **No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it.** The manifest repair
  removes 1148 paths the header already excludes and adds none, so nothing is un-measured that
  the recipe considers in scope. No save format, value-fitting, clamping, journal or signal
  contract changed. 5524 suppresses only the
  last-touched store, and only during a load/init (the project-loading window and the scoped
  init flag); a normal write still claims it, which the regression's last step asserts. The
  p15-273 change conforms to the existing destructor ownership rule (the model already deleted
  its connection); it removes a leak and a stale `destroyed()` handler instead of adding a new
  lifetime rule. Full suites for the touched areas passed: `AutomatableModelTest`,
  `PhaseDSidechainTest`, `AutomationPrepassProofTest` (3/3, log `34`), and the RT safety sweep
  exits 0.
- **Pre-submit check.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 2647`
  (`build/2647-logs/48-submit-check-final.log`) passes "commits on the branch", "working tree
  clean", "report", "tests added or changed" and "no new test file to register"; its single FAIL
  is "gates (feature_check.py)", and that is the sandbox `EACCES` on
  `<worktree>-feature-check/` described above, not a gate result. Run with only that directory
  redirected, `feature_check.py` exits 0 with all eight rows PASS
  (`build/2647-logs/50-feature-check-final-shim.log`), and every question it ASKs is answered by
  the bullets in this section.
- **2647 is ALREADY-FIXED - cite the code (file:line) or the intended contract that shows it.**
  `src/core/AutomatableModel.cpp:752-777` (`valueBuffer()` has no mutex and no lock call),
  `src/core/AutomatableModel.cpp:884-919` (the ramp is read in the lock-free prepass function,
  line 885), `include/AutomatableModel.h:663-664` (the mutex is documented as a test sentinel
  that product code no longer acquires), `rg -n 'QMutexLocker|\.lock\(\)|tryLock'
  src/core/AutomatableModel.cpp` -> no match. Introduced by ancestors `c28751cef` and
  `8c837f075`. The new probe test was added so the property cannot silently regress.
