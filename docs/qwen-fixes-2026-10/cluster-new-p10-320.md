FIXED

# Cluster new-p10-320 — an invalid pitch made a zone voice stall or convert undefined

## Finding
`src/core/RackZoneInputs.cpp:133` (historical): the computed playback step
`note.pitch * sampleRate / outputRate` was not checked for a finite positive value. A zero
pitch never advanced `position` (the same source frame replayed for the whole note), and a
negative or non-finite step made `static_cast<std::size_t>(position)` in
`RackZoneVoice::render` undefined.

## Verdict: REAL (fixed)

The trigger is live code, not a hypothetical parameter:

- `src/core/RackZoneFeed.cpp:97` — `note.pitch = nph->frequency() / DefaultBaseFreq;` with no
  check on `frequency()`.
- `src/core/NotePlayHandle.cpp:695` — an active session tuning table that does not map the key
  sets `m_frequency = m_unpitchedFrequency = 0;`.
- `src/core/NotePlayHandle.cpp:711` — the same for the microtuner: an unmapped key is 0 Hz
  ("silence, the Microtuner's own contract for an unmapped key").

So `note.pitch` is exactly 0 for every note of an unmapped key, and the step becomes 0. A zero
step also follows from a `SampleBuffer` that reports no rate (`include/SampleBuffer.h:66` returns
whatever rate the buffer was built with; `src/core/SampleBuffer.cpp:50` stores it verbatim).

What the old code did with the step (`src/core/RackZoneInputs.cpp:43-57`):

- step 0: `position += 0` never reaches the end, so `render` wrote `data[0] * gain` into every
  output frame of every period and returned "still alive" — a stuck source frame delivered to
  the rack chain, and one pool voice held for the life of the note (32 of them starve the pool,
  which is the "stall" the finding reports).
- step < 0 after the first frame, or step NaN: `position` goes negative/NaN and
  `static_cast<std::size_t>(position)` (line 50) converts a value it cannot represent — undefined.

Fix (`src/core/RackZoneInputs.cpp:140-149`, the check at line 149): `playVoice` computes the step
first and refuses the
note when `!std::isfinite(step) || step <= 0.0`, before it claims a voice. Such a note is silence:
nothing is written to the destination, no pool voice is taken, and `RackZoneFeed::renderChain`
sees "nothing rendered" (`src/core/RackZoneFeed.cpp:130-140`) so no block is delivered for that
chain this period — instead of a DC block. `run()`'s return stays `chains != 0`
(`include/RackZoneFeed.h:100`), so the handle's own silence decision in
`src/core/AudioBusHandle.cpp:247` is unchanged.

Contract updated with the code: `include/RackZoneInputs.h:124-127` (playVoice: false also for a
note whose pitch gives no finite positive step) and `include/RackZoneInputs.h:76-78` (render's
`step` is a finite positive number; playVoice voices nothing else). No new control command, no
undo step, no allocation, lock, wait or log on the audio thread: the guard is one `std::isfinite`
and one compare before the existing work.

## Regression test
`tests/src/core/RackZoneRoutingTest.cpp` — `aNoteWithoutAPlayableStepIsNotVoiced`. Deterministic:
it injects the pitch values and a rate-0 sample into `RackZoneInputs::playVoice` directly; no
timing, sleeps, threads or wall clock. It proves, for pitch 0, -1, NaN and +inf, and for a sample
that reports no rate:

- `playVoice` returns false (the note is not voiced),
- the destination stays exactly silent (nothing written),
- `busyVoices()` stays 0 (a refused note costs no pool voice),
- and the same note at pitch 1.0 on the same inputs then plays from the sample start with one
  voice held, so the guard refuses only a step that cannot advance.

Test matrix for this change:

