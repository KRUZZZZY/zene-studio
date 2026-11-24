FIXED

# cluster new-p15-260-port — Stutter playback state is shared across potentially concurrent notes

Verdict: **FIXED** (round 3). The finding is real. Round 2 made every access to the stutter continuation atomic but
left it as **two** atomics, so a note start still took the frame from one publication and the direction from another
when a sibling note job published in between. That is the defect the review named, and it is now one lock-free word.

## The defect, at the code

- The continuation was two separate atomics — `std::atomic<f_cnt_t> m_nextPlayStartPoint` and
  `std::atomic<bool> m_nextPlayBackwards` (`plugins/AudioFileProcessor/AudioFileProcessor.h:206-207` at round 2,
  commit `db9664a9ca`) — while the frame and the direction are one value semantically: they are published together
  (`AudioFileProcessor.cpp:263-264` at round 2) and used together (`src/core/Sample.cpp:227-247`, the ping-pong
  turnaround reads `m_backwards` and moves `m_frameIndex` as one state).
- `AudioFileProcessor::playNoteImpl()` then read the continuation twice on a note's first period: the end-of-sample
  test read `m_nextPlayStartPoint`, and the voice state was loaded from `m_nextPlayStartPoint` and
  `m_nextPlayBackwards` again (`AudioFileProcessor.cpp:181-198` at round 2). Notes of one instrument render on the
  engine's worker threads (`SamplerVoicePool.h`, "Threading"), so another note job's per-period publish can land
  between the two reads. A note could therefore start at one publication's frame while playing the other
  publication's direction — and in ping-pong (loop mode 2) that direction is a real value, not always false.
