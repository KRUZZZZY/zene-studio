FIXED

# cluster new-p9-157: short reads in the GIG forward-loop path converted uninitialised stack bytes

## Verdict evidence
- Before the fix, `GigInstrument::loadSample` (plugins/GigPlayer/GigPlayer.cpp, forward-loop branch) filled a VLA `int8_t buffer[allocationsize]` with a do/while of `Read()` calls. The loop ended on `readsamples <= 0` (source exhausted, e.g. a loop whose end lies past the sample data) or on a short read, and nothing zeroed the unread tail. The conversion loops that follow (24-bit and 16-bit) read all `samples` frames, so indeterminate stack bytes became audio.
- Sibling paths checked: the non-looped branch already zero-fills (`memset` after `Read`), and the bidirectional path (`readSampleSegment`/`loadBidirectionalSample`) already zero-fills. Only the forward loop was missing it.
- Also fixed in the same loop: `min(samplestoread, samplestoloopend)` was passed to the unsigned `Read()` even when `samplestoloopend` was <= 0, which would wrap to a huge read into the stack buffer. The loop now stops in that case.

## Fix
- The forward-loop read moved into `gigplayerReadForwardLoop` (plugins/GigPlayer/GigPlayer.cpp:1254), exported like the existing `gigplayerMixResampledBlock` seam. `loadSample` calls it (GigPlayer.cpp:1398-1399). It zero-fills the unread tail and stops on a non-positive distance to loop end or a non-positive read.
- Audio thread: nothing added that allocates, locks, waits or logs (a loop and a memset on the existing stack buffer).
- No control commands added.

## Commits
1. `60aabdacba`: behaviour-identical extraction of the loop behind the seam, plus the regression test (red against it).
2. The fix (zero-fill and guard), plus this report.

## Regression test
`PluginPortsMigrationTest::gigplayerForwardLoopZeroFillsShortRead` (tests/src/plugins/PluginPortsMigrationTest.cpp). It drives the seam with a fake 100-frame source and a loop ending at 200 (so the source runs dry after 10 frames), with the buffer poisoned with 0x55, and checks the read count and that the tail is zero. A second case checks that a real wrap still fills the whole block.

## Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-157.r1/logs)
- Build with seam, no fix: `cmake --build build --target zene gigplayer PluginPortsMigrationTest -j4` EXIT=0 (build-1.log)
- RED, commit 60aabdacba state: `build/tests/PluginPortsMigrationTest gigplayerForwardLoopZeroFillsShortRead` EXIT=1: `byte 40 is 85, expected 0 (unread tail must be zero)` (test-1.log)
- GREEN with fix: build EXIT=0 (build-2.log); same test EXIT=0, 3 passed (test-2.log)
- 4b: `git diff -- plugins/GigPlayer/GigPlayer.cpp > fix.patch && git apply -R fix.patch` EXIT=0; rebuild EXIT=0 (build-red.log); test EXIT=1 (test-red.log, same failure message). `git apply fix.patch` EXIT=0; rebuild EXIT=0 (build-green.log); test EXIT=0 (test-green.log).
- Family: `ctest -R PluginPortsMigrationTest --output-on-failure` from build/tests EXIT=8 (test-3.log); direct run of the whole binary: 91 passed, 4 failed, 2 skipped, EXIT=4 (test-family.log). The 4 failures are malletsVoiceLimitIsEnforcedPerInstrument, malletsVoiceLimitSurvivesDeviceChange, nonRealtimeLadspaEffectsAreInvalid, slice8PluginsAreLiveAndExact. They report Mallets rendering silence and a missing SWH Decimator LADSPA fixture, which are environment gaps unrelated to GIG. I did NOT run them on a clean base to confirm they predate this change. All 15 gig tests pass.
- Gates: `feature_check.py` EXIT=0, RESULT: PASS (gates.log).

## Not verified
- No TSan locally. No libgig-backed end-to-end render of a loop-past-end fixture was added; the seam test uses an injected source.

## Other findings
- Mallets and SWH LADSPA tests in PluginPortsMigrationTest fail on this machine (see above).
