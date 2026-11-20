FIXED

# Cluster 5550 — ClapInstrument leaves a failed or unprepared block unsilenced

Verdict: **REAL** (process-error output bug). Fixed in commit
`80368e6bbf fix(clap-instrument): cluster 5550 - silence the whole output when the
plug-in errors or is unprepared`.

## Evidence (old code, at base `fa4a5f2e7`)

- `plugins/ClapInstrument/ClapInstrument.cpp:331-337` (base) — `m_plugin.process(...)`
  is called as a statement and its `bool` is discarded. The method then always
  returns `ProcessStatus::Continue` and still calls `m_outputBuses.deliver(...)` on
  the partially written / untouched `out`.
- `plugins/ClapEffect/ClapHost.h:250-253` — documented contract: a chunk returning
  `CLAP_PROCESS_ERROR` "stops the request: the remaining frames are left untouched
  and the call returns false, so the caller can fall back to dry audio for the
  whole block it asked for."
- `plugins/ClapEffect/ClapEffect.cpp:236-239` — the effect sibling already honours
  that contract: on a false return it calls `passThrough(in, out, ...)` for the
  whole block. An instrument has no dry signal, so its equivalent fallback is
  silence.
- Unprepared path, `plugins/ClapInstrument/ClapInstrument.cpp:311-318` (base) — sets
  `m_needsReprepare` and returns `Continue` without writing `out` at all, exposing
  whatever the caller's mixer buffer held.

Fix, `plugins/ClapInstrument/ClapInstrument.cpp` (HEAD):

- `:112-120` — `silenceOutput()` fills every frame of every channel with `0.0f`;
  plain `std::fill`, no allocation, lock or log.
- `:329` — unprepared path clears `out` before requesting the reprepare.
- `:344-352` — captures the `process()` result; on `false` clears the whole block
  before `m_outputBuses.deliver` delivers it, so the extra buses send silence and
  cannot replay a stale block.

## Matrix (test `ClapInstrumentViewTest`, `tests/src/plugins/ClapInstrumentViewTest.cpp`)

| case | expectation | slot |
|------|-------------|------|
| (a) normal block | a note-on, then one block; every sample == 0.25f (`kFixtureLevel`); no second render | `successfulBlockIsUnchanged` |
| (b) error on first chunk | caller prefill 0.5f erased; every sample 0.0f; `Continue` | `errorOnFirstChunkSilencesTheWholeBlock` |
| (c) error on a later chunk (`CLAP_TEST_INSTRUMENT_PROCESS_ERROR=2`, block of 4 × `framesPerPeriod` frames) | the host chunks the block by the prepared block size; chunk 1's marker write also erased; whole block 0.0f; no `prepare()` call in the test | `errorOnLaterChunkSilencesTheWholeBlock` |
| (d) unprepared (`release()`) | all 0.0f and `needsReprepare()` still true | `unpreparedPluginClearsOutputAndRequestsReprepare` |
| (e) recovery | the first block errors (`CLAP_TEST_INSTRUMENT_PROCESS_ERROR=1`), then a note-on and the next block renders 0.25f; no `reprepare()` call in the test | `blockAfterTheErrorRendersNormally` |
| (f) audio thread | `silenceOutput` is a `std::fill` over `out` only; no vector/QString/qDebug in the path | reviewed diff |
| (g) extra buses | clear happens before `deliver`, deliver still called | reviewed diff |
| (h) build | module and test build clean (C++20, Qt6/Linux) | `build-1/2/5*.log` |

The fixture (`tests/data/clap-test-plugin/clap-test-instrument.c:77-81,173,640,765`)
adds `CLAP_TEST_INSTRUMENT_PROCESS_ERROR=n`: calls before the nth write a marker
level, the nth returns `CLAP_PROCESS_ERROR` without writing. The error point is a
call count, not a clock.

