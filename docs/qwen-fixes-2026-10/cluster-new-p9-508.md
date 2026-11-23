ALREADY-FIXED

# Cluster new-p9-508 — VST3 reprepare can race audio processing

## Verdict

**ALREADY-FIXED.** On this branch, both `Vst3Effect::reprepare()` and
`Vst3Instrument::reprepare()` close the audio-process admission gate before they call
`HostedPlugin::prepare()` (which releases and clears the plug-in's processing buffers) and reopen it only
after `prepare()` and the latency cache are complete. The audio thread checks the same gate around every
`HostedPlugin` access and falls back to pass-through (effect) or silence (instrument) while a
reconfiguration is pending. This is the exact defect the finding describes, fixed on the branch before
this job by commits `51b528f7c8` and `309e3ce831` (both ancestors of `050/m0-train-35`, the fix base).

## File and line evidence

The fix mechanism is `vst3::AudioProcessGate`:

- `plugins/Vst3Effect/Vst3Host.h:87-141`: lock-free admission gate. `tryEnterAudioProcess()` refuses when
  `m_reconfiguring` is set (double check after the active-call increment); `beginReconfigure()` sets the
  flag and waits for `m_activeCalls == 0`; `endReconfigure()` reopens admission.
  A `static_assert` requires the counter to be lock-free.

Effect path (the finding's `Vst3Effect.cpp:145`):

- `plugins/Vst3Effect/Vst3Effect.cpp:154`: `reprepare()` calls `m_processGate.beginReconfigure()` before
  `m_plugin.prepare(...)` (line 161).
- `plugins/Vst3Effect/Vst3Effect.cpp:168`: `endReconfigure()` after `prepare()` and `cacheLatency()`.
- `plugins/Vst3Effect/Vst3Effect.cpp:212-226`: `processImpl()` refuses admission during reconfiguration,
  passes the dry signal through, and returns without touching `HostedPlugin` state.
- `plugins/Vst3Effect/Vst3Effect.cpp:257`: the normal exit releases admission.

Instrument path (the finding's `Vst3Instrument.cpp:268`):

- `plugins/Vst3Instrument/Vst3Instrument.cpp:286`: `reprepare()` calls `beginReconfigure()` before
  `prepare()`.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:315`: `endReconfigure()` after `cacheLatency()`.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:388-397`: `processImpl()` zero-fills and returns when
  admission is refused.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:415` and `:435`: both exits after admission release it
  (unprepared branch and normal branch); there is no other return after a successful
  `tryEnterAudioProcess()`.

Sibling path that mutates the prepared plug-in on the GUI thread:

- `plugins/Vst3Instrument/Vst3Instrument.cpp:225` / `:237`: `enableOutputBus()` also brackets
  `release()` + `prepare()` with the same gate (in addition to `requestChangeInModel()`), so the
  sibling is consistent.

The regression tests already on the branch:

- `tests/src/plugins/Vst3EffectIntegrationTest.cpp:207-278`:
  `testWrapperProcessingIsQuiescedDuringReprepare` drives the real advantage path
  (`EffectChain::processAudioBuffer` while `Vst3Effect::reprepare()` is paused by the
  `setReprepareTestHook` seam) and requires pass-through audio (dry 0.5) rather than processed audio
  (0.25). This fails without the gate (demonstrated below).
- `tests/src/plugins/Vst3HostTest.cpp:686-733`: `testReprepareGateQuiescesAllActiveAudioCalls` proves the
  gate refuses admission while pending, waits for every active call, and does not allocate on the audio
  path.

## Verification evidence

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-508.r1/logs`. Build commands ran from
the worktree root; test commands ran from `build/tests`.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Fix ancestry | `git merge-base --is-ancestor 51b528f7c8 HEAD; echo ANCESTOR_EXIT=$?` | `ANCESTOR_EXIT=0` (and `309e3ce831` likewise) |
| Build the regression tests | `cmake --build build --target Vst3EffectIntegrationTest Vst3HostTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Whole affected test family | `ctest -R 'Vst3EffectIntegrationTest\|Vst3HostTest' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=0`, 2/2 passed |
| Wrapper regression test (GREEN) | `./Vst3EffectIntegrationTest testWrapperProcessingIsQuiescedDuringReprepare > .../logs/test-2.log 2>&1; echo EXIT=$?` | `EXIT=0`, 3 passed / 0 failed |
| Gate concurrency test (GREEN) | `./Vst3HostTest testReprepareGateQuiescesAllActiveAudioCalls > .../logs/test-3.log 2>&1; echo EXIT=$?` | `EXIT=0`, 3 passed / 0 failed |
| Save the gate removal as a patch (working tree only) | `git diff -- plugins/Vst3Effect/Vst3Effect.cpp > .../logs/gate-removal.patch` | patch contains exactly the two gate calls removed from `reprepare()` |
| Rebuild without the gate | `cmake --build build --target Vst3EffectIntegrationTest -j4 > .../logs/build-2-red.log 2>&1; echo BUILD_EXIT=$?` | `BUILD_EXIT=0` |
| 4b RED (no gate) | `./Vst3EffectIntegrationTest testWrapperProcessingIsQuiescedDuringReprepare > .../logs/test-4-red.log 2>&1; echo RED_EXIT=$?` | `RED_EXIT=1` — `wrapper processed plugin audio during re-prepare: 0.25` |
| Restore the gate | `git apply -R .../logs/gate-removal.patch; echo APPLY_R_EXIT=$?` | `APPLY_R_EXIT=0`; `git status --porcelain` printed nothing |
| Rebuild with the gate | `cmake --build build --target Vst3EffectIntegrationTest -j4 > .../logs/build-3-green.log 2>&1; echo BUILD_EXIT=$?` | `BUILD_EXIT=0` |
| 4b GREEN (gate restored) | `./Vst3EffectIntegrationTest testWrapperProcessingIsQuiescedDuringReprepare > .../logs/test-5-green.log 2>&1; echo GREEN_EXIT=$?` | `GREEN_EXIT=0`, 3 passed / 0 failed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check-1.log 2>&1; echo EXIT=$?` | `EXIT=0`, RESULT: PASS (all gates PASS) |

The RED run is the proof that the existing regression test catches this exact defect: with only the two
`m_processGate` calls removed from `Vst3Effect::reprepare()`, the wrapper processed audio during the
re-prepare window (`firstOutput` 0.25 instead of the pass-through 0.5). No product code is changed by
this job; the working tree was restored to HEAD before the report commit.

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p9-508.md` (this report)

No product or test file was modified: the fix and its regression tests are already in the branch
baseline (`51b528f7c8`, `309e3ce831`, both ancestors of `050/m0-train-35`).

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  RED `RED_EXIT=1` (`test-4-red.log`, assertion `wrapper processed plugin audio during re-prepare: 0.25`)
  after removing the two gate calls from `Vst3Effect::reprepare()`; GREEN `GREEN_EXIT=0`
  (`test-5-green.log`) after restoring them. The reverse patch application returned `APPLY_R_EXIT=0`.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values
  it uses).** The seam is `Vst3Effect::setReprepareTestHook` (`Vst3Effect.h:71-77`): `reprepare()` calls
  the hook at `Vst3Effect.cpp:156`, i.e. after `beginReconfigure()` has closed admission. The test's
  audio thread waits on the hook's `entered` flag, processes once, and releases the hook; the assertion
  is on the deterministic pass-through value. The hook's 2-second `steady_clock` deadline is only a
  deadlock guard for a hung run, not the observation signal; the gate decision itself is
  flag/counter based, not timed.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and
  stays consistent.** Callers: `Vst3EffectControls::poll()` (`Vst3EffectControls.cpp:78-81`) and
  `Vst3Instrument::poll()` (`Vst3Instrument.cpp:524-527`) are the only `reprepare()` callers; both run
  on the GUI thread. Siblings that release/re-prepare the plug-in: `Vst3Instrument::enableOutputBus()`
  (`Vst3Instrument.cpp:218-238`) uses the same gate around `release()`/`prepare()`. Both
  `processImpl()` implementations balance admission on every return path. No product code was changed by
  this job, so no caller can regress.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you
  added does not hit other callers that legitimately exceed it.** No product, serialization, limit or
  check was changed; only this report is committed. The gate already ships in the baseline and the
  VST3 test family (`Vst3EffectIntegrationTest`, `Vst3HostTest`) passes on it.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** `plugins/Vst3Effect/Vst3Effect.cpp:154,168` and
  `plugins/Vst3Instrument/Vst3Instrument.cpp:286,315` bracket `prepare()` with
  `AudioProcessGate::beginReconfigure()`/`endReconfigure()`
  (`plugins/Vst3Effect/Vst3Host.h:87-141`), and `processImpl()` refuses admission at
  `Vst3Effect.cpp:212` / `Vst3Instrument.cpp:388`. Fixed by ancestors `51b528f7c8` and `309e3ce831`
  of the fix base.
- **Sibling commands in the command files you changed — confirm each is consistent with your fix (or
  say why it is not affected).** This job changed no command file and adds no command: the only
  committed file is this report. The submit check derives that sibling list from every path changed
  since the fix base (`git diff --name-only 050/m0-train-35..HEAD`, 349 files from the merge train), not
  from this job's commit `93e4df5449`; none of `app.*`, `arrangement.*` or any other control command is
  touched here, so the ControlCommandsSnapshot canary is unchanged.
