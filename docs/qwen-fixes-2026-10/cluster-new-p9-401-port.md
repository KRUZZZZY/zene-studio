# Cluster new-p9-401-port

FIXED

This is a **port**, not a new review: the fix for cluster `new-p9-401` was accepted on
`bugs/qwen-new-p9-401` (commits `db350fe49`, `69ccd9798`, `b5462e5e9`, plus the two report commits)
but could not go on the newest train because it conflicted with the newer line in
`plugins/ReverbSC/ReverbSC.cpp` and `plugins/ReverbSC/revsc.c`. The fix was re-applied **by hand**
on this branch (`bugs/qwen-new-p9-401-port`, base `b61e5f965`): no merge, no cherry-pick, no rebase.

The finding under port is **new-p9-401** — "Reverb sample-rate changes can free state while audio
processing uses it" (`plugins/ReverbSC/ReverbSC.cpp`, pre-fix line 92 in the patrol's numbering).

## Verdict per bug of the ported branch

| Bug | Verdict on this line | Evidence |
|---|---|---|
| new-p9-401-port | **FIXED** | this port: the fix of `bugs/qwen-new-p9-401` re-applied by hand on this branch. |
| new-p9-401 (the finding of this job) | **FIXED** (ported) | the pre-fix code freed the DSP state under a mutex the audio thread never took — see below. |
| new-p14-246 (sibling commit `db350fe49`) | **FIXED** (ported) | the pre-fix constructor wrote through the pointer `sp_create()` returned without checking its result; the ported `createState()` checks it. |
| new-p14-247 (sibling commit `69ccd9798`) | **FIXED** (ported) | the soundpipe-layer half of that commit is **already on the newest line** (cluster `new-p12-203`, commit `83018848f`): `plugins/ReverbSC/revsc.c:52` `sp_revsc_create()` reports `SP_NOT_OK` and leaves the pointer null, `plugins/ReverbSC/revsc.c:63` `sp_revsc_init()` refuses a state that was never created and reports a failed delay-line allocation, `plugins/ReverbSC/revsc.c:96` `sp_revsc_destroy()` releases nothing when there is no state, and `plugins/ReverbSC/base.c:165` `sp_auxdata_alloc()` reports a failed allocation. The effect-level half (checking `sp_revsc_create()`'s result before init) was **not** on this line and is ported. |

So the merge conflict named in the port job (`plugins/ReverbSC/revsc.c`) needed **no** porting: the
newest line already carries the equivalent, stricter soundpipe-layer contract. The conflict is
resolved by keeping the newest line's `revsc.c`/`base.c` untouched and porting only the effect.

## The bug (REAL on this line), evidence

Pre-fix, on this branch's base (`git show HEAD:plugins/ReverbSC/ReverbSC.cpp`):

* `plugins/ReverbSC/ReverbSC.cpp:133-156` — `changeSampleRate()` wrote the shared context's sample
  rate (`sp->sr = ...`, line 136) **before** taking the lock, then destroyed and recreated the
  reverb state and both DC blockers under `mutex` (lines 138-155).
* `plugins/ReverbSC/ReverbSC.cpp:83-131` — `processImpl()` never took that mutex, so the audio
  thread could call `sp_revsc_compute()` / `sp_dcblock_compute()` on a freed state (lines 123-125)
  while the main thread was inside `sp_revsc_destroy()`.
* `plugins/ReverbSC/ReverbSC.h:63` — the `QMutex mutex` member guarded nothing.

The signal that starts this path is the sample-rate-changed signal:
`plugins/ReverbSC/ReverbSCControls.cpp:43` connects `Engine::audioEngine() SIGNAL(sampleRateChanged())`
to `ReverbSCControls::changeSampleRate()` (`plugins/ReverbSC/ReverbSCControls.cpp:66-69`), which
calls `m_effect->changeSampleRate()`.

## The ported fix

The DSP state now lives in per-sample-rate states; each state carries **its own** soundpipe
context, so the sample rate of a running state is never written while the audio thread uses it.

* `plugins/ReverbSC/ReverbSC.h:61-85` — `struct DspState` (context + reverb + the two DC
  blockers), the three helpers `createState()` / `destroyState()` / `reclaimRetiredStates()` and
  the atomic hand-over members `m_activeState`, `m_pendingState`, `m_retiredStates[2]`,
  `m_retiredIndex`. The `QMutex` member is gone.
* `plugins/ReverbSC/ReverbSC.cpp:73-106` — `createState()` builds a complete state for one sample
  rate and checks every allocation (`sp_create()`, `sp_revsc_create()`, `sp_revsc_init()`,
  `sp_dcblock_create_and_init_pair()`); a state that could not be built leaves the effect
  bypassed.
* `plugins/ReverbSC/ReverbSC.cpp:201-227` — `changeSampleRate()` **prepares** the replacement state
  off the audio thread and publishes it with one atomic exchange. It does not touch the state the
  audio thread may be using; the states the audio thread left behind are released by the main
  thread (`reclaimRetiredStates()`, `plugins/ReverbSC/ReverbSC.cpp:127-134`) before the next
  publication and at destruction.
* `plugins/ReverbSC/ReverbSC.cpp:136-149` — `processImpl()` takes the prepared state over at the
  top of the block — the audio boundary — with one atomic exchange and hands the state it leaves
  behind to the main thread through the two-slot retired ring.

The audio thread allocates nothing, frees nothing, locks nothing, waits for nothing and logs
nothing: the hand-over is two atomic exchanges, one atomic fetch-add and one atomic store.

## The regression

`tests/src/plugins/ReverbSCPluginStateTest.cpp` (new, fork-NEW) compiles the effect's **own**
sources (`ReverbSC.cpp`, `ReverbSCControls.cpp`, `ReverbSCControlDialog.cpp` and the three
soundpipe C files) into the test binary, so the test drives the shipped plugin code, and builds the
soundpipe C files with `malloc`/`calloc`/`free` redirected to a shim in the test
(`tests/CMakeLists.txt`, target `ReverbSCPluginStateTest`). The shim is the seam:

* it can fail ONE allocation of a known size (`sizeof(sp_data)`, `sizeof(sp_revsc)`), which is how
  the effect is constructed while an allocation is failing, and
* it counts every free of a soundpipe block, which is how the sample-rate change is measured.

Each case runs in a child process (`--child <case>`), because the pre-fix code crashes the process
when an allocation fails; a crashed child is reported as a failed case instead of taking the whole
test binary down with it.

* `sampleRateChangeKeepsLiveStateAlive` — the case that carries this finding: it renders a block,
  records the number of soundpipe frees, calls `changeSampleRate()` and checks that **not one**
  soundpipe block was freed (the state the audio thread may be using is still alive), then renders
  another block and checks that every block the effect allocated is released again (no leak).
* `failedContextAllocationLeavesEffectBypassed` — the ported new-p14-246 case.
* `failedStateAllocationLeavesEffectBypassed` — the ported new-p14-247 case.

## Regression evidence

Every RED and every final GREEN ran the same command in the same tree; the only difference is
whether the product patch was applied. The affected-family run executed **6** test binaries.

| Check | Cwd | Exact command | Exit | Result |
|---|---|---|---:|---|
| RED, unfixed tree (all three cases) | `build/tests` | `ctest -R '^ReverbSCPluginStateTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1` | 8 | 2 of the 5 QTest slots failed: `failedContextAllocationLeavesEffectBypassed` (the child died with `CrashExit` on the null context) and `sampleRateChangeKeepsLiveStateAlive` (the child exited 1 on the free count). `failedStateAllocationLeavesEffectBypassed` already passed, because the newest line's `revsc.c` (cluster `new-p12-203`) already refuses a null state. |
| Reverse the product patch (step 4b) | worktree | `git diff -- plugins/ReverbSC/ReverbSC.cpp plugins/ReverbSC/ReverbSC.h > $LOGS/fix.patch && git apply -R $LOGS/fix.patch` | 0 | Product hunks reversed; the regression test stayed. `fix.patch` is 251 lines. |
| RED rebuild (step 4b) | worktree | `cmake --build build --target ReverbSCPluginStateTest reverbsc > $LOGS/build-red-2.log 2>&1` | 0 | Pre-fix product and regression compiled. |
| RED CTest (step 4b) | `build/tests` | `ctest -R '^ReverbSCPluginStateTest$' --output-on-failure > $LOGS/test-red-2.log 2>&1` | **8** | `sampleRateChangeKeepsLiveStateAlive` FAILED (child exit code 1 — the sample-rate change freed soundpipe blocks while the audio thread could still be using them) and `failedContextAllocationLeavesEffectBypassed` FAILED (child `CrashExit`). `Totals: 3 passed, 2 failed`. |
| Restore the product patch (step 4b) | worktree | `git apply $LOGS/fix.patch` | 0 | Product restored (137 insertions, 43 deletions in the two product files). |
| GREEN rebuild (step 4b) | worktree | `cmake --build build --target ReverbSCPluginStateTest reverbsc zene > $LOGS/build-green-2.log 2>&1` | 0 | Fixed product, plugin module and application compiled. |
| GREEN CTest (step 4b) | `build/tests` | `ctest -R '^ReverbSCPluginStateTest$' --output-on-failure > $LOGS/test-green-2.log 2>&1` | **0** | **1/1 passed** (5 QTest slots, all passed). |
| Affected-family run | `build/tests` | `ctest -R 'Reverb' --output-on-failure > $LOGS/test-family-2.log 2>&1` | **0** | **6 tests executed, 6 passed**: `ReverbSCCreateTest`, `ReverbSCBaseTest`, `ReverbScAllocationTest`, `ReverbSCInitTest`, `ReverbSCPluginStateTest`, `ReverbSCRawProcessingTest`. |
| Behaviour preservation (the reference module) | `build/tests` | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > $LOGS/test-migration-1.log 2>&1` | 8 | The `reverbsc` case compares the migrated plugin's render against the pre-migration reference module **sample-exactly** and it passed: `sha256 migrated == sha256 reference == b66277183befd45777af7887a2ff643946182110f20bf9adc632ad4dcf0048f0`, `max|delta|=0`. The one failing case in that binary, `nonRealtimeLadspaEffectsAreInvalid()` ("SWH Decimator test fixture was not loaded"), is **pre-existing and unrelated**: it fails identically with the product patch reverted (`$LOGS/test-migration-red.log`, exit 8, same single case). |

Earlier logs of the same session: `$LOGS/build-test-2.log` (the regression built against the
unfixed product), `$LOGS/build-test-3.log`, `$LOGS/test-green-1.log` (the first GREEN), 
`$LOGS/build-plugin-1.log` (`reverbsc` module), `$LOGS/build-zene-1.log` (`zene` application),
`$LOGS/build-family-1.log` and `$LOGS/test-family-1.log` (the first family run: 2 passed, 4
"Not Run" because those four binaries had never been built in this lane — after
`cmake --build build --target ReverbSCCreateTest ReverbSCBaseTest ReverbSCInitTest
ReverbSCRawProcessingTest`, exit 0, all six run and pass), and `$LOGS/build-reconf-1.log` (the
reconfigure that registered the new target).

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-401-port.r1/logs/`.

## Gates

| Exact command | Exit | Result |
|---|---:|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-401-port` (`$LOGS/gate-feature-1.log`) | **0** | **PASS**: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). |

## Files committed

| File | Change |
|---|---|
| `plugins/ReverbSC/ReverbSC.cpp` | the ported fix: the per-sample-rate DSP state, the audio-boundary swap in `processImpl()`, the preparation and publication in `changeSampleRate()`, and the checked context/state creation. |
| `plugins/ReverbSC/ReverbSC.h` | the `DspState` struct and the atomic hand-over members; the `QMutex` member is gone. |
| `tests/src/plugins/ReverbSCPluginStateTest.cpp` | the regression (new, fork-NEW). |
| `tests/CMakeLists.txt` | registers the `ReverbSCPluginStateTest` ctest. |
| `tests/fork-sources.txt` | registers the new fork-NEW test source (both recipe copies and the list). |
| `tests/upstream-modifications.txt` | declares the divergence of the changed inherited files with a reason. |
| `docs/qwen-fixes-2026-10/cluster-new-p9-401-port.md` | this report. |

`plugins/ReverbSC/revsc.c` and `plugins/ReverbSC/base.c` are **not** touched: the newest line
already carries the equivalent soundpipe-layer contract (cluster `new-p12-203`), which is why the
merge conflict named in the port job does not need a port.

## Self-check

* **Does the regression test FAIL with the product change reverted and PASS with it?** Yes, and the
  exact commands and exit codes are in the table above: with the product patch reverted
  (`git apply -R $LOGS/fix.patch`, exit 0) the rebuilt test run exits **8** with
  `sampleRateChangeKeepsLiveStateAlive` failing (the child exits 1 on the free count) and
  `failedContextAllocationLeavesEffectBypassed` failing (the child dies on the null context);
  after `git apply $LOGS/fix.patch` (exit 0) the same command exits **0** with 1/1 passed.
  The unfixed tree failed the same way from the start (`$LOGS/test-red-1.log`, exit 8).
* **Is the test deterministic? (no wall-clock timing, sleeps or races; name the seam)** The seam is
  the allocator shim: `malloc`/`calloc`/`free` are redirected in the soundpipe C files of the test
  build (`tests/CMakeLists.txt`, `target_compile_definitions(ReverbSCPluginStateTest PRIVATE
  $<$<COMPILE_LANGUAGE:C>:malloc=sp_test_malloc> ...)`), so the test (a) injects a failure into one
  allocation of a known size and (b) counts the frees. The other seam is the child-process harness
  (`--child <case>`), which turns a crash into a failed case. There is no timer, no sleep, no
  thread and no timing assertion anywhere in the test; the same command produced the same result
  in every run.
* **Is the fix at the cause and complete? (every caller and sibling checked and consistent)**
  * The changed member set (`sp`, `revsc`, `dcblk`, `mutex` → `DspState` + atomics) is private to
    `ReverbSCEffect`; `ReverbSCControls.cpp` is the only friend and touches none of it
    (`grep -n "sp\b\|revsc\|dcblk\|mutex" plugins/ReverbSC/ReverbSCControls.cpp` finds nothing).
  * `changeSampleRate()` has exactly one caller chain —
    `plugins/ReverbSC/ReverbSCControls.cpp:43` (the `sampleRateChanged()` signal) →
    `plugins/ReverbSC/ReverbSCControls.cpp:66-69` → `ReverbSCEffect::changeSampleRate()`. It is
    still called from the same place after the port.
  * The construction path and the sample-rate path now share **one** state factory
    (`createState()`), so neither can skip an allocation check — the sibling fixes (new-p14-246,
    new-p14-247) are complete because the same code makes both paths.
  * Every soundpipe entry point the effect uses is checked: `sp_create()`,
    `sp_revsc_create()`, `sp_revsc_init()`, `sp_dcblock_create_and_init_pair()` are each called
    only from `createState()`/`destroyState()` in this file.
  * `destroyState()` releases the context only when one exists and the reverb only when one
    exists, so the destructor cannot double-free or free a null pointer.
* **No collateral damage: do old projects and saved data still load and render exactly as before,
  and does any limit or check I added hit other callers that legitimately exceed it?** The change
  touches no saved setting, no control, no id and no file format: the same four knobs
  (input gain, size, colour, output gain) are read in `processImpl()` and the same wet/dry mix is
  written, so an old project renders exactly as before. This is measured, not asserted: the
  `PluginPortsMigrationTest` `reverbsc` case renders the migrated plugin and the pre-migration
  reference module and compares them **sample-exactly** — identical `sha256` and `max|delta|=0`
  (see the evidence table). No limit, threshold or quota was added, so no legitimate caller can
  trip it. The only behavioural change inside the vendored soundpipe layer is in the *failure* path
  and it was already on this line (cluster `new-p12-203`), untouched by this port.
* **Realtime, undo and commands.** `processImpl()` performs two atomic exchanges, one atomic
  fetch-add and one atomic store: it allocates nothing, frees nothing, locks nothing, waits for
  nothing and logs nothing (the `QMutex` is gone, so no lock can be taken on the audio path at
  all). The state factory, the publication and every release run on the main thread
  (`changeSampleRate()`, the destructor). No control command was added and no undo-relevant
  behaviour changed — the effect is not undoable state and the sample-rate path was already not an
  undo step.
* **Is the verdict ALREADY-FIXED or FALSE-POSITIVE?** No: the verdict is FIXED. The finding is
  reproduced on the unfixed tree of this branch (`$LOGS/test-red-1.log`, exit 8): the pre-fix
  `plugins/ReverbSC/ReverbSC.cpp:133-156` freed the reverb state and both DC blockers under
  `mutex` while `processImpl()` (lines 83-131) never took that mutex, and `plugins/ReverbSC/ReverbSC.h:63`
  held a `QMutex` that guarded nothing. The ported code is at
  `plugins/ReverbSC/ReverbSC.cpp:201-227` (preparation and publication) and
  `plugins/ReverbSC/ReverbSC.cpp:136-149` (the audio-boundary swap).
* **Portability.** The fix uses `<atomic>` and C++20 only: no Qt-6-only API, no `windows.h`
  macro, no `std::filesystem`. The test uses `QProcess` and `QtTest`, which the neighbouring
  plugin tests already use.
