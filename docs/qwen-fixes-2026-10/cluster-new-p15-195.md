# Cluster new-p15-195

Verdict: **FIXED**

new-p15-195: FIXED
new-p12-234: ALREADY-FIXED
new-p15-194: ALREADY-FIXED

Same-file batch in `src/core/SessionAudioCells.cpp`: three findings, one product fix (new-p15-195),
two verdicts with code evidence (new-p12-234, new-p15-194).

## new-p15-195: FIXED - the warp-variant limit counted the original clips

### Cause

`sessionaudio`'s clip cache kept the loaded source files and the settings-variants (the warped /
repitched clip of a cell) in ONE map, `Clips::held`, and the variant budget was checked against
that map's size:

- `src/core/SessionAudioCells.cpp:153` (before the fix) - `std::map<QString, std::shared_ptr<const SampleBuffer>> held;` held both the source clips (key: path) and the variants (key: `path|w<speed>|p<ratio>`).
- `src/core/SessionAudioCells.cpp:210` (before the fix) - `if (all.held.size() >= MaxHeldVariants) { return original; }` - the 256-variant budget was measured against sources + variants.

So a project that had loaded 256 distinct source clips spent the whole variant budget on them:
the next warped cell was refused its variant and played its file as it is (unwarped). The budget
is documented as a limit on the settings-variants (`MaxHeldVariants`, "The most settings-variants
of clips one project may hold"), and `load()` never applied any limit to the source files, so the
two counts were never meant to be the same number.

### Fix

The variants now live in their own map, and the budget counts the variants alone:

- `src/core/SessionAudioCells.cpp:154` - `Clips::variants` (the settings-variants, by path and settings) beside `Clips::held` (the source files, by path).
- `src/core/SessionAudioCells.cpp:212-214` - `variant()` looks the key up in `all.variants` and refuses a NEW variant only when `all.variants.size() >= MaxHeldVariants`.
- `src/core/SessionAudioCells.cpp:186-190` - the engine-rate change moves the variants into `retired` too (they were baked at the old rate, exactly like the sources were).
- `src/core/SessionAudioCells.cpp:270-274` - `retire()` retires and clears the variants too (a project close frees every clip it held).
- `src/core/SessionAudioCells.cpp:280` - `heldClips()` still counts EVERY clip the project holds (sources + variants), the same number as before the split.

Nothing else moved: `load()` still caches the source files without a limit (as before), the variant
key and its shape are unchanged, and the audio thread still only reads a finished `SampleBuffer`.

### Regression test

`tests/src/core/SessionAudioCellPlaybackTest.cpp` - `theVariantLimitCountsVariantsNotLoadedClips()`:
it loads 256 distinct source clips (each its own file, through `sessionaudio::prepare()`), then
prepares a warped cell of a 257th file (`warp_bpm` 60 against a tempo of 120, so the baked clip is
half as long). The published clip must be the baked variant (300 frames), not the unwarped original
(600 frames).

Deterministic: no wall-clock timing, no sleeps, no races - the clip lengths are exact frame counts
of files the test writes itself, and the engine rate is the one the files are written at.

## new-p12-234: ALREADY-FIXED - the render path already honours the cell's playback settings

The finding's claim ("The render loop copies raw sample frames and wraps at the full buffer length.
It does not apply the cell's saved gain, transpose, detune, or loop start and length") does not
describe this tree: the settings are applied, in two halves.

Gain and the loop region are applied in the render path itself:

- `src/core/SessionAudioCells.cpp:135` - `const float gain = view.audio.gain;`
- `src/core/SessionAudioCells.cpp:138` - `frame = clip[m_position] * gain;`
- `src/core/SessionAudioCells.cpp:91-98` - `loopRegion()` - the cell's loop start / length in frames of the clip, the whole clip when none is set.
- `src/core/SessionAudioCells.cpp:124` - `const auto [loopStart, loopEnd] = loopRegion(view.audio, size);`
- `src/core/SessionAudioCells.cpp:139` - `m_position = m_position + 1 < loopEnd ? m_position + 1 : loopStart;` - the wrap is at the loop end, not at the buffer length.

Transpose, detune and the tempo warp are applied on the model thread into the published clip
(the design the header documents at `include/SessionAudioCells.h:52-54`):

- `src/core/SessionAudioCells.cpp:253` - `prepared.clip = variant(source, original, bakeParamsFor(slot, tempo));`
- `src/core/SessionAudioBake.cpp:42-47` - `bakeParamsFor()` - the warp speed from `warp` / `warp_bpm` / tempo and the pitch ratio `2^((transpose + detune/100) / 12)`.
- `src/core/SessionAudioCells.cpp:254-257` - `prepare()` puts the cell's gain and loop region into `prepared.shape`, which `src/core/SessionPlayback.cpp:234` publishes and `play()` reads back as `view.audio`.

The fix is in the base of this branch: commit `174ab288f` "feat(session): audio cells play on
instrument columns and honour gain, loop, transpose, detune and tempo warp (Q050-013)" (an ancestor
of HEAD). The behaviour is covered by the tests of that family, all passing in the runs below:
`gainScalesTheRenderedLevelOnBothTrackTypes`, `theLoopRegionBoundsThePlayedFrames`,
`transposeAndDetuneRepitchTheClip`, `warpFollowsTheSongTempoKeepingPitch`.

