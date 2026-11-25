FIXED

# cluster new-p14-363 - latency-change callback updated the effect chain from the plug-in thread

Verdict: FIXED. The VST3 effect installed a `kLatencyChanged` callback that called `cacheLatency()` directly. `cacheLatency()` reads the plug-in and then calls `EffectChain::refreshLatency()`, which walks the chain and may log (`qWarning` locks and may allocate), so a plug-in reporting its latency from its processing thread - the audio thread is one of them - did non-real-time work there. The callback now raises an atomic flag, and the control thread's poll re-reads the latency and lets the chain re-align, exactly as `Vst3Instrument` does for the same report (MX-157).

## Evidence

| What | Where |
| --- | --- |
| The defect, before this fix | `plugins/Vst3Effect/Vst3Effect.cpp:121` on `HEAD`: `m_plugin.setLatencyChangedCallback([this] { cacheLatency(); });` |
| The chain walk the callback performed | `src/core/EffectChain.cpp:115` `EffectChain::refreshLatency()`; its diagnostic at `src/core/EffectChain.cpp:138` is a `qWarning`, and the comment at `src/core/EffectChain.cpp:134` says the audio thread may not call it |
| The chain sums the effect's cached latency | `include/Effect.h:310` `compensatedLatencyFrames()` -> `latencyFrames()` |
| The listener the report arrives through | `plugins/Vst3Effect/Vst3ComponentHandler.h:106` `restartComponent()` calls the host callback with no thread restriction |
| The sibling that already defers it | `plugins/Vst3Instrument/Vst3Instrument.cpp:133` (callback stores a flag) and `:528` (poll re-reads) |
| The fix | `plugins/Vst3Effect/Vst3Effect.cpp:124` (callback stores `m_latencyChanged`), `:139` `refreshLatencyIfChanged()`, `plugins/Vst3Effect/Vst3Effect.h:74` and `:117` (flag), `plugins/Vst3Effect/Vst3EffectControls.cpp:83` (the control thread applies it) |
| Lifetime, matching the sibling | `plugins/Vst3Effect/Vst3Effect.cpp:136` - the destructor clears the callback, as `Vst3Instrument.cpp:188` does |

`kLatencyChanged` is delivered on whatever thread the plug-in calls `IComponentHandler::restartComponent` from; the handler forwarded it to the host callback inline. The callback now does one relaxed atomic store, which allocates nothing, takes no lock, waits for nothing and logs nothing.

## Test matrix

| Path | Covered by |
| --- | --- |
| Normal path: a report arrives on a non-GUI thread, the control thread applies it | `Vst3EffectIntegrationTest::testLatencyChangeFromThePlugInThreadIsDeferredToTheControlThread` - the report is raised on a `std::thread`, then applied by `Vst3EffectControls::poll()` invoked directly (Qt direct connection), asserting both `Vst3Effect::latencyFrames()` and `EffectChain::latencyFrames()` follow |
| Failure path: no plug-in / no handler | read: `HostedPlugin::raiseLatencyChangedForTest()` is a no-op without a handler, `m_latencyChanged` stays false and `refreshLatencyIfChanged()` returns at its `exchange` - the poll never re-reads a plug-in that has none; the effect is also constructed with a failed load in the existing `testParameterFallbackWithoutStateChunks()` |
| Reconfiguration path | `reprepare()` already calls `cacheLatency()` on the control thread (`Vst3Effect.cpp:181`) and still does; a report arriving during a re-prepare leaves the flag set, so the next poll re-reads once more (idempotent, same thread) |
| Exhaustion | not applicable: one edge-triggered flag, no retry budget and no queue to fill; repeated reports just re-set it |
| Audio-thread constraints | the callback body is `m_latencyChanged.store(true, std::memory_order_relaxed)`; the seam used by the test is compiled out of the product module (`LMMS_TESTING`), and the one unconditional member is an `int32_t` compared against -1 |
| Supported platforms | nothing platform-specific: `std::atomic<bool>`, no new API surface, no path handling. The test seam is header + definition under `LMMS_TESTING`, which the Windows and macOS jobs compile the same way |

## Test seams added (inert in the product)