## Verification commands and results

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5550.r1/logs/`.

| step | command | EXIT | log / evidence |
|------|---------|------|----------------|
| gates | `python3 .../feature_check.py <wt>` | **0** | `gates-1.log`: 8 PASS, RESULT: PASS |
| submit before report | `python3 .../submit_check.py <wt> 5550` | **1** | `submit-1.log`: RESULT FAIL, 2 items: report missing; `Self-check` section missing |
| RED before fix | `cd build/tests && ctest -R ClapInstrumentViewTest --output-on-failure` | 8 | `test-1-red.log`: 6 passed, 3 failed |
| build fix | `cmake --build build --target zene ClapInstrumentViewTest` | 0 | `build-1/2.log` |
| GREEN | `cd build/tests && ctest -R ClapInstrumentViewTest --output-on-failure` | **0** | `test-2-green.log`: 1/1 Passed |
| family | `cd build/tests && ctest -R "Clap.*" --output-on-failure` | **0** | `test-3-family.log`: 8/8 Passed |
| reverse fix | `git apply -R .../fix.patch` | 0 | `02-p4b-exits.md` row 3 |
| build reversed | `cmake --build build --target ClapInstrumentViewTest` | 0 | `build-3-reversed.log` |
| reversed RED | `cd build/tests && ctest -R ClapInstrumentViewTest --output-on-failure` | **8** | `test-4-reversed-red.log`: 6 passed, 3 failed |
| re-apply fix | `git apply .../fix.patch` | 0 | `02-p4b-exits.md` row 6 |
| build re-applied | `cmake --build build --target zene ClapInstrumentViewTest` | 0 | `build-4-reapplied.log` |
| re-applied GREEN | `cd build/tests && ctest -R ClapInstrumentViewTest --output-on-failure` | **0** | `test-5-reapplied-green.log`: 1/1 Passed |
| mutation i (unprepared-only) | apply/build/test | 0 / 0 / **8** | `test-mut-i.log`: 1 failed |
| mutation ii (error-only) | apply/build/test | 0 / 0 / **8** | `test-mut-ii.log`: 2 failed |
| mutation iii (channel 0 only) | apply/build/test | 0 / 0 / **8** | `test-mut-iii.log`: 3 failed |
| final build | `cmake --build build --target zene ClapInstrumentViewTest` | 0 | `build-5-final.log` |
| final GREEN | `cd build/tests && ctest -R ClapInstrumentViewTest --output-on-failure` | **0** | `test-6-final.log`: 1/1 Passed |
| family re-run at HEAD | `cd build/tests && ctest -R 'Clap' --output-on-failure` | **0** | `test-7-family-head.log`: 8/8 Passed |
| submit after report, before commit | `python3 .../submit_check.py <wt> 5550` | **1** | `submit-2.log`: RESULT FAIL, 1 item: working tree clean (untracked report) |
| submit after commit | `python3 .../submit_check.py <wt> 5550` | **0** | `submit-3.log`: RESULT PASS |
| submit final (this report commit) | `python3 .../submit_check.py <wt> 5550` | **0** | `submit-4.log`: RESULT PASS |

Mutation patches revert exactly one half of the fix: (i) clears only the unprepared
path, (ii) clears only the error path, (iii) clears only channel 0. Each turns the
matching slots red, which shows the test pins the whole-block clear on every channel.

Canary: no control command or command-registration file is touched by the four
changed files, and `doc-figures` (the gate that holds `ControlCommandsSnapshot`
equal to the binary) is PASS in `gates-1.log`, so the canary did not move.

TSan does not run locally in this environment, so the audio-thread claim is by
inspection only. The fix adds no shared state: it only clears the calling thread's
own `out` buffer, with no allocation, lock or log.

## Other findings

- The fixture `tests/data/clap-test-plugin/clap-test-instrument.c` is listed in
  `tests/upstream-modifications.txt` but not in `tests/fork-sources.txt`; the
  fork-sources gate still passes. Registration kept as-is (the gate does not
  require it).
- `ClapEffect`'s error fallback is dry audio while an instrument's is silence;
  that asymmetry is correct (no dry path for an instrument), not a defect.

## Self-check

- **Regression test fails with the product change reverted and passes with it.**
  Reversed: `test-4-reversed-red.log` (ctest EXIT 8, 6 passed / 3 failed:
  `errorOnFirstChunkSilencesTheWholeBlock`,
  `errorOnLaterChunkSilencesTheWholeBlock`,
  `unpreparedPluginClearsOutputAndRequestsReprepare`). Re-applied:
  `test-5-reapplied-green.log` (ctest EXIT 0, 1/1 Passed). Mutations i/ii/iii
  also fail (EXIT 8 each), pinning each half of the fix.
- **Deterministic.** No wall-clock, sleeps or races: the seam is
  `CLAP_TEST_INSTRUMENT_PROCESS_ERROR=n`, a 1-based `process()` call count since
  `activate()`; the multi-chunk case renders 4 × `framesPerPeriod` frames and the
  host splits it at the prepared block size; every buffer is pre-filled with
  `0.5f` and checked sample by sample. The fixture's call counter is reset in
  `instrument_activate()` (`tests/data/clap-test-plugin/clap-test-instrument.c:702`);
  the host is driven on one thread.
- **At the cause and complete.** `processImpl` is the only product site changed.
  The host contract (`ClapHost.h:250-253`) says the failing chunk stops the
  request and leaves later frames untouched, so the caller must own the whole
  block; the effect sibling (`ClapEffect.cpp:236-239`) already does. Both the
  error return and the unprepared early return are cleared, and the clear happens
  before the only consumer, `m_outputBuses.deliver`. The success path,
  `process()` call and delivery order are unchanged.
- **No collateral damage.** No saved data, project format or load path is
  touched; only audio samples in a block the plug-in failed to render. No new
  limit or check was added, so no other caller can be newly rejected. Successful
  and recovery blocks are asserted byte-for-byte/level-exact. Extra output buses
  receive silence on failure by design, which is the same signal the main bus
  gets.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is REAL, shown
  by the old `ClapInstrument.cpp:331` discard and the 3-failing reversed run.

Unverified: no TSan/sanitizer run (not available locally); no real DAW/GUI
playback or macOS/Windows host was exercised — the evidence is the Qt6/Linux
test binary and the fixture only.
