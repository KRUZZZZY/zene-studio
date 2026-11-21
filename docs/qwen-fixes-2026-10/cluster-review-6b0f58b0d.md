# Cluster review-6b0f58b0d — zone voice tail under a fractional playback step

**Verdict:** ALREADY-FIXED

## Finding
Luna's patrol reported that `RackZoneVoice` ends at the *first visit of the last frame*
instead of when the position has passed the end of the sample. For a fractional step
(`step < 1`, a note pitched below the sample's own rate) that last frame has to be
visited several times, so the tail was cut early.

That is exactly the bug the earlier rework of cluster `6b0f58b0d` repaired in commit
`9b02a1992` ("fix(rework 6b0f58b0d): a fractional-step zone voice ended on its first
visit to the last frame"), which is an ancestor of this branch's HEAD (`2da2ed356`).
The behaviour the finding asks for is the behaviour in the tree today: no product
change was needed, so this commit adds the missing regression coverage only.

## Evidence (file:line) — why the code is already correct
- `src/core/RackZoneInputs.cpp:54` — `const std::size_t next = index + 1 < size ? index + 1 : index;`
  The last source frame is its own interpolation partner, so it is *held* (it keeps
  sounding) instead of decaying into silence, and a one-frame sample is not silent.
- `src/core/RackZoneInputs.cpp:59-60` — `position += step;` then
  `if (position >= static_cast<double>(size)) { ended = true; }`
  The voice ends only once the position has *passed the end of the sample* — not on the
  first visit to the last frame. With `step = 0.5` over three frames the last frame is
  written twice (fraction 0.0 and 0.5) before the voice ends.
- `src/core/RackZoneInputs.cpp:50` — `if (index >= size) { ended = true; break; }`
  The bounds guard is still in place (the read never runs past the sample).
- `include/RackZoneInputs.h:59-61` — the contract: "plays the decoded file from its
  start, linearly interpolated at the note's pitch, **until the file ends**".
- `src/core/RackZoneInputs.cpp:147` and `src/core/RackZoneFeed.cpp:97` — the fractional
  step is real: `step = note.pitch * sample.sampleRate() / m_outputRate` and
  `note.pitch = nph->frequency() / DefaultBaseFreq`, so any note below the base
  frequency (e.g. a low MIDI note) plays with `step < 1`.
- `tests/src/core/RackZoneRoutingTest.cpp:285` — `aFractionalStepHoldsTheLastSourceFrame`
  (the regression written by the earlier rework) already covers step 0.5.
- `tests/src/core/RackZoneRoutingTest.cpp:301` — `aFractionalStepSoundsEveryFinalFrameVisit`
  (added by this commit) covers a fractional step that does **not** divide the sample
  length (0.25 over three frames): twelve outputs, of which the last source frame is
  held for all four visits inside it.

## Commands and EXIT codes
All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-6b0f58b0d.r1/logs`.

Build (HEAD, product code unchanged):
`cmake --build build --target RackZoneRoutingTest -j$(nproc)` → **EXIT=0** (build-2.log)

GREEN at HEAD (with the new regression, fix in place):
`cd build/tests && ctest -R '^RackZoneRoutingTest$' --output-on-failure` → **EXIT=0**
(test-2-green.log) — 1 test executable, all slots passed.

### RED / GREEN proof that the regressions catch the finding
`logs/fix.patch` is the product-file part of the historical fix `9b02a1992`
(`src/core/RackZoneInputs.cpp` only), so the documented pair of commands reproduces the
finding exactly: `git apply -R fix.patch` puts the *buggy* "end on the first visit of
the last frame" code back, `git apply fix.patch` restores the fix.

RED (the finding's code):
`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-6b0f58b0d.r1/logs/fix.patch` → **EXIT=0**
`cmake --build build --target RackZoneRoutingTest -j$(nproc)` → **EXIT=0** (build-3-red.log)
`cd build/tests && ctest -R '^RackZoneRoutingTest$' --output-on-failure` → **EXIT=8** (test-3-red.log)

Failing assertions in test-3-red.log:
```
FAIL!  : RackZoneRoutingTest::aFractionalStepHoldsTheLastSourceFrame() Compared floats are not the same (fuzzy compare)
  Actual   (out[...][0]): 0 (0x0p+0)
  Expected (expected[i])  : 3 (0x1.8p+1)
  Loc: [../../tests/src/core/RackZoneRoutingTest.cpp(294)]
FAIL!  : RackZoneRoutingTest::aFractionalStepSoundsEveryFinalFrameVisit() Compared floats are not the same (fuzzy compare)
  Actual   (out[...][0]): 0 (0x0p+0)
  Expected (expected[i])  : 3 (0x1.8p+1)
  Loc: [../../tests/src/core/RackZoneRoutingTest.cpp(311)]
Totals: 10 passed, 2 failed, 0 skipped, 0 blacklisted, 1129ms
```

GREEN (the fix restored):
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-6b0f58b0d.r1/logs/fix.patch` → **EXIT=0**
`cmake --build build --target RackZoneRoutingTest -j$(nproc)` → **EXIT=0** (build-4-green.log)
`cd build/tests && ctest -R '^RackZoneRoutingTest$' --output-on-failure` → **EXIT=0**
(test-4-green.log) — 1 test executable, all slots passed.

Affected family (rack/zone):
`cmake --build build --target RackMacrosTest RackTest RackZonesTest RackZoneFeedRunTest InstrumentLayerZonePublishTest EffectRackAddTest RackDeferredLifetimeTest RackMenuTest -j$(nproc)` → **EXIT=0** (build-6-family.log)
`cd build/tests && ctest -R 'Rack|Zone' --output-on-failure` → **EXIT=0** (test-6-family.log) — **9/9 tests passed**
(RackZoneFeedRunTest, RackMacrosTest, RackTest, RackZonesTest, RackZoneRoutingTest,
InstrumentLayerZonePublishTest, EffectRackAddTest, RackDeferredLifetimeTest, RackMenuTest).

`zene` itself was rebuilt too: `cmake --build build --target zene -j$(nproc)` → **EXIT=0** (build-5-zene.log).

Gates:
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → **EXIT=0** (gates-1.log);
every gate PASS, `complexity` and `file-length` report "no new item in your files".

## Summary
The reported behaviour (a zone voice whose tail is cut at the first visit of the last
frame under a fractional step) is not in the current tree: the last frame is held
(`src/core/RackZoneInputs.cpp:54`) and the voice ends when the position passes the end
of the sample (`src/core/RackZoneInputs.cpp:60`). The fix is commit `9b02a1992`, already
in this branch's history. This commit adds the missing coverage for a fractional step
that does not divide the sample length, and proves with a revert/re-apply cycle that
both regressions fail on the reported code and pass on the fixed code.

## Self-check
1. **The regression test FAILS with your product change reverted and PASSES with it.**
   No product change was needed, so the "reverted" state is the finding's own code,
   restored with `git apply -R logs/fix.patch` (the product-file part of `9b02a1992`).
   RED: build **EXIT=0**, `ctest -R '^RackZoneRoutingTest$'` **EXIT=8** (test-3-red.log,
   10 passed / 2 failed, `out[5]` = 0 where 3 is expected). GREEN: build **EXIT=0**,
   ctest **EXIT=0** (test-4-green.log). The same cycle is what step 4b asks for, with
   the patch that carries the fix.
2. **The test is deterministic.** Both slots drive `RackZoneVoice::render()` directly
   with an injected `step` (0.5 and 0.25) over a fixed three-frame `SampleBuffer`
   (`{1,1},{2,2},{3,3}`); no wall-clock, no sleep, no thread, no random value, and
   every expected value is exactly representable in binary floating point (quarters),
   so the comparisons are exact. The seam is the `render(sample, dst, frames, step,
   gain, released)` seam of `include/RackZoneInputs.h:76`.
3. **The fix is at the cause and complete.** The only implementation of the zone voice
   in the fork is `RackZoneVoice::render()` (`src/core/RackZoneInputs.cpp:42`); its only
   caller is `RackZoneInputs::playVoice()` (`src/core/RackZoneInputs.cpp:139-150`), whose
   only caller is `RackZoneFeed.cpp:122`. The bounds guard (`:50`), the held last frame
   (`:54`) and the end condition (`:60`) are consistent with each other and with the
   header contract (`include/RackZoneInputs.h:59-61`). No sibling implementation of the
   same stepped-interpolation loop exists elsewhere in the fork's `src/` or `include/`.
   The other rack/zone tests still pass (9/9, test-6-family.log).
4. **No collateral damage.** This commit changes one test file only — no product code,
   no data format, no save/load path, no command. The product behaviour is exactly the
   one that was already in the tree, so old projects and saved data load and render as
   before; no limit or check was added to any product path, so no caller can trip over
   it. The family run (test-6-family.log) is green.
5. **If the verdict is ALREADY-FIXED: the code (file:line) or the intended contract.**
   `src/core/RackZoneInputs.cpp:54` (the last frame is its own interpolation partner),
   `src/core/RackZoneInputs.cpp:60` (the voice ends when `position >= size`), the
   contract in `include/RackZoneInputs.h:59-61` ("until the file ends"), and the
   existing regression `tests/src/core/RackZoneRoutingTest.cpp:285`. The historical fix
   is commit `9b02a1992`, an ancestor of HEAD; the RED run above reproduces the
   finding's code and fails, which shows the finding described a real bug in that older
   code and that the current tree does not have it.
6. **Realtime, undo and commands.** Nothing on the audio path was touched: no product
   file changed, so nothing allocates, frees, locks, waits, logs or emits on the audio
   thread; `RackZoneVoice::render()` remains allocation-free (the existing
   `zonedNotesAllocateNothing` slot in this test still passes). No control command was
   added, so the `ControlCommandsSnapshot` canary is untouched. Undo is untouched (no
   product edit at all).

## Files committed
- tests/src/core/RackZoneRoutingTest.cpp (new regression slot
  `aFractionalStepSoundsEveryFinalFrameVisit`)
- docs/qwen-fixes-2026-10/cluster-review-6b0f58b0d.md (this report)
