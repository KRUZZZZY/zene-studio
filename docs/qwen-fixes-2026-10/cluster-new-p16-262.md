FIXED

# cluster new-p16-262 - null audio-channel pointers dereferenced during VST3 bus mapping

Verdict: **FIXED** (real defect, fixed at the cause in the bus mapping; regression test
`Vst3HostTest::testNullChannelPointersReadSilenceAndWriteScratch` fails without the fix and passes with it).

## The bug, in the code

`plugins/Vst3Effect/Vst3Host.cpp` (pre-fix line numbers, commit `76bb7be2b1^` state):

- `HostedPlugin::process` (`plugins/Vst3Effect/Vst3Host.cpp:607`) clears the caller's output
  pointers through `clearCallerOutputs`, which tolerates nulls
  (`plugins/Vst3Effect/Vst3Host.cpp:107`: `if (outputs == nullptr || frames <= 0) { return; }`
  and a per-channel `outputs[channel] != nullptr` check).
- `HostedPlugin::Impl::mapBuses` (`plugins/Vst3Effect/Vst3Host.cpp:754`) then treated every
  channel index below the caller's count as a valid pointer:
  - input side: `channel < static_cast<std::size_t>(numInputs) ? const_cast<float*>(inputs[channel]) + offset : silence.data();`
  - output side: `const bool hasCallerOutput = channel < static_cast<std::size_t>(numOutputs);`
    then `outputs[channel] + offset`.
  Neither condition looks at the array itself nor at the entry. A null channel array with a
  positive count dereferences null, and a null entry beside a valid channel produces `nullptr + offset`
  which the plug-in then writes through. This runs on the audio thread (called from `runChunk`,
  `plugins/Vst3Effect/Vst3Host.cpp:686`).

So the finding is REAL: the guard `clearCallerOutputs` applies to the same pointers was missing
from the mapping that hands them to the plug-in.

## The fix

`plugins/Vst3Effect/Vst3Host.cpp:95` and `:101`, in the file's anonymous namespace:

```cpp
auto callerChannel(const float* const* channels, int count, std::size_t index) -> const float*
{
	if (channels == nullptr || index >= static_cast<std::size_t>(count)) { return nullptr; }
	return channels[index];
}

auto callerChannel(float* const* channels, int count, std::size_t index) -> float*
{
	if (channels == nullptr || index >= static_cast<std::size_t>(count)) { return nullptr; }
	return channels[index];
}
```

`mapBuses` (`plugins/Vst3Effect/Vst3Host.cpp:754`) now asks `callerChannel` instead of comparing
the index alone (`:770` inputs, `:785` outputs). A channel the caller does not provide - no array,
an index past the count, or a null entry beside valid ones - keeps the mapping's existing
fallbacks: inputs read the pre-allocated `silence` buffer, outputs write `scratchOutput` and are
memset to zero for the chunk. Decision count is unchanged (one ternary plus one `if` on the output
side, one ternary on the input side), and nothing allocates, locks, logs or waits on the audio
thread: both helpers are two comparisons and a load.

Callers/siblings checked:

- `mapBuses` is called only from `HostedPlugin::Impl::runChunk`
  (`plugins/Vst3Effect/Vst3Host.cpp:686`), which is the single place the caller's planar arrays
  reach the plug-in. `process` itself touches the caller's outputs only through
  `clearCallerOutputs`, which already handled nulls.
- `Vst3HostReports.cpp` uses the `mapBuses` overload that takes bus descriptors
  (`plugins/Vst3Effect/Vst3BusMap.h:91`), not this one; no channel pointers there.
- There is no header change: `mapBuses`'s signature in `plugins/Vst3Effect/Vst3HostInternals.h:162`
  is untouched, so no ABI/rebuild fan-out beyond the targets that compile `Vst3Host.cpp`.
- Behaviour outside the null case is identical: a provided channel still gets
  `pointer + offset`; a channel past the count still gets silence/scratch.
- The CLAP host has the same shape (`plugins/ClapEffect/ClapHost.cpp:454`, `:469`) - see
  "Other findings"; it is outside this job's two-file checkpoint and was not changed.

## Regression test

`tests/src/plugins/Vst3HostTest.cpp:405`, slot declared at `:227` (immediately after
`testChannelPartition`). It re-prepares the shared fixture itself
(`m_plugin.prepare(TestSampleRate, TestBlockSize, &error)`), asserts `isPrepared()`, sets the gain
parameter to 1.0, and then:

1. positive control: an ordinary stereo call, both channels 0.5 in -> rms 0.5 out (a mapping that
   refused everything would fail here);