- Round 2 also shipped a test that **proved nothing**: `SamplerEnvelopeTest::stutterStartIsOneCoherentSnapshot`
  reached the helper through `#define private public` and never entered `playNoteImpl()`, so reversing only the
  production caller left it green (the review's measured result). That test and its trait are gone.

## The fix

1. **One lock-free word.** `std::atomic<std::uint64_t> m_stutterContinuation{0}`
   (`plugins/AudioFileProcessor/AudioFileProcessor.h:252`): bit 63 is the direction, bits 0-62 the frame
   (`StutterBackwardsBit` / `StutterFrameMask`, `:164-165`). One accessor pair — `publishStutterContinuation()`
   (`:181-184`) and `stutterContinuation()` (`:174-178`) — makes every publication and every read one relaxed
   load/store of the same word, so a frame can never be paired with a direction from another publication.
2. **All sites use it.** The six sites plus `beatLen()`: the magic-key reset (`AudioFileProcessor.cpp:173`), the
   note start (`:190-191`), the per-period publish (`:264`), `reverseModelChanged()` (`:531`),
   `stutterModelChanged()` (`:547`), `pointChanged()` (`:619`) and `beatLen()`'s resume frame (`:453`). The
   end-of-sample restart keeps the same pair (`stutterStartForNote()`, `AudioFileProcessor.h:188-199`).
3. **One read per note start.** `playNoteImpl()` now takes `const StutterStart start = stutterStartForNote(...)`
   once and writes `state.setFrameIndex(start.frame)` / `state.setBackwards(start.backwards)` from it
   (`AudioFileProcessor.cpp:190-201`).
4. **Lock-free is a compile-time requirement:** `static_assert(std::atomic<std::uint64_t>::is_always_lock_free)`
   and, for the playhead the same render path publishes, `std::atomic<f_cnt_t>`
   (`AudioFileProcessor.cpp:78-79`). Nothing allocates, locks, waits or logs on the render path; no command and no
   undo step was added.
5. **A deterministic seam through the production caller** (the repo's `ZynAddSubFxInstrumentTest` shape):
   `noteStartHook()` (`AudioFileProcessor.h:200-204`, defined at `AudioFileProcessor.cpp:305-313`) runs in
   `playNoteImpl()` **exactly between** the continuation read and the voice state write, and is compiled in only
   when `LMMS_TESTING` is defined — the plugin module is built without it. Test-only read accessors let the
   regression see what the note start consumed (`testStutterContinuation()`, `testPublishStutterContinuation()`,
   `testVoiceState()`, `AudioFileProcessor.h:207-214`). `instantiateView()` answers `nullptr` in a test build
   (`AudioFileProcessor.cpp:465-474`) because the test target compiles this translation unit without the plugin's
   view; the shipping build is unchanged.

## The regression

`tests/src/plugins/SamplerStutterContinuationTest.cpp`, its own target `SamplerStutterContinuationTest`, which
compiles `plugins/AudioFileProcessor/AudioFileProcessor.cpp` with
`LMMS_TESTING PLUGIN_STATIC_DEFINE PLUGIN_NAME=audiofileprocessor` (`tests/CMakeLists.txt`, `add_dependencies
audiofileprocessor`). It drives the shipping note-start path — `NotePlayHandle::play -> Instrument::playNote ->
playNoteImpl` — with an instrument built by the test and installed with `InstrumentTrack::setInstrumentForTest()`.

| case | what it drives | what it asserts |
| --- | --- | --- |
| (a) `aNoteStartReadsTheContinuationOnce` | continuation `(midFrame, forwards)`; the hook publishes `(end-1, backwards)` between the read and the write | the note renders the ramp **from `midFrame`** (`0.45`, not `0.899933`) and **rising** |
| (b) `pingPongPlaybackContinuesBackwardsAtThePublishedFrame` | loop mode 2 rendered past the loop end, then a second note; the hook publishes `(0, forwards)` | the second note continues **backwards at the published frame** (`0.888316`, falling), not forwards from `0` |
| (c) `aNoteThatReachedTheSampleEndPublishesItsOwnVoiceState` | a note that reaches the sample end | the pair it published equals its own voice state, read once |

Determinism: no thread, sleep or clock. Every value is injected (the continuation, the 0.9 ramp, a fixed note key
and period length) and the hook runs synchronously inside the note start. Direction is compared over a whole period
(256 frames ≈ 0.0026 of the ramp) because the 16-bit ramp repeats a level for two frames.

## Commands and results (logs in `.../new-p15-260-port.r3/logs`)

| what | command | EXIT | log |
| --- | --- | --- | --- |
| plugin builds with the packed word | `cmake --build build --target audiofileprocessor -j4 > $LOGS/build-3.log 2>&1` | 0 | build-3.log |
| the regression builds | `cmake --build build --target SamplerStutterContinuationTest -j4 > $LOGS/build-6.log 2>&1` | 0 | build-6.log |
| GREEN before the red run | `ctest -R '^SamplerStutterContinuationTest$' --output-on-failure > $LOGS/test-2.log 2>&1` | 0 | test-2.log |
| **RED**: `playNoteImpl()`'s body reverted to the e4194e0 shape (helper, packed word and hook kept) | `cmake --build build --target SamplerStutterContinuationTest -j4 > $LOGS/build-red-1.log 2>&1` | 0 | build-red-1.log |
| **RED** run | `ctest -R '^SamplerStutterContinuationTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1` | **8** | test-red-1.log |
| fix restored | `cp $LOGS/red-restore-AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.cpp` + `cmake --build build --target SamplerStutterContinuationTest -j4 > $LOGS/build-green-1.log 2>&1` | 0 | build-green-1.log |
| **GREEN** again | `ctest -R '^SamplerStutterContinuationTest$' --output-on-failure > $LOGS/test-green-1.log 2>&1` | **0** | test-green-1.log |
| the whole product patch reversed (procedure 4b literal) | `git diff -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h > $LOGS/fix.patch` then `git apply -R $LOGS/fix.patch` | 0 | fix.patch |
| with the whole patch reversed the test does **not build** (the seam is part of the product change), and the stale binary passes | `cmake --build build --target SamplerStutterContinuationTest -j4 > $LOGS/build-4b-revert.log 2>&1` / `ctest ... > $LOGS/test-4b-revert.log 2>&1` | 2 / 0 | build-4b-revert.log, test-4b-revert.log |
| patch restored | `git apply $LOGS/fix.patch` (`diff` against the saved green file: identical) + build + `ctest ... > $LOGS/test-4b-restore.log 2>&1` | 0 / 0 / **0** | build-4b-restore.log, test-4b-restore.log |

The RED output (test-red-1.log):

```
FAIL!  : SamplerStutterContinuationTest::aNoteStartReadsTheContinuationOnce() 'std::abs(note.left[0] - rampAt(midFrame)) < 0.05f' returned FALSE. (the note started at the ramp value 0.899933 instead of 0.45)
FAIL!  : SamplerStutterContinuationTest::pingPongPlaybackContinuesBackwardsAtThePublishedFrame() 'std::abs(second.left[0] - rampAt(paused.frame)) < 0.05f' returned FALSE. (the note started at the ramp value 0 instead of 0.888316)
Totals: 3 passed, 2 failed, 0 skipped, 0 blacklisted, 483ms
```

That is the defect in one line each: with the caller reverted, the note starts at the *hook's* publication
(`end-1` playing backwards; `0` playing forwards) instead of the pair it read. (c) still passes there, as expected:
the publish sites are unchanged, and the case asserts the publication equals the voice state.

The 4b literal reversal is recorded because the procedure names it. On its own it proves nothing here — it removes
the seam the test needs, so the test binary does not rebuild and the stale one passes — which is exactly the failure
mode the review measured on round 2. The caller-only revert above is the proof that the test catches the bug.

## Family and registration

| what | command | EXIT | log |
| --- | --- | --- | --- |
| every target that includes the header (`zene`, the plugin, the sampler family) | `cmake --build build --target zene audiofileprocessor SamplerEnvelopeTest SamplerVoiceReuseTest AudioFileProcessorPlayheadTest AudioFileProcessorWaveViewTest SamplerTakeTest -j4 > $LOGS/build-8.log 2>&1` | 0 | build-8.log |
| the sampler / AudioFileProcessor family | `ctest -R 'Sampler|AudioFileProcessor' --output-on-failure > $LOGS/test-3.log 2>&1` | **0** (7/7 pass) | test-3.log |
| the whole-tree manifest reproduces | `bash tests/all-sources-reproduce.sh > $LOGS/allsources-verify.log 2>&1` | 0 (`REPRODUCES`) | allsources-verify.log |
| gates | `FEATURE_CHECK_LOG_DIR=$LOGS/feature-check python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $LOGS/feature_check-1.log 2>&1` | **0** | feature_check-1.log |

```
feature-check kads-new-p15-260-port (base fa4a5f2e7, 579 changed files; logs in .../logs/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Registration: `tests/src/plugins/SamplerStutterContinuationTest.cpp` is in `tests/fork-sources.txt` and in
`tests/all-sources.txt` (regenerated with the manifest's own recipe; the diff is exactly one added line and the
recipe's verifier says `REPRODUCES`). The changed inherited files — `plugins/AudioFileProcessor/AudioFileProcessor.h`,
`.cpp` and `tests/CMakeLists.txt` — are recorded in `tests/upstream-modifications.txt` with the round-3 reason. All
four list files end with a newline.

## Self-check

- **The regression FAILS with the product change reverted and PASSES with it (exit codes).** RED: the note-start
  caller reverted to the e4194e0 shape, everything else (helper, packed word, hook) present —
  `ctest -R '^SamplerStutterContinuationTest$'` **EXIT=8**, two cases failed on the combined publication
  (`test-red-1.log`). GREEN: `ctest -R '^SamplerStutterContinuationTest$'` **EXIT=0**
  (`test-green-1.log`, `test-2.log`). The whole-patch reversal is also recorded (build EXIT=2, stale binary
  EXIT=0) and why it is not the meaningful red: it deletes the seam the test needs.
- **The test is deterministic.** No wall-clock timing, no sleep, no thread, no race. The seam is the `LMMS_TESTING`
  `noteStartHook()` invoked inside `playNoteImpl()` between the continuation read and the voice state write; the
  values are injected (continuation `(midFrame, false)` / `(end-1, true)` / `(0, false)`, the 0.9 ramp written by
  `writeWav`, a fixed note key of 69 and fixed period lengths derived from the engine's `framesPerPeriod()`).
  Direction is compared over a full period so the 16-bit ramp's repeated levels cannot make it flaky.
- **The fix is at the cause and complete.** Every sibling of the value was checked and converted together: the
  magic-key reset, the note start, the per-period publish, `reverseModelChanged()`, `stutterModelChanged()`,
  `pointChanged()` and `beatLen()`. There is no other reader or writer of the continuation in the tree
  (`tests/reference/AudioFileProcessor/` is a frozen reference copy and is not compiled). The header and the
  definition changed together and every target that includes the header was rebuilt (build-3, build-8, and the
  green builds above).
- **No collateral damage.** The continuation is runtime-only: it is not saved or loaded, no parameter was added or
  reordered, and no voice limit or command schema changed. `SamplerEnvelopeTest` (including
  `stutterContinuesWhereThePreviousNoteStopped`, which asserts the behaviour the shared continuation exists for),
  `SamplerVoiceReuseTest`, `SamplerTakeTest`, `SamplerTakeGuiTest`, `AudioFileProcessorPlayheadTest` and
  `AudioFileProcessorWaveViewTest` all pass (`test-3.log`, 7/7). The only new check is the compile-time lock-free
  requirement; the only new limit is the 62-bit frame field, far beyond any sample (2^62 frames).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED. For the record, round 2's atomics
  (`b4078a8627` lineage) did not fix the pair coherence, which is why this round replaced them with one word.
- **Sibling commands.** No command file, schema or handler changed, so `app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`,
  `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks` and
  `arrangement.get_state` are unaffected (`ControlCommandsSnapshot` unchanged — no command was added, removed or
  edited). The sampler's `plugin.param_*` indices are unchanged too: no model was added or reordered.

## Correction to the round-2 report

`docs/qwen-fixes-2026-10/cluster-new-p15-260-port.md:128` claimed "`m_nextPlayBackwards` is always false in current
code". That was wrong: the per-period publish stores `voice.state.backwards()`, which ping-pong playback sets to
true at the loop end (`src/core/Sample.cpp:227-247`). That is the whole point of the finding, and the round-3 fix
carries the direction inside the one word instead of dropping it. The line is corrected in place.

## Other findings

- The two log entries from round 2 that named `SamplerEnvelopeTest::stutterStartIsOneCoherentSnapshot` are
  superseded: that test, its `#define private public` include and its `HasStutterStartSnapshot` trait were removed
  from `tests/src/core/SamplerEnvelopeTest.cpp` (the file keeps `stutterContinuesWhereThePreviousNoteStopped`), and
  the `tests/CMakeLists.txt` block that gave that test the plugin's include directory is gone with it.
- Test-environment noise, unchanged by this fix: fontconfig "No writable cache directories" lines in every test
  log, and the `hwloc ... sysfs cpu topology` notes in build logs.
- `tests/reference/AudioFileProcessor/` still holds the pre-fix copies of both files. It is a frozen reference copy
  and is not compiled, so it was deliberately left untouched.