## new-p15-194: ALREADY-FIXED - the cell's play handle is removed with its track

The finding's claim ("track destruction only nulls the `QPointer` in `handled`; it does not remove
the registered handle") is true of `handled` and false of the handle: the handle is removed by the
track's own destructor, before the track (and its bus) is gone.

- `src/tracks/SampleTrack.cpp:75-78` - `~SampleTrack()`: `Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SamplePlayHandle | PlayHandle::Type::InputMonitorHandle | PlayHandle::Type::SessionCellHandle );`
- `src/tracks/InstrumentTrack.cpp:224-227` - `~InstrumentTrack()`: `requestChangeInModel()` / `removePlayHandlesOfTypes( this, PlayHandle::Type::SessionCellHandle )` / `doneChangeInModel()` - the removal under the engine's model-change lock, so the audio thread cannot be inside the handle while it is retired.
- `src/core/AudioEngine.cpp:1320-1399` - `removePlayHandlesOfTypes()` walks the render list, the STAGED list (`m_newPlayHandles`, the list a handle waits in until the next period) and the capacity-deferred list, so a handle cannot survive its track in any of the three places; it also detaches the handle from its bus handle (`:1328` / `:1360` / `:1386` `removePlayHandleUnderEngineChangeLock`) and disposes it.

Only the two track types that can carry a cell are concerned - `src/core/SessionAudioCells.cpp:219-222`
`canRenderCells()` accepts a `SampleTrack` or an `InstrumentTrack` only - and both destructors
remove the `SessionCellHandle`.

`Clips::handled` (`src/core/SessionAudioCells.cpp:157`) is bookkeeping only: it keeps Qt's
`QPointer` of the tracks that already have a handle so that `ensureHandle()` does not register a
second one. Qt nulls the pointer when the track dies and `ensureHandle()` (`:227-228`) drops the
dead entry - it never owns the handle.

Both destructor removals are in the base of this branch: `363054390` (SampleTrack, with the
WF-055 feature) and `073cb5038` (InstrumentTrack, with the Q050-013 feature); both are ancestors
of HEAD.

## Commands and exit codes

