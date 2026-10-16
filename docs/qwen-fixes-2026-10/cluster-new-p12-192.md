# Cluster new-p12-192 — Reused note handles can resume an old sample voice (rework, round 2)

**Verdict:** FIXED

## Finding
Luna's round-1 fix detected note reuse from `framesPlayed` alone. That heuristic
restarts a voice whenever a note's frame count is `<= m_frames` or drops below the
voice's last count. A continuing note is indistinguishable from a reused handle:
`Song::setTempo()` → `NotePlayHandle::resize()` (NotePlayHandle.cpp:700) rescales and
truncates `m_totalFramesPlayed`, so a one-frame note can report one frame on the next
period and spuriously restart its voice at the sample start.

The fix assigns a stable per-note **generation** to every `NotePlayHandle` on
construction and on every pooled reuse, propagates it through `RackZoneNote`, and
restarts a `RackZoneVoice` only when the generation changes. A continuing note keeps
its generation and resumes where it left off; a reused handle carries a new generation
and restarts.

## Evidence (file:line)
- `include/NotePlayHandle.h:202` — `generation()` accessor; `m_generation` member at
  `include/NotePlayHandle.h:357`; `s_generationCounter` static at
  `include/NotePlayHandle.h:398`.
- `src/core/NotePlayHandle.cpp:88` — constructor bumps `m_generation = ++s_generationCounter`
  (runs on every construction, including pool reuse via the placement new at
  `src/core/NotePlayHandle.cpp:748`).
- `src/core/NotePlayHandle.cpp:716` — `std::atomic<std::uint64_t> NotePlayHandle::s_generationCounter{0};`.
- `include/RackZoneInputs.h:50` — `RackZoneNote::generation`.
- `include/RackZoneInputs.h:67` — `RackZoneVoice::lastGeneration`.
- `src/core/RackZoneInputs.cpp:109` — `voiceFor` restarts only when
  `note.generation != voice.lastGeneration` (both the owner-match and the claim paths
  record the generation).
- `src/core/RackZoneFeed.cpp:94` — `noteOf` copies `nph->generation()` into the note.
- Regression: `tests/src/core/RackZoneRoutingTest.cpp` —
  `aReusedHandleRestartsTheVoice` (reuse with equal/greater frame counts, now keyed on
  generation) and `aContinuingNoteSurvivesTempoResizeReset` (continuing note after a
  tempo-resize frame reset must resume, not restart).

## Commands and EXIT codes
All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-192.r2/logs`.

Build (GREEN tree):
`cmake --build build --target zene RackZoneRoutingTest -j12` → **EXIT=0** (build-2.log)

GREEN test (fix applied):
`cd build/tests && QT_QPA_PLATFORM=offscreen /usr/bin/ctest -R '^RackZoneRoutingTest$' --output-on-failure` → **EXIT=0** (test-green.log); 1 test executable, all slots passed.

RED proof (only `src/core/RackZoneInputs.cpp` reverted to the round-1 framesPlayed
heuristic, generation plumbing kept so the test compiles):
`git diff -- src/core/RackZoneInputs.cpp > fix.patch && git apply -R fix.patch` → reverted.
Build → **EXIT=0** (build-red.log); test → **EXIT=8** (test-red.log).
Failing assertion (test-red.log): `continuing note restarted at 1, expected resume 257`.
Restored with `git apply fix.patch`, rebuilt → **EXIT=0** (build-green.log), test → **EXIT=0**.

Gates:
`bash tests/complexity-gate.sh --check` → **EXIT=1** (pre-existing baseline regressions
in ControlCommandsSession*.cpp, Vst3Host.cpp — not in any file changed here).
`bash tests/file-length-gate.sh --check` → **EXIT=1** (pre-existing: RackMacrosTest.cpp,
RevisionTimelineTest.cpp, VideoTrackTest.cpp, ArrangerTimelineTest.cpp — none changed here).
Neither gate lists `RackZoneInputs`, `NotePlayHandle`, or `RackZoneRoutingTest`; my
functions (`voiceFor`, `generation`) stay within CCN ≤ 10 and file-length limits.

## Summary
The bug (reused pooled handle resuming an old voice, plus the round-1 regression where a
continuing note restarted after a tempo-resize frame reset) is fixed by a generation
identifier instead of frame-count inference. The new regression fails on the prior
heuristic (EXIT=8) and passes with the fix (EXIT=0).

## Files committed
- include/NotePlayHandle.h
- include/RackZoneInputs.h
- src/core/NotePlayHandle.cpp
- src/core/RackZoneFeed.cpp
- src/core/RackZoneInputs.cpp
- tests/src/core/RackZoneRoutingTest.cpp
- tests/upstream-modifications.txt
