FIXED

# cluster new-p16-320 - a CLAP effect's runtime latency report never reached the chain

Verdict: FIXED. The host's `clap_host_latency_t::changed` callback was an empty function
(`plugins/ClapEffect/ClapHostInternals.h:475` at the base), so a plug-in that announced a new latency had
its report dropped: the cached value (`ClapEffect::m_latencyFrames`) was only read at construction and in
`reprepare()`, and the effect chain kept the old delay compensation until something else re-prepared the
plug-in. The callback now records the report in a relaxed atomic, and the controls' control-thread poll
re-reads the plug-in and lets `EffectChain::refreshLatency()` re-align - the same shape the VST3 effect
uses for `kLatencyChanged`.

## Evidence

| What | Where |
| --- | --- |
| The defect at the base | `git show HEAD:plugins/ClapEffect/ClapHostInternals.h` line 475: `static void CLAP_ABI latencyChanged(const clap_host_t*) {}` - registered as the host callback in the same file at line 572: `hostLatency.changed = &Impl::latencyChanged;` |
| The only places the latency was read | `plugins/ClapEffect/ClapEffect.cpp:110` (constructor, after `prepare()`) and `:170` (`reprepare()`); both control thread, both unreachable from a `changed()` report |
| What the chain uses | `src/core/EffectChain.cpp:115` `refreshLatency()` sums `compensatedLatencyFrames()` - which is `latencyFrames()`, i.e. the cached value - and may log at `:138` (`qWarning` locks and may allocate, so the audio thread may not call it) |
| CLAP's contract for the report | `plugins/CarlaBase/carla/source/includes/clap/ext/latency.h:22-27`: `changed()` "Tell the host that the latency changed. The latency is only allowed to change if the plugin is deactivated. If the plugin is activated, call host->request_restart()". Both halves were broken here: a deactivated plug-in's legal report was dropped outright, and an active plug-in's report (a contract violation, but one VST3 already tolerates) never surfaced either - only a separate `request_restart` reached `reprepare()` |
| The sibling that already defers this | `plugins/Vst3Effect/Vst3Effect.cpp:123-124` (callback stores a flag), `:139` `refreshLatencyIfChanged()`, `plugins/Vst3Effect/Vst3EffectControls.cpp:83` (the poll applies it) |
| The fix - the recorded report | `plugins/ClapEffect/ClapHostInternals.h:225` (`std::atomic<bool> latencyChangePending{false}` next to `needsReprepare`), `:484-487` `latencyChanged()` stores it |
| The fix - the host accessors | `plugins/ClapEffect/ClapHost.h:163-164` (declarations), `plugins/ClapEffect/ClapHost.cpp:173-181` (definitions), `:335` (`unload()` clears it with every other pending request) |
| The fix - the control-thread read | `plugins/ClapEffect/ClapEffect.cpp:182-189` `refreshLatencyIfChanged()` (clear, then `cacheLatency()`), declared publicly at `plugins/ClapEffect/ClapEffect.h:80`, called from `plugins/ClapEffect/ClapEffectControls.cpp:71` in `poll()` - the controls' 100 ms `QTimer` slot on the GUI thread |
| The regression test | `tests/src/plugins/ClapEffectIntegrationTest.cpp:95` (slot), `:738` `testALatencyChangeThePluginReportsReachesTheChain()` |
| The fixture's report | `tests/data/clap-test-plugin/clap-test-gain.c:100` and `:652` (`latency_reported`), `:786-792` - under `CLAP_TEST_GAIN_LATENCY_REPORT` the plug-in calls `host->get_extension(host, CLAP_EXT_LATENCY)->changed(host)` once, from `process()` |

The report is the plug-in's own CLAP call, made on the thread that is processing audio, and the host's
answer is one `std::atomic<bool>::store` with `std::memory_order_relaxed`: no allocation, no lock, no wait,
no log, no Qt call. `EffectChain::refreshLatency()` (and its possible `qWarning`) stays on the control
thread, in the poll.

## Test matrix

