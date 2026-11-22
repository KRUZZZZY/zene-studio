ALREADY-FIXED

# cluster new-p9-151: GIG sample-vector copies lose active playback state

new-p9-151: ALREADY-FIXED

## Finding
`GigSample`'s copy constructor and assignment (historical `GigPlayer.cpp:1106-1130`) did not
keep the resampler, the source and mix buffers, or their span views. Active samples were held in
`std::vector<GigSample>`, so when that vector grew or had an element erased, a playing sample
was copied, or another sample was assigned over it.

## Evidence that it is fixed on this branch
The defective code is still visible in the upstream snapshot `tests/reference/GigPlayer/GigPlayer.h:157-158`
(the copy constructor and assignment) and `:216` (`std::vector<GigSample> samples;`). The current product code no longer has it:

- `plugins/GigPlayer/GigPlayer.h:195-196`: `GigSample( const GigSample& ) = delete;` and
  `GigSample& operator=( const GigSample& ) = delete;`. Because copying is user-declared as deleted,
  C++ declares no implicit move constructor or move assignment. A `GigSample` therefore cannot be
  copied, moved or relocated, and any code that tried would not compile.
- `plugins/GigPlayer/GigPlayer.h:345-355`: every `GigSample` sits inside a `GigVoice`. Each note
  links its voices by index (`nextInNote`/`prevInNote`), and no per-note vector holds them.
  `plugins/GigPlayer/GigPlayer.h` holds no `std::vector` or `QVector`. Its only `QList` is
  `m_retiredStates` (line 472), a list of `GigFileState*`.
- `plugins/GigPlayer/GigPlayer.h:504-505` and `plugins/GigPlayer/GigPlayer.cpp:564-574`: voices are
  built in `std::unique_ptr<GigVoice[]>` segments that are never reallocated. A new segment is
  added by storing one more pointer into a fixed `std::array<std::atomic<GigVoiceSegment*>, MaxGigSegments>`.
  Existing voices never change address, so their `m_sourceBufferView` and `m_mixBufferView` spans
  (which point into the voice's own `m_sourceBuffer`/`m_mixBuffer`) stay valid.
- `plugins/GigPlayer/GigPlayer.cpp:1951-1966` `GigSample::attach`: a voice is reused in place.
  The method resets the resampler and clears both views. A voice is only given new state when it
  receives a new sample; it never takes over the state of another voice.
- Fixing commit: `ffe21f4714 port(gigplayer): the audio callback neither locks nor waits for a GIG
  file load (cluster new-p9-149-port)` (found with `git log -S"GigSample( const GigSample& ) = delete"`).
  That commit replaced the per-note `std::vector<GigSample>` with the fixed voice pool.
  `2b2b319acc` later grew the pool in segments that are never relocated.

No product code was changed by this job, and no new regression test was added. The bug cannot
come back without first removing the `= delete` declarations, because any relocation of a
`GigSample` would then fail to compile. The existing tests that create several voices per note,
including release voices, pass (see below).

## Commands and results
- `cmake --build build --target zene PluginPortsMigrationTest -j4 > logs/build-1.log 2>&1; echo EXIT=$?` gave EXIT=0
- (in build/tests) `ctest -R PluginPortsMigrationTest --output-on-failure > logs/test-1.log 2>&1; echo EXIT=$?` gave EXIT=8
  - Totals: 90 passed, 4 failed, 2 skipped. Every GIG test passed, among them `gigplayerKeepsEveryLayerOfANote`,
    `gigplayerKeepsEveryOverlappingNote`, `gigplayerPlaysMoreThan512OverlappingNotes`,
    `gigplayerSurvivesGlobalVoicePoolExhaustion`, `gigplayerAudioPathDoesNotAllocate`,
    `gigPlayerKeepsReleaseTriggerAcrossRegions` and `gigOneFrameReleaseSampleIsAudible`.
  - The 4 failures are outside GigPlayer (see Other findings).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1.log 2>&1; echo EXIT=$?` gave EXIT=0, RESULT: PASS
  (all 8 gates PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
  no-tautology, doc-figures, complexity, file-length).

Here, logs/ means /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-151.r1/logs.

## Other findings
These failures were seen in the same PluginPortsMigrationTest run. They are not related to GigPlayer and were not investigated or fixed here:
- `malletsVoiceLimitIsEnforcedPerInstrument` (PluginPortsMigrationTest.cpp:2718), `malletsVoiceLimitSurvivesDeviceChange` (:2876),
  and `slice8PluginsAreLiveAndExact` (:5665, "malletsstk rendered silence"): Mallets renders silence on this box. This may be
  because the STK rawwaves are missing; that cause is not verified.
- `nonRealtimeLadspaEffectsAreInvalid` (:1623): "SWH Decimator test fixture was not loaded".

## Self-check
- **Regression test fails without the fix and passes with it:** Not applicable. The verdict is ALREADY-FIXED and this job changed no product code, so there is no patch to revert. The fix is enforced when the code compiles: `GigPlayer.h:195-196` deletes copying, and that also suppresses moving. Code that copies or relocates a `GigSample` cannot be built. The existing GIG tests pass on this branch (`logs/test-1.log`, lines 5609-5617 and 12591-12592).
- **Deterministic test:** No new test was added. The existing GIG tests in PluginPortsMigrationTest render offline from fixed fixtures.
- **Fix at the cause and complete:** The cause was relocating active `GigSample`s inside a `std::vector`. That vector is gone. Every voice lives in a `GigVoiceSegment` array that is never relocated (`GigPlayer.h:504-505`, `GigPlayer.cpp:564-574`), and is reached through `GigInstrument::voice()` (`GigPlayer.cpp:630-636`). The two uses of a `GigSample` (`GigPlayer.cpp:1039` and `:1086`) take a reference, `GigSample & sample = voice( v ).sample;`, so neither copies one.
- **No collateral damage:** This job changed no product code or saved-data code, and added no limits or checks.
- **ALREADY-FIXED evidence:** See `plugins/GigPlayer/GigPlayer.h:195-196`, `:345-355` and `:504-505`, `plugins/GigPlayer/GigPlayer.cpp:564-574` and `:1951-1966`, and fixing commit `ffe21f4714` (cluster new-p9-149-port). The old defect can still be seen in `tests/reference/GigPlayer/GigPlayer.h:157-158,216`.
- **Sibling commands (app.*, arrangement.*):** Not affected. This job changed no command files. The listed test files and commands come from earlier commits on this branch's train history, not from this job. This job's only commit adds this report.