All build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-195.r1/logs`.

| Evidence | Command (working directory) | Exit |
| --- | --- | ---: |
| RED on the original code (`test-1.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 8 |
| GREEN with the fix (`test-2.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 0 |
| RED after reversing `fix.patch` (`test-3.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 8 |
| GREEN after reapplying `fix.patch` (`test-4.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 0 |
| Wider family (`test-6.log`) | `ctest -R 'Session\|AudioCell' --output-on-failure` (`build/tests`) | 8 (1 pre-existing failure, see below) |
| The same failure without the fix (`test-7.log`) | `ctest -R '^AbletonLinkSessionTest$' --output-on-failure` (`build/tests`) | 8 (pre-existing) |
| Gates (`gate-1.log`) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 |

The RED run (`test-1.log`, `test-3.log`) fails exactly in the new case:

```
FAIL!  : SessionAudioCellPlaybackTest::theVariantLimitCountsVariantsNotLoadedClips() Compared values are not the same
   Actual   (baked.clip->size()): 600
   Expected (std::size_t{300})  : 300
Totals: 13 passed, 1 failed, 0 skipped, 0 blacklisted
```

The GREEN runs (`test-2.log`, `test-4.log`) report `Totals: 14 passed, 0 failed` - the 13 cases of
the family plus the new one.

The fix-removal command was
`git diff -- src/core/SessionAudioCells.cpp > ~/.local/share/zene-bunny-fix/qwen/runs/new-p15-195.r1/logs/fix.patch && git apply -R ~/.local/share/zene-bunny-fix/qwen/runs/new-p15-195.r1/logs/fix.patch`
(exit 0); the restoration command was
`git apply ~/.local/share/zene-bunny-fix/qwen/runs/new-p15-195.r1/logs/fix.patch` (exit 0).

Every build used `cmake --build build --target <targets>` from the worktree root and exited 0
(`build-1.log` .. `build-8.log`); `zene` was rebuilt after the change (`build-2.log`, `build-4.log`,
`build-5.log`, `build-8.log`).

The wider family (`test-6.log`) ran 29 tests: 28 passed, and `AbletonLinkSessionTest` segfaulted in
`lmms::AbletonLinkSession::numPeers()` after its own multicast datagram was SKIPped (the host
delivers no multicast datagram in this environment). That failure is NOT collateral damage of this
change: with the fix reverted (`git apply -R fix.patch`), rebuilt and run (`build-7.log`,
`test-7.log`), the same test segfaults at the same place - exit 8 both ways. It is a sandbox /
Ableton Link environment failure, untouched by the audio-cell clip cache.

The gate table (`gate-1.log`) is all PASS: fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length
("no new item in your files").

## Self-check

The completion check `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-195`
reports `RESULT: PASS - submit` (commits on the branch, working tree clean, report present, tests
changed, gates PASS). Its five ASK items are answered below.

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   RED `test-1.log` exit 8 (the new case fails: 600 frames instead of 300) on the original code;
   GREEN `test-2.log` exit 0 with the fix; RED `test-3.log` exit 8 after `git apply -R fix.patch`;
   GREEN `test-4.log` exit 0 after `git apply fix.patch`. The patch is only the product file
   (`src/core/SessionAudioCells.cpp`), the test file stays in place for both runs.
2. **The test is deterministic.** It counts frames of WAV files the test writes itself
   (`writeFloatWav`, the engine's own rate), and reads the published clip's frame count. No clock,
   no sleep, no thread, no randomness: the same 600 vs 300 frame difference every run.
3. **The fix is at the cause and complete.** The cause is the shared map: the budget was measured
   against `held`, which held the sources too. Every user of the cache was checked and kept
   consistent: `load()` (source lookup, engine-rate change), `variant()` (lookup, budget, insert),
   `retire()` (project close frees every clip), `heldClips()` (still counts every held clip),
   `prepare()` (unchanged), and the audio thread, which only reads a finished `SampleBuffer`.
   The sibling of the change - the engine-rate path - clears the variants as well, because a
   variant baked at the old rate is as stale as a source resampled at the old rate.
4. **No collateral damage.** The sources are still loaded and cached exactly as before, without a
   limit; the variant key, its shape and the published clip are unchanged; `heldClips()` returns
   the same total as before the split. The new limit applies to the variants only, and no other
   caller is charged for it: the only writer of a variant is `variant()`, and the only reader of
   the map is `variant()` itself. Old projects and saved data are untouched - the split is in the
   in-memory cache, not in the file format. The wider `Session|AudioCell` family (29 tests) is
   green apart from the pre-existing `AbletonLinkSessionTest` segfault, which reproduces with the
   fix reverted (`test-7.log`, exit 8) and is a multicast-environment failure of the Ableton Link
   code, not of this change.
5. **Nothing new on the audio thread.** The change is entirely in the model-thread cache
   (`Clips`, `load()`, `variant()`, `retire()`); `SessionCellPlayHandle::play()` is not touched.
   The variant is still baked on the model thread (`bakeClip`) and published as a finished
   `SampleBuffer`. The realtime cases of the family (`theCellRendererDoesNotAllocate`,
   `theInstrumentCellRendererDoesNotAllocateEither`) still pass.
6. **Undo and commands.** No command was added or changed, so no reversibility class and no
   command snapshot change; the fix touches no undoable state (the cache is rebuilt from the
   files).
7. **Portability.** No new API, no platform-specific code: the change is a second `std::map` in an
   anonymous-namespace struct and the same `std::map` operations as before.
8. **The ALREADY-FIXED verdicts cite the code.** new-p12-234: `src/core/SessionAudioCells.cpp:135`,
   `:138` (gain), `:91-98`, `:124`, `:139` (loop region), `:253` with
   `src/core/SessionAudioBake.cpp:42-47` (transpose / detune / warp), fixed in the base by
   `174ab288f`. new-p15-194: `src/tracks/SampleTrack.cpp:75-78` and
   `src/tracks/InstrumentTrack.cpp:224-227` remove the `SessionCellHandle` on destruction, and
   `src/core/AudioEngine.cpp:1320-1399` retires it from every list under the model-change lock;
   both removals are in the base (`363054390`, `073cb5038`).
9. **Registration.** `src/core/SessionAudioCells.cpp` is a fork source
   (`tests/fork-sources.txt:1845`); both changed files are registered with a reason in
   `tests/upstream-modifications.txt` (the file ends with a newline).

## Files committed

- `src/core/SessionAudioCells.cpp`
- `tests/src/core/SessionAudioCellPlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p15-195.md`