| Path | Covered by |
| --- | --- |
| Normal path: a report made while the effect is prepared reaches the cached value and the chain | `testALatencyChangeThePluginReportsReachesTheChain()`: constructed with 0 frames, the plug-in's `CLAP_TEST_GAIN_LATENCY` becomes 320 and its next `process()` calls `changed()`; after `ClapEffectControls::poll()` the effect reports 320 and `EffectChain::latencyFrames()` is 320 |
| Failure path: no plug-in / nothing reported | `refreshLatencyIfChanged()` returns at the flag check when nothing was reported, and `ClapEffectControls::poll()` returns early for an unloaded plug-in (`ClapEffectControls.cpp:59`); the existing `testScanFailuresAreReported()` and `testParameterFallbackWithoutStateChunks()` construct effects whose load failed and pass |
| Reconfiguration path | `reprepare()` still calls `cacheLatency()` (`ClapEffect.cpp:170`) and still runs before the new step in the poll, so a restart and a report in the same poll only add one idempotent re-read; a report arriving during the re-read stays pending for the next poll (the flag is cleared before the read) |
| Exhaustion | not applicable: one edge-triggered flag, no queue and no retry budget; repeated reports just re-set it, and `unload()` clears it |
| Audio-thread constraints | the callback body is one relaxed atomic store (it is called from the plug-in's `process()` in the test, which is the audio thread in the product); the re-read, the chain walk and the `qWarning` stay in the GUI poll. Nothing in `process()`/`processImpl()` changed |
| Supported platforms | nothing platform-specific: `std::atomic<bool>`, an existing Qt timer slot, and a C fixture already built on every CLAP-enabled target. No path handling, no new API surface |

## Test seams added (inert in the product)

The fixture (`tests/data/clap-test-plugin/clap-test-gain.c`) gained one `bool latency_reported` field and
the `CLAP_TEST_GAIN_LATENCY_REPORT` branch in `gain_process()`, which calls the real
`clap_host_latency_t::changed` through the host's own `get_extension`. Nothing in `plugins/` is
`#ifdef`-guarded and no test-only member was added to a product class: the product change is the shipped
behaviour, so the test needs no seam to see it.

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-320.r1/logs`. Builds ran from the
worktree root, `ctest` from `build/tests`.

| Step | Command | Exit |
| --- | --- | --- |
| Baseline build (before any edit) | `cmake --build build --target ClapEffectIntegrationTest -j4 > .../build-1-baseline.log 2>&1` | 0 |
| Baseline run | `ctest -R '^ClapEffectIntegrationTest$' --output-on-failure > .../test-1-baseline.log 2>&1` | 0 - but the case SKIPPED: the fixture had never been built by that target (`Totals: 0 passed, 0 failed, 1 skipped`, `logs/test-2b-direct.log`). The real baseline is the RED run below |
| Fixture + test build (test added, product unchanged) | `cmake --build build --target clap-test-gain ClapEffectIntegrationTest -j4 > .../build-3-red-fixture.log 2>&1` | 0 |
| RED run | `ctest -R '^ClapEffectIntegrationTest$' --output-on-failure > .../test-3-red.log 2>&1` | 8 - `Totals: 20 passed, 1 failed, 0 skipped`; `after the plug-in's clap_host_latency.changed(): effect 0 frames, chain 0 frames`, `after the control-thread poll: effect 0 frames, chain 0 frames`, `FAIL! ... testALatencyChangeThePluginReportsReachesTheChain() Compared values are not the same` |
| GREEN build (every target that includes the changed headers, plus the modules and `zene`) | `cmake --build build -j4 --target clap-test-gain ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest ClapInstrumentViewTest clapeffect clapinstrument zene > .../build-5-green.log 2>&1` | 0 |
| GREEN run | `ctest -R '^ClapEffectIntegrationTest$' --output-on-failure > .../test-4-green.log 2>&1` | 0 |
| GREEN, direct (proves the case is not skipped) | `./ClapEffectIntegrationTest -v1 > .../test-5-green-direct.log 2>&1` | 0 - `Totals: 21 passed, 0 failed, 0 skipped`; `after the control-thread poll: effect 320 frames, chain 320 frames` |
| Family build (the four tests that compile the ClapEffect sources) | `cmake --build build -j4 --target ClapBusMapTest ClapNoteQueueTest ClapTestInstrumentUnitTest > .../build-6-family-missing.log 2>&1` | 0 |
| Family run | `ctest -R Clap --output-on-failure > .../test-9-green-proof-family.log 2>&1` | 0 - 9/9 (`ClapBusMapTest`, `ClapNoteQueueTest`, `ClapHostTest`, `ClapTestInstrumentStateTest`, `ClapTestInstrumentEventCountTest`, `ClapTestInstrumentActivationCountersTest`, `ClapLoaderErrorTest`, `ClapEffectIntegrationTest`, `ClapInstrumentViewTest`) |
| 4b - extract the product change | `git diff -- plugins/ClapEffect/ClapEffect.cpp plugins/ClapEffect/ClapEffect.h plugins/ClapEffect/ClapEffectControls.cpp plugins/ClapEffect/ClapHost.cpp plugins/ClapEffect/ClapHost.h plugins/ClapEffect/ClapHostInternals.h > .../fix.patch` | 0 |
| 4b - revert it | `git apply -R .../fix.patch` | 0 |
| 4b - rebuild | `cmake --build build -j4 --target clap-test-gain ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest ClapInstrumentViewTest clapeffect clapinstrument zene > .../build-7-red-proof.log 2>&1` | 0 |
| 4b - run (must FAIL) | `ctest -R '^ClapEffectIntegrationTest$' --output-on-failure > .../test-8-red-proof.log 2>&1` | 8 - same assertion as the RED run, `Totals: 20 passed, 1 failed` |
| 4b - restore the fix | `git apply .../fix.patch` | 0 |
| 4b - rebuild | `cmake --build build -j4 --target clap-test-gain ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest ClapInstrumentViewTest clapeffect clapinstrument zene > .../build-8-green-proof.log 2>&1` | 0 |
| 4b - run (must PASS) | `ctest -R Clap --output-on-failure > .../test-9-green-proof-family.log 2>&1` | 0 - 9/9 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gates-1.log 2>&1` | 0 - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files") all PASS, `RESULT: PASS` |
| Gates on the final tree (this report included) | the same command `> .../gates-2.log 2>&1` | 0 - the same eight PASS rows, `9 changed files`, `RESULT: PASS` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-320 > .../submit-3-final.log 2>&1` | 0 - every item PASS, `RESULT: PASS - submit` |

`fix.patch` holds the six product files only. The fixture and the test file are not in it: the test calls
no new product API, so it compiles both ways - which is what makes the 4b run above an assertion failure
rather than a build failure.

## Files changed

- `plugins/ClapEffect/ClapHostInternals.h` - the `latencyChangePending` flag and the reporting callback.
- `plugins/ClapEffect/ClapHost.h`, `plugins/ClapEffect/ClapHost.cpp` - the two accessors and the clear in `unload()`.
- `plugins/ClapEffect/ClapEffect.h`, `plugins/ClapEffect/ClapEffect.cpp` - `refreshLatencyIfChanged()`.
- `plugins/ClapEffect/ClapEffectControls.cpp` - the control-thread poll applies a pending report.
- `tests/data/clap-test-plugin/clap-test-gain.c` - the fixture's `changed()` report under `CLAP_TEST_GAIN_LATENCY_REPORT`.
- `tests/src/plugins/ClapEffectIntegrationTest.cpp` - the regression test.

All are already registered in `tests/all-sources.txt`; no file was added to the tree, so neither
registration list changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  `git diff ... > fix.patch` (EXIT 0), `git apply -R fix.patch` (EXIT 0), rebuild (EXIT 0),
  `ctest -R '^ClapEffectIntegrationTest$'` -> EXIT 8 with
  `FAIL! : testALatencyChangeThePluginReportsReachesTheChain() Compared values are not the same` and
  `after the control-thread poll: effect 0 frames, chain 0 frames` (`logs/test-8-red-proof.log`);
  `git apply fix.patch` (EXIT 0), rebuild (EXIT 0), `ctest -R Clap` -> EXIT 0, 9/9
  (`logs/test-9-green-proof-family.log`), and the direct run reports
  `after the control-thread poll: effect 320 frames, chain 320 frames`, `Totals: 21 passed, 0 failed,
  0 skipped` (`logs/test-5-green-direct.log`).
- **The test is deterministic - no wall clock, sleeps or races.** The latency the plug-in reports is
  injected through the fixture's own environment variable (`CLAP_TEST_GAIN_LATENCY`), which its
  `gain_latency_get()` reads on every call; the report is one explicit `changed()` call made by the
  fixture from the `process()` the test itself drives (`chain.processAudioBuffer(bus)`); the "control
  thread" step is a direct `QMetaObject::invokeMethod(controls, "poll")` on the test's own thread. There
  is no sleep, no deadline, no spin, no second writer, and no thread is started. The pre-fix failure is
  an assertion (0 != 320), not a schedule.
- **The fix is at the cause and complete: every caller and every sibling checked.** `cacheLatency()` has
  exactly three callers - the constructor (`ClapEffect.cpp:110`), `reprepare()` (`:170`), and the new
  `refreshLatencyIfChanged()` (`:188`); the first two are unchanged. `refreshLatencyIfChanged()` has one
  caller, `ClapEffectControls::poll()` (`ClapEffectControls.cpp:71`), next to the re-prepare the poll
  already serviced and before the parameter sync, so a report and a restart in one poll are both taken.
  `HostedPlugin::latencyChangePending()/clearLatencyChangePending()` are the only way in or out of the
  flag, and `unload()` clears it (`ClapHost.cpp:335`) beside `needsReprepare` so a request from the old
  plug-in cannot survive into the next one. The same callback struct is handed to the CLAP instrument
  module's host (its CMakeLists compiles the same host translation units); the flag is additive there and
  `ClapInstrument` does not read it - see Other findings. The VST3 sibling keeps its own, already-fixed
  shape and is untouched.
- **No collateral damage.** No `.mmp` element, parameter, or saved value changed: the latency is still
  cached the same way and published through the same `latencyFrames()` override, and the chain is still
  re-aligned by the same `EffectChain::refreshLatency()`. Nothing new is rejected on load - no new limit
  or check was added, and the flag has no persistence. The only behaviour difference is that a reported
  change now lands within one 100 ms poll instead of never. `zene`, `clapeffect`, `clapinstrument` and
  the four tests that compile the host sources all rebuild clean, and the whole `Clap` ctest family
  passes (9/9).
- **Verdict evidence (the "not real" item does not apply).** This is a FIXED, not an ALREADY-FIXED or a
  FALSE-POSITIVE: the base callback was empty (`git show HEAD:plugins/ClapEffect/ClapHostInternals.h`
  line 475) and the only refresh sites were the constructor and `reprepare()` (`ClapEffect.cpp:110`,
  `:170`) - there was no path from a `changed()` report to the chain before this change.
- **Realtime and undo.** The callback body is a single relaxed atomic store; no allocation, free, lock,
  wait, log or Qt call was added anywhere on the audio thread, and `processImpl()` is untouched. No undo
  step is created or changed (latency is not an undoable value here), and no control command was added:
  `git status` for this commit lists no `ControlCommands*`, `ControlRegistry*` or
  `ControlReversibilityTable*` file, so the `ControlCommandsSnapshot` canary does not move.
- **The fixture must be built for this test to run at all.** `cmake --build --target
  ClapEffectIntegrationTest` does not build `clap-test-gain` (it is not in that target's dependency list),
  and without the module `initTestCase()` `QSKIP`s the whole case - `ctest` then reports "Passed" with
  `Totals: 0 passed, 0 failed, 1 skipped`. Every run cited above was made after
  `--target clap-test-gain`, and the GREEN evidence includes a direct `-v1` run showing
  `Totals: 21 passed ... 0 skipped`.

## Other findings

- `plugins/ClapInstrument/ClapInstrument.cpp:247-253` reads the latency only at construction and in
  `reprepare()`, and it does not consult the new `latencyChangePending()`, so a CLAP instrument that
  announces a change with `clap_host_latency.changed()` (legal while deactivated) still does not refresh
  its reported latency. Its own comment (`plugins/ClapInstrument/ClapInstrument.h:151-155`) documents the
  restart-then-reprepare design as sufficient. Out of this cluster's scope (effect-chain compensation)
  and not changed here.
- `plugins/ClapEffect/ClapHostInternals.h:28-31` still says the host has four TUs including
  `ClapHostLoad.cpp`, which does not exist in this tree (the files are `ClapHost.cpp`,
  `ClapHostGui.cpp`, `ClapHostLifecycle.cpp`, `ClapHostNotes.cpp`, `ClapHostParams.cpp`). Comment only.
