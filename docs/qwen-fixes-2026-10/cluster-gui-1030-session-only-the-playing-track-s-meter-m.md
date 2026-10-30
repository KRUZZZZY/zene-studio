FALSE-POSITIVE — the track level bar shows its mixer channel's level by design (MX-011); a per-track meter is a new feature, not a fix.

# Cluster gui-1030-session-only-the-playing-track-s-meter-m — "Track level bars show the shared mixer channel level"

Branch: `bugs/qwen-gui-1030-session-only-the-playing-track-s-meter-m`. Finding: `src/gui/LevelBar.cpp:110` (050/m0-train-24).

## What the finding claims

Two instrument tracks routed to the same mixer channel (the default master) show the same level, so a silent track's bar lights while the other plays. The expected behaviour is a per-track meter: the empty track's bar stays dark.

## Why this is the documented contract, not a defect

1. **The bar is defined as the channel's level.** `include/LevelBar.h:66` — "MX-011: a LevelBar on @a track's mixer channel, after its fader (the channel's RMS); silent for a track with none." The class comment (`include/LevelBar.h:45-47`) names the track headers as a user of the meter, and the reader is the channel strip's RMS.
2. **The code does exactly that.** `src/gui/LevelBar.cpp:105-111`: the source looks up `watched->mixerChannelModel()` and returns `max(strip->m_rmsLeft, strip->m_rmsRight)` of that channel. `m_rmsLeft/Right` are the channel's own buffer RMS × fader (`src/core/MixerStrip.cpp:324-325`), so a channel carries the sum of every track routed into it. Two tracks on one channel therefore show the same reading. The finding's own root cause says the same thing.
3. **The existing test pins the contract.** `tests/src/gui/TrackIconsLevelBarTest.cpp:248-262` (`aTrackLevelBarFollowsItsTracksMixerChannelAndIsSilentWithNone`) sets channel 2's RMS and asserts the track bar follows that channel and is silent with none. It passes on the unmodified tree (see the evidence table).
4. **The wiring is the same for both track kinds.** `src/gui/tracks/InstrumentTrackView.cpp:160` and `src/gui/tracks/SampleTrackView.cpp:119` both call `trackLevelBar(getTrack(), ...)`.
5. **No spec in the repo asks for a per-track meter.** A search of the repo (excluding `build/`) for "Only the playing track", "meter moves" and "track's level" returns only the `LevelBar.cpp:111` tooltip string, the test's accessible-name check, and the finding itself. The metering docs (`docs/MIXER-METERING.md`, `docs/KNOWN-LIMITATIONS.md` 0.5.0 L08 section) describe channel meters and say no track carries its own *input* meter; neither describes a per-track output meter.

## What a per-track meter would need (not done here)

Making the empty track's bar stay dark means a new meter source: each instrument track's own output RMS, taken before it sums into the channel. That is a new audio-path measurement (new state written per period, on the realtime path), and it needs owner decisions the finding does not settle: pre- or post-fader, what a track's share is when it sends to another channel, and whether channel-level bars stay as they are. Changing `LevelBar.cpp` alone would contradict `include/LevelBar.h:66` and the test at `TrackIconsLevelBarTest.cpp:248-262`. So no product behaviour is changed on this branch.

**Recommendation to the owner:** if per-track bars are wanted, file it as a feature request with those three decisions; it is not a bug fix.

## Commands and results

| # | Command (run from the worktree root or `build/`) | EXIT |
| --- | --- | --- |
| 1 | `cmake --build . --target TrackIconsLevelBarTest -j4 > .../logs/build-1-TrackIconsLevelBarTest.log 2>&1` | 2 |
| 2 | `CCACHE_TEMPDIR=$PWD/ccache-tmp cmake --build . --target TrackIconsLevelBarTest -j4 > .../logs/build-2-TrackIconsLevelBarTest.log 2>&1` | 0 |
| 3 | `cd build/tests && ctest -R '^TrackIconsLevelBarTest$' --output-on-failure > .../logs/test-1-TrackIconsLevelBarTest-baseline.log 2>&1` | 0 |
| 4 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1-feature-check.log 2>&1` | 1 |
| 5 | `python3 $TMPDIR/fc-gui1030-r1/feature_check.py <worktree> > .../logs/gates-1-feature-check.log 2>&1` (same script; see note) | 0 |
| 6 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1030-session-only-the-playing-track-s-meter-m > .../logs/submit-check-1.log 2>&1` | 1 |

- Run 4 stopped before any gate ran: `PermissionError: [Errno 13] ... '<projects>/zene-qwen-gui-1030-session-only-the-playing-track-s-meter-m-feature-check'`. `feature_check.py:70` puts its logs at `<worktree>/../<name>-feature-check`, which is the projects folder, outside this lane's write-jail. I did not create that folder and did not edit the shared script.
- Note on run 5: the copy differs from the shared script only at two lines: `ROOT` is pinned to the shared tooling folder (so `FIX-BASE` and the cached base listings for `2a7fd9f9c` resolve as before), and the log folder is the LOG FOLDER above. Gate commands, comparison logic and thresholds are unchanged. All eight rows PASS (`RESULT: PASS`). The complexity and file-length rows are the ratchets; each reports "no new item in your files".
- Run 6: the completion check runs the unmodified `feature_check.py`, so its `gates` row hits the same `PermissionError` and FAILS for an environment reason, not a gate result. Its other failures (uncommitted report, missing Self-check) are addressed in this commit.

- Run 1 failed inside ccache, not in the compiler: `ccache: error: failed to create temporary file for /home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6/...: Permission denied`. The default ccache temp dir is not writable from this sandbox. Run 2 pointed `CCACHE_TEMPDIR` at a writable directory inside `build/` and built cleanly. This only affects the build cache location, not the product code.
- Run 3: `100% tests passed out of 1`, 1 test executed (the family has one test), total time 9.96 s. The test passes on the current code, which is the "is the contract pinned" check.

The regression RED/GREEN procedure (steps 3, 4, 4b) does not apply: no product file was changed, so there is no fix to prove. Adding a test that asserted per-track behaviour would have to fail on the shipped contract, which would be a test of an unspecified feature, so none was added.

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-only-the-playing-track-s-meter-m.r1/logs/`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-gui-1030-session-only-the-playing-track-s-meter-m.md` (this report, the only file changed).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Not applicable: the verdict is FALSE-POSITIVE, so there is no product change to revert and no regression test was added. The existing test is the evidence: run 3 above, `TrackIconsLevelBarTest` EXIT=0 on the unmodified tree.
- **The test is deterministic (no wall-clock timing, sleeps or races; name the seam or injected values).** Not applicable (no new test). The existing test sets channel RMS directly (`TrackIconsLevelBarTest.cpp:258-262`, `m_rmsLeft/m_rmsRight.set(...)`) and reads the bar's source; it has no timing dependence on the assertion path.
- **The fix is at the cause and complete (every caller and sibling checked).** Not applicable (no fix). The callers checked: `trackLevelBar` is called from `InstrumentTrackView.cpp:160` and `SampleTrackView.cpp:119`, and both pass the track's own channel. The channel assignment is a track's `mixerChannelModel()` value, set by the track view (`src/gui/tracks/InstrumentTrackView.cpp:251`) and ranged by the mixer (`src/gui/MixerView.cpp:454-460`); changing how routing works is outside this finding.
- **No collateral damage (old projects and saved data; limits don't hit other callers).** Not applicable: no product file changed, so saved projects and other meter callers (`ChannelLevelMeter.cpp:100-101`, `MixerWindows.cpp:92`, `MixerViewOptions.cpp:135-136`) read the same channel RMS as before.
- **If ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** `include/LevelBar.h:66` ("MX-011: a LevelBar on @a track's mixer channel, after its fader (the channel's RMS)"); `src/gui/LevelBar.cpp:107-110` (channel lookup and RMS return); `src/core/MixerStrip.cpp:324-325` (channel RMS from the shared buffer); `tests/src/gui/TrackIconsLevelBarTest.cpp:248-262` (the test that pins the contract, passing). The sweep's "only the playing track's meter moves" expectation has no spec in the repo (see the claim section above).

**Open items for the owner / loop (not gate failures of this branch):**
- `submit_check.py` `gates` row FAILS for an environment reason: the shared `feature_check.py` cannot write its logs outside the write-jail (run 4). Run 5 is the same check with only the log location changed, and it passes all eight rows. The loop should run the gate with a writable log location, or the shared script should take one.
- The `ccache` default temp dir is not writable from this lane (run 1); run 2 used `CCACHE_TEMPDIR` inside `build/`.
- If the owner does want per-track bars, that is a new feature (see "What a per-track meter would need"), not a bug fix on this branch.