| path | covered by |
| --- | --- |
| normal play (pitch > 0) | `aZoneSampleLoadsAndPlays`, `aVoiceRendersTheLastSourceFrame`, `aFractionalStepHoldsTheLastSourceFrame`, `aFractionalStepSoundsEveryFinalFrameVisit`, and the last block of the new slot |
| failure (the bug: 0, negative, NaN, +inf, rate 0) | new slot `aNoteWithoutAPlayableStepIsNotVoiced` |
| pool exhaustion | unchanged behaviour (the guard runs before `voiceFor`, so a refused note cannot consume a slot); the new slot asserts `busyVoices() == 0` after a refusal and the existing reuse slots still take voices |
| reconfiguration | `aContinuingNoteSurvivesTempoResizeReset`, `aReusedHandleRestartsTheVoice`, and `prepare()` + `endPeriod()` calls in the existing slots and the new one |
| audio thread | `zonedNotesAllocateNothing` (still 0 allocations) covers `playVoice` + `deliver` + `processAudioBuffer`; the guard adds no allocation, lock, wait or I/O |
| platforms | no Qt-6-only API, no POSIX-only path in the change; `<cmath>` / `std::isfinite` are C++20 standard and the file already uses `std::clamp`, `std::size_t` |

## Commands and EXIT codes
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-320.r1/logs`.

RED (test written, product code unchanged — build-1.log then test-red-1.log):
`cmake --build build --target RackZoneRoutingTest -j4` → **EXIT=0**
`cd build/tests && ctest -R RackZoneRoutingTest --output-on-failure` → **EXIT=8**
`FAIL!  : RackZoneRoutingTest::aNoteWithoutAPlayableStepIsNotVoiced() '!inputs.playVoice(note, sample, out.data())' returned FALSE. (pitch 0 was voiced)` — `Loc: [../../tests/src/core/RackZoneRoutingTest.cpp(338)]`.

GREEN with the fix (build-2.log, test-green-1.log):
`cmake --build build --target RackZoneRoutingTest -j4` → **EXIT=0**
`cd build/tests && ctest -R RackZoneRoutingTest --output-on-failure` → **EXIT=0** (`100% tests passed`).

Step 4b, with the final test text:
`git diff -- src/core/RackZoneInputs.cpp include/RackZoneInputs.h > .../logs/fix.patch` → **EXIT=0**
`git apply -R .../logs/fix.patch` → **EXIT=0** (`git diff --stat` then lists only the test file)
`cmake --build build --target RackZoneRoutingTest -j4` (build-9-revert.log) → **EXIT=0**
`cd build/tests && ctest -R RackZoneRoutingTest --output-on-failure` (test-red-3.log) → **EXIT=8**
`FAIL!  : RackZoneRoutingTest::aNoteWithoutAPlayableStepIsNotVoiced() ... (pitch 0 was voiced)` line 338.
`git apply .../logs/fix.patch` → **EXIT=0**
`cmake --build build --target RackZoneRoutingTest zene RackZoneFeedRunTest -j4` (build-10-reapply.log) → **EXIT=0**
`cd build/tests && ctest -R 'RackZoneFeedRunTest|RackZoneRoutingTest|RackZonesTest|RackTest$|ZeneRackTest' --output-on-failure` (test-green-4.log) → **EXIT=0**, `100% tests passed out of 5`.

Wider family first, same fix (test-family-1.log): `ctest -R Rack` → **EXIT=8** — 11 tests
`***Not Run` (binaries not built in this worktree: RackMacrosTest, RackTest, RackZonesTest,
DrumRackPadsTest, EffectRackAddTest, RackDeferredLifetimeTest, DrumRackPanelTest, RackMenuTest,
ZeneRackTest) and ControlDrumRackPads / ControlDrumRackChains failing with
`FAIL: build lists no loadable audiofileprocessor module` (plugin modules are not built in this
build tree — the failure names the missing module, not this change; the string is a plugin-scan
verdict from the app, unrelated to `playVoice`). RackZoneFeedRunTest and RackZoneRoutingTest
passed in that run. After building the other four rack binaries (`build-4.log`, `build-5.log`,
both **EXIT=0**) they all pass (test-family-3.log **EXIT=0**, 5/5).

Gates (gate-2.log): `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
→ **EXIT=0**, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures all PASS; complexity and file-length report "no new
item in your files").