The fixture is the Steinberg SDK's `again_sampleaccurate` sample, which reports no latency of its own, so the test could not otherwise drive a latency change. Two test-only members were added under `#ifdef LMMS_TESTING` in the host: `setReportedLatencyForTest()` (what `latency()` reports) and `raiseLatencyChangedForTest()` (calls the component handler's `restartComponent(kLatencyChanged)`, the plug-in's own call, on the calling thread). The stored value (`HostedPlugin::Impl::testLatency`) is deliberately NOT guarded: `Vst3HostInternals.h` is built by one translation unit and read by another (the instrument module allocates the `Impl`, its tests call into it), and a member that existed only in test builds gave the two halves different layouts. That mistake was caught here: with the guarded member, `Vst3InstrumentIntegrationTest` failed three cases with `vst3Plugin()->isPrepared()` false while the same binary passed on the unmodified tree (`logs/test-5-family-vst3.log` EXIT=8 vs `logs/test-6-instrument-baseline.log` EXIT=0). With the member unconditional, the whole family passes (`logs/test-11-family-vst3-green.log` EXIT=0).

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-363.r1/logs`. Every command was run from the worktree root except `ctest`, which was run from `build/tests`.

| Step | Command | Exit |
| --- | --- | --- |
| Baseline build | `cmake --build build --target Vst3EffectIntegrationTest -j4 > .../build-1.log 2>&1` | 0 |
| Baseline test (before any edit) | `ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > .../test-1-baseline.log 2>&1` | 0 |
| RED build (test + seams, fix not yet written) | `cmake --build build --target Vst3EffectIntegrationTest -j4 > .../build-2-red.log 2>&1` | 0 |
| RED run | `ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > .../test-2-red.log 2>&1` | 8 - `FAIL! ... 'effect->latencyFrames() == 0' returned FALSE. (the callback read the plug-in's latency on its own thread: 320)` |
| GREEN build | `cmake --build build --target Vst3EffectIntegrationTest zene -j4 > .../build-3-green.log 2>&1` | 0 |
| GREEN run | `ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > .../test-3-green.log 2>&1` | 0 |
| Family build (every target that includes the changed headers) | `cmake --build build -j4 --target Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EditorTest Vst3InstrumentTest Vst3InstrumentIntegrationTest Vst3InstrumentFixtureProbe vst3instrument vst3effect zene > .../build-4-family.log 2>&1`, later the same with `Vst3EffectIntegrationTest` as well (`build-6-family.log`, `build-10-green-proof.log`) | 0 |
| Family run with the first (guarded) seam | `ctest -R Vst3 --output-on-failure > .../test-5-family-vst3.log 2>&1` | 8 - `Vst3InstrumentIntegrationTest` 3 failures (caused by the guarded `Impl` member, see above) |
| Instrument test on the unmodified product files | `cmake --build build -j4 --target Vst3InstrumentIntegrationTest > .../build-5-instrument-baseline.log 2>&1` and `ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > .../test-6-instrument-baseline.log 2>&1` | 0 and 0 - it passes without my change, so the failure above was mine |
| Family run with the final patch | `ctest -R Vst3 --output-on-failure > .../test-11-family-vst3-green.log 2>&1` | 0 - 8/8 passed (Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3EditorTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest) |
| 4b - extract the fix | `git diff -- plugins/Vst3Effect/Vst3Effect.h plugins/Vst3Effect/Vst3Effect.cpp plugins/Vst3Effect/Vst3EffectControls.cpp plugins/Vst3Effect/Vst3ComponentHandler.h > .../fix.patch` | 0 |
| 4b - revert the fix | `git apply -R .../fix.patch` | 0 |
| 4b - rebuild | `cmake --build build -j4 --target Vst3EffectIntegrationTest > .../build-9-red-proof.log 2>&1` | 0 |
| 4b - run (must fail) | `ctest -R '^Vst3EffectIntegrationTest$' --output-on-failure > .../test-10-red-proof.log 2>&1` | 8 - `FAIL! ... (the callback read the plug-in's latency on its own thread: 320)` |
| 4b - restore the fix | `git apply .../fix.patch` | 0 |
| 4b - rebuild | `cmake --build build -j4 --target Vst3EffectIntegrationTest Vst3InstrumentIntegrationTest Vst3HostTest Vst3InstrumentTest Vst3EditorTest Vst3BusMapTest Vst3ChunkProbeTest Vst3InstrumentFixtureProbe vst3instrument vst3effect zene > .../build-10-green-proof.log 2>&1` | 0 |
| 4b - run (must pass) | `ctest -R Vst3 --output-on-failure > .../test-11-family-vst3-green.log 2>&1` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gates-2.log 2>&1` | 0 - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-363 > .../submit-1.log 2>&1` | 0 |

`fix.patch` is the four product files that make the fix. The two host test seams (`Vst3Host.h`, `Vst3HostInternals.h`, `Vst3HostReports.cpp`) are not in it: the test calls them, so reversing them would fail the build instead of the assertion, and they are compiled out of the product module.

## Files changed

- `plugins/Vst3Effect/Vst3Effect.h`, `plugins/Vst3Effect/Vst3Effect.cpp` - the flag, `refreshLatencyIfChanged()`, the callback and destructor.
- `plugins/Vst3Effect/Vst3EffectControls.cpp` - the control thread applies a pending report next to the re-prepare it already services.
- `plugins/Vst3Effect/Vst3ComponentHandler.h` - comment corrected: the report may come from any thread, so the callback may only raise a flag.
- `plugins/Vst3Effect/Vst3Host.h`, `plugins/Vst3Effect/Vst3HostInternals.h`, `plugins/Vst3Effect/Vst3HostReports.cpp` - the two `LMMS_TESTING` seams and the unconditional `testLatency` member.
- `tests/src/plugins/Vst3EffectIntegrationTest.cpp` - the regression test.

All of these are already registered in `tests/fork-sources.txt`; no file was added to the tree, so neither registration list changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** `git apply -R fix.patch` (EXIT 0), rebuild (EXIT 0), `ctest -R '^Vst3EffectIntegrationTest$'` -> EXIT 8, `FAIL! : testLatencyChangeFromThePlugInThreadIsDeferredToTheControlThread() 'effect->latencyFrames() == 0' returned FALSE. (the callback read the plug-in's latency on its own thread: 320)` (`logs/test-10-red-proof.log`). `git apply fix.patch` (EXIT 0), rebuild (EXIT 0), same test -> EXIT 0 (`logs/test-11-family-vst3-green.log`, 8/8 in the family).
- **The test is deterministic - no wall clock, sleeps or races.** It injects the value the plug-in reports (`HostedPlugin::setReportedLatencyForTest(320)`) and raises the report from one `std::thread` that is joined before any assertion; the "control thread" step is a direct `QMetaObject::invokeMethod(controls, "poll", Qt::DirectConnection)` call on the test's own thread. There is no sleep, no deadline, no spin and no second writer. The pre-fix failure is an assertion, not a schedule: `cacheLatency()` ran inline in the callback.
- **Fix at the cause, callers and siblings checked.** `cacheLatency()` has exactly three callers: the constructor and `reprepare()` (both control thread, unchanged) and the removed callback call. The callback's other half - the handler that forwards the report (`plugins/Vst3Effect/Vst3ComponentHandler.h:106`) - is unchanged in behaviour and its comment now states the thread rule. The sibling `Vst3Instrument` (`:133`, `:528`) already used flag + poll, and its destructor clears the callback (`:188`); the effect's destructor now does the same, so no downgrade against it. `ClapEffect::cacheLatency()` (`plugins/ClapEffect/ClapEffect.cpp:173`) is untouched: its host's `clap_host_latency_t::changed` is an empty function (`plugins/ClapEffect/ClapHostInternals.h:475`), so no callback path exists there to fix here (listed under Other findings).
- **No collateral damage.** No saved data, `.mmp` element or parameter changed; the effect's latency and tail are still cached the same way and the chain is still re-aligned by the same `EffectChain::refreshLatency()`, just on the control thread. Nothing new is rejected on load. `zene`, `vst3effect` and `vst3instrument` rebuild clean, and the whole `Vst3` family passes. The only behaviour difference is the thread that performs the re-read and the up-to-100 ms (the controls' poll interval, the same as `Vst3Instrument::m_pollTimer`) delay before PDC sees a changed latency.
- **Realtime.** The callback body is one relaxed `std::atomic<bool>::store`. No allocation, free, lock, wait, log or Qt call was added on the audio thread; the `qWarning` remains reachable only from the control thread.
- **Sibling commands (the pre-submit check's last ASK).** This fix changes no command file at all: `git show --stat 38bfef35e8` lists only the seven VST3 host/effect files, the test and this report - no `ControlCommands*`, `ControlRegistry*` or `ControlReversibilityTable*` file, so no `app.*`, `arrangement.*` or any other command id is affected. The list the check printed comes from the whole branch's diff against the FIX-BASE merge base (438 files, other lanes' work), not from this cluster's change.

## Other findings

- `plugins/ClapEffect/ClapHostInternals.h:475`: `latencyChanged(const clap_host_t*)` is an empty function, so a CLAP effect that changes its latency never updates its cached value or its chain. That is a separate defect from this cluster and was not touched.
- `plugins/Vst3Effect/Vst3HostInternals.h` must stay layout-stable across `LMMS_TESTING` and product builds (empirically shown above by the three instrument-test failures). A future test-only member added under `#ifdef LMMS_TESTING` there will silently break `Vst3InstrumentIntegrationTest` again.