2. null input array with count 2 -> both outputs silent, no dereference;
3. one null input entry beside a valid channel -> that channel silent, the other still 0.5;
4. null output array -> the request completes into scratch, the caller's buffers stay silent;
5. one null output entry -> the provided channel still receives 0.5;
6. a request longer than the prepared block (`TestBlockSize + 128`) with a null output entry ->
   the chunked path uses the same fallbacks and the provided channel is rendered whole.

No sleeps, no wall-clock, no threads: the assertions are sample values only.

## Commands and EXIT codes (all logs under `.../new-p16-262.r7/logs`)

RED (test in place, product fix not yet written):

```
cmake --build build --target Vst3HostTest -j4 > logs/build-red-1.log 2>&1      EXIT=0
(cd build/tests && ctest -R "^Vst3HostTest$" --output-on-failure > logs/test-red-1.log 2>&1)  EXIT=8
```

`test-red-1.log`: `testChannelPartition()` PASS, then
`Received signal 11 (SIGSEGV), code SEGV_MAPERR ... accessing address 0x0000000000000000`,
frame `#8 ... Vst3HostTest::testNullChannelPointersReadSilenceAndWriteScratch()`, and
`799 - Vst3HostTest (SEGFAULT)`.

GREEN (fix in place):

```
cmake --build build --target Vst3HostTest -j4 > logs/build-green-1.log 2>&1    EXIT=0
(cd build/tests && ctest -R "^Vst3HostTest$" --output-on-failure > logs/test-green-1.log 2>&1)  EXIT=0
(cd build/tests && ./Vst3HostTest > logs/test-direct-1.log 2>&1)              EXIT=0
```

`test-direct-1.log`: `PASS : ...::testNullChannelPointersReadSilenceAndWriteScratch()`,
`Totals: 25 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms`.

Reverse-red proof (product patch removed, then restored):

```
git diff HEAD~1 -- plugins/Vst3Effect/Vst3Host.cpp > logs/fix.patch            EXIT=0
git apply -R logs/fix.patch                                                    EXIT=0
cmake --build build --target Vst3HostTest -j4 > logs/build-reversered-1.log 2>&1 EXIT=0
(cd build/tests && ctest -R "^Vst3HostTest$" --output-on-failure > logs/test-reversered-1.log 2>&1) EXIT=8
git apply logs/fix.patch                                                       EXIT=0
cmake --build build --target Vst3HostTest -j4 > logs/build-restore-1.log 2>&1  EXIT=0
(cd build/tests && ctest -R "^Vst3HostTest$" --output-on-failure > logs/test-restore-1.log 2>&1)  EXIT=0
```

`test-reversered-1.log`: `0% tests passed, 1 tests failed out of 1`, `799 - Vst3HostTest (SEGFAULT)`.
`test-restore-1.log`: `100% tests passed out of 1`.

Family and product build:

```
cmake --build build --target zene -j4 > logs/build-zene-1.log 2>&1             EXIT=0
cmake --build build --target Vst3BusMapTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3EditorTest -j4 > logs/build-vst3-family-1.log 2>&1  EXIT=0
cmake --build build --target Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest -j4 > logs/build-vst3-instruments-1.log 2>&1  EXIT=0
(cd build/tests && ctest -R "Vst3" --output-on-failure > logs/test-vst3-family-2.log 2>&1)  EXIT=0
```

`test-vst3-family-2.log`: 8/8 passed - Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest,
Vst3EffectIntegrationTest, Vst3EditorTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest,
Vst3InstrumentIntegrationTest.

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
  /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-262 \
  > logs/gates-1.log 2>&1                                                     EXIT=0