## Files
- `src/core/RackZoneInputs.cpp` — the step check in `playVoice`, `#include <cmath>`.
- `include/RackZoneInputs.h` — the `playVoice` and `render` contracts (comments only).
- `tests/src/core/RackZoneRoutingTest.cpp` — `aNoteWithoutAPlayableStepIsNotVoiced`.

## Self-check
- **The regression test FAILS with the product change reverted and PASSES with it.** Yes:
  test-red-3.log **EXIT=8** (only the new slot fails, "pitch 0 was voiced"), test-green-4.log
  **EXIT=0**. The revert was `git apply -R` of `fix.patch` (product files only), rebuilt, run.
- **The test is deterministic.** It injects the pitch values (0, -1, NaN, +inf) and a `SampleBuffer`
  whose rate is 0 straight into `RackZoneInputs::playVoice`; there is no timing, sleep, thread or
  wall clock in it, and `prepare()` is given the engine's own frame count and rate.
- **The fix is at the cause and complete.** The step is computed in one place
  (`RackZoneInputs::playVoice`) and guarded there, before `voiceFor`. Its only production caller is
  `RackZoneFeed::addNote` (`src/core/RackZoneFeed.cpp:122`), which has one path into it (the zone's
  loaded sample); that caller's `false` is already handled as "nothing rendered"
  (`renderChain`, `src/core/RackZoneFeed.cpp:133-141`) and does not change `run()`'s return
  (`include/RackZoneFeed.h:100`, unchanged), so `AudioBusHandle`'s silence decision is untouched.
  `RackZoneVoice::render` keeps its single production caller and its new contract is documented in
  the header; it was not otherwise changed. `voiceFor`, `deliver`, `endPeriod` and `busyVoices` are
  unchanged.
- **No collateral damage.** Nothing on disk or in a project changes: no save/load path, no schema,
  no command id is touched, so old projects load exactly as before. No limit was added that other
  callers could exceed: any finite positive step is still voiced, including steps far below 1
  (the fractional-step slots still pass) and steps far above 1. The only behaviour that changes is
  a note whose step is 0, negative, NaN or infinite — which produced a stuck frame or undefined
  behaviour before.
- **Realtime, undo, commands.** The guard is `std::isfinite(step) || step <= 0.0` plus a return on
  `playVoice`, which runs on the audio workers: no allocation, free, lock, wait, I/O or log; the
  existing spin flag, atomics and pre-allocated blocks are untouched, and
  `zonedNotesAllocateNothing` still reports 0 allocations. No undo state is involved (no model is
  written) and no control command was added.
- **ALREADY-FIXED / FALSE-POSITIVE ask:** not applicable — the verdict is FIXED; the evidence for
  "real" is the three file:line references above.
- **Sibling commands:** this change touches no command file — the three files I edited are
  `src/core/RackZoneInputs.cpp`, `include/RackZoneInputs.h` and
  `tests/src/core/RackZoneRoutingTest.cpp` (this commit also adds this report), and none of them
  registers or calls a control command. The sibling list the pre-submit check prints
  (`app.*`, `arrangement.*`) comes from the branch-wide diff since the fix base, which contains
  the train's other commits, not from this fix; no command id, argument schema or reversibility
  class changed here.

## Other findings (not fixed here)
- `src/core/SamplePlayHandle.cpp:754-758` divides by `converterRatio * sampleRateRatio * freqRatio`
  and guards only `converterRatio > 0.0`; a sample whose frequency is 0 would make `speed`
  infinite. Different file, different finding — left alone.
- In this worktree's build tree the ControlDrumRack* tests fail with
  `FAIL: build lists no loadable audiofileprocessor module`, and most rack test binaries were not
  built; I built the five rack ones I cite and left the rest. Not caused by this change.