```

`gates-1.log`: `RESULT: PASS`, every row PASS (fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity
"no new item in your files", file-length "no new item in your files").

## Registration

`plugins/Vst3Effect/Vst3Host.cpp` is already listed in `tests/fork-sources.txt:1599`; no list
change is required. No new product file was added.

## Self-check

Answers to the pre-submit check's ASK items, in its order:

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
   Yes. Reverted: `git apply -R logs/fix.patch` (`EXIT=0`), rebuild (`EXIT=0`), `ctest -R
   "^Vst3HostTest$"` -> `EXIT=8`, `799 - Vst3HostTest (SEGFAULT)` (`logs/test-reversered-1.log`).
   Restored: `git apply logs/fix.patch` (`EXIT=0`), rebuild (`EXIT=0`), same ctest -> `EXIT=0`,
   `100% tests passed` (`logs/test-restore-1.log`). The first RED run before the fix was written
   also exited 8 (`logs/test-red-1.log`).
2. **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
   values it uses).** The seam is the public `HostedPlugin::process` boundary itself: the test
   injects the channel pointers - the array (`nullptr` or a two-element vector), the entries
   (`nullptr` beside a valid buffer) and the frame count (`TestFrames`, and `TestBlockSize + 128`
   for the longer request) - and asserts only sample values (`rms` of the buffers) and the exact
   zero of the silence/scratch path. No sleeps, no threads, no timers, no files, no random data:
   the fixture is re-prepared with fixed `TestSampleRate`/`TestBlockSize` and the gain parameter is
   set explicitly, so the only variable is host state the test sets itself.
3. **The fix is at the cause and complete: every caller and sibling of what you changed was
   checked and stays consistent.** Yes. `mapBuses(inputs, outputs, numInputs, numOutputs, offset,
   chunk)` has one caller, `HostedPlugin::Impl::runChunk` (`plugins/Vst3Effect/Vst3Host.cpp:686`),
   reached from `process`; both channel arrays enter the plug-in only there. The descriptor overload
   of `mapBuses` (`plugins/Vst3Effect/Vst3BusMap.h:91`, used by `Vst3HostReports.cpp:97`) carries no
   channel pointers and is unaffected. `clearCallerOutputs` keeps its own null guard, so outputs are
   zeroed before the mapping and are not zeroed twice. The header
   (`plugins/Vst3Effect/Vst3HostInternals.h:162`) is unchanged. The one sibling with the same shape
   is the CLAP host (`plugins/ClapEffect/ClapHost.cpp:454`, `:469`), reported under Other findings
   and deliberately not touched: this job's checkpoint is the two VST3 files.
4. **No collateral damage: old projects and saved data still load as before, and any limit or check
   you added does not hit other callers that legitimately exceed it.** No file format, id or saved
   state changes: the diff is the two helpers plus the two mapping expressions. This fix adds no
   limit at all - it removes a dereference, it does not reject anything, so a caller that supplies
   all its channels is mapped exactly as before (`pointer + offset`), and a caller that supplies
   fewer channels keeps the pre-existing silence/scratch fallback. The 25 slots of `Vst3HostTest`
   (including `initTestCase`, `testIdentity`, `testStateRoundTrip`, `testAudioGain`) and the seven
   other VST3 tests pass on the fixed build: `ctest -R "Vst3"` -> `EXIT=0`, 8/8 passed.
5. **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
   contract that shows it.** Not applicable - the verdict is FIXED (a real defect, reproduced by
   SIGSEGV). For contrast, the guard that already existed on the neighbouring path is
   `plugins/Vst3Effect/Vst3Host.cpp:107` (`clearCallerOutputs`), and the pre-fix mapping at `:754`
   dereferenced `inputs[channel]` / `outputs[channel]` after checking only the index.
6. **Sibling commands in the command files you changed.** No command file was changed: the commit
   touches only `plugins/Vst3Effect/Vst3Host.cpp`, `tests/src/plugins/Vst3HostTest.cpp` and this
   report. No control command, schema entry or catalogue snapshot is affected, so
   `app.*`/`arrangement.*` siblings are unchanged and the `ControlCommandsSnapshot` canary cannot
   move.

- Verdict backed by code: yes - `clearCallerOutputs` null guard
  (`plugins/Vst3Effect/Vst3Host.cpp:107`) versus the unguarded `inputs[channel]` /
  `outputs[channel]` in the pre-fix `mapBuses` (`:754`), reproduced by SIGSEGV at address 0 in the
  RED run.
- Test fails without the fix, passes with it: proven twice - first RED (no fix written yet,
  `EXIT=8`), then the reverse-red proof on the committed fix (`git apply -R` -> `EXIT=8`,
  `git apply` -> `EXIT=0`).
- Deterministic: no timing, sleeps, threads or wall clock; only sample-value assertions on
  injected buffers.
- Cause, not symptom: the null check lives in the one mapping that hands caller pointers to the
  plug-in; the existing silence/scratch fallbacks and their buffer sizes are unchanged.
- No collateral damage: a provided channel still maps to `pointer + offset`, a channel past the
  count keeps its previous fallback, project data and saved state are untouched (no format change),
  and the other seven VST3 tests plus all 25 slots of `Vst3HostTest` pass.
- Realtime: no allocation, lock, wait or log added - `callerChannel` is two comparisons and a load,
  the packet buffers it points into (`silence`, `scratchOutput`) are still allocated in `prepare`.
- No new control command, no undo or command-schema change.
- Registration/gates: `feature_check.py` RESULT: PASS, EXIT=0.

## Other findings

- `plugins/ClapEffect/ClapHost.cpp:454` and `:469` have the same unguarded shape
  (`inputs[channel + c]`, `outputs[channel + c]`): a null CLAP channel array or a null entry beside
  valid ones would be dereferenced the same way. Out of this job's declared two-file scope, so not
  changed here; worth its own finding.
