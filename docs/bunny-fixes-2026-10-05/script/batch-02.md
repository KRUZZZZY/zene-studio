# Space Bunny fixer lane `script` — batch 02

Worktree `zene-bunny-script`, branch `bugs/bunny-script-2026-10-05`. Six findings:
2 FIXED (3 defects), 1 FALSE-POSITIVE, 1 SKIPPED-SCOPE, 1 REAL-UNTESTED.

### Cluster 5329 — saveProject runs on the script worker and can save stale state

Verdict: FIXED

Evidence: `src/core/ScriptBindings.cpp:1026` — `LuaSong::saveProject` called
`song()->saveProjectFile(resolved)` straight from the script worker, so a write the
script had already queued (`song.setTempo(150)`) was still sitting in the apply-side
queue when the file was written. `Song::saveProjectFile` serialises the tempo from the
model (`Song.cpp:1934`), so the file carried the OLD value. The same stale-read class
was already fixed for the getters in `a5de51c1b`, which added
`ScriptEngine::flushCommandsForRead()`; `saveProject` simply never called it.

Commit: `bcfb36701`

Test: `ScriptBindingsTest::saveProjectSeesTheScriptsOwnQueuedWrites`

Red/green:
- red: `cmake --build build --target zene ScriptBindingsTest` EXIT=0;
  `cd build/tests && ctest -R '^ScriptBindingsTest$' --output-on-failure` EXIT=8 —
  `'xml.contains(QStringLiteral("bpm=\"150\""))' returned FALSE`, `midiWrappers` PASS,
  25 passed / 1 failed.
- green: same build EXIT=0; same ctest EXIT=0 — 100% tests passed, 0 failed out of 1.
- no regressions: `ctest -R '^(ScriptBindingsTest|ScriptClockTest|ControlRegistryTest|ControlLivecodeCommands)$'` EXIT=0.

### Cluster 5327 — ProjectFile.exists throws for a missing in-project file

Verdict: FALSE-POSITIVE

Evidence: the mechanism is real (`src/core/ScriptBindings.cpp:1049` resolves with
`forWrite = false`, and `src/core/ScriptEngine.cpp:807` returns empty for a path that
does not exist, so `exists()` raises) but it is the DOCUMENTED, test-pinned contract,
not a defect: `tests/src/core/ScriptBindingsTest.cpp:651-656` asserts the throw and the
"does not exist" wording, with a comment stating that a missing file "is reported as an
error rather than as false". `exists()` exists to ask about a file the sandbox already
holds; absence is reported by the same typed error that `read()` raises. Changing this
would mean rewriting a passing assertion, which the lane rules forbid.

Commit: none

Test: none (existing `ScriptBindingsTest::projectFileWrapper` pins current behaviour)

Red/green: none

### Cluster 5328 — A failed ProjectFile.write can destroy the previous file

Verdict: REAL-UNTESTED

Evidence: real as described — `src/core/ScriptBindings.cpp:1082` opens with
`QIODevice::Truncate`, so the old contents are gone the moment the open succeeds; a
short write or failed flush at line 1087 then returns false with a partial file left
behind. The suggested `QSaveFile` fix is sound, but I could not build a deterministic
headless test for it: provoking a short write needs a full disk or a failing device,
which this environment cannot produce, and a successful `QSaveFile` path is
indistinguishable from the current code. Per the lane procedure, a finding with no
deterministic failing test gets no product change.

Commit: none

Test: none

Red/green: none

### Cluster 4433 — ProjectFile.write reports success after a partial write

Verdict: FALSE-POSITIVE

Evidence: the current code already does what the finding asks for. `src/core/ScriptBindings.cpp:1087`
reads `return file.write(bytes) == bytes.size() && file.flush();` — it compares the
full byte count AND ANDs the flush result, so a short write or a failed flush returns
false. The reported code ("any nonnegative write count is treated as success") is from
an older revision and is not present in this tree.

Commit: none

Test: none

Red/green: none

### Cluster 4438 — Adding a schedule during playback can fire a false transport edge

Verdict: FIXED

Evidence: `ScriptClockGrid` started unprimed (`include/ScriptClock.h`: `m_playing = false`,
`m_pos = 0`), and `src/core/ScriptClock.cpp:197` `syncTimer` started the poll timer
without anchoring the grid to the live transport. So a transport schedule added to a song
that was already rolling saw its first poll read "stopped -> playing" at
`src/core/ScriptClock.cpp:126` and fired a play edge that never happened.

Commit: `3e720e32a`

Test: `ScriptClockTest::aScheduleAddedDuringPlaybackDoesNotFireAPlayEdge`

Red/green:
- red: `cmake --build build --target zene ScriptClockTest` EXIT=0;
  `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` EXIT=8 —
  `Actual (hookRuns(clock) - before): 1, Expected: 0`.
- green: same build EXIT=0; same ctest EXIT=0 — 100% tests passed, 0 failed out of 1.

### Cluster 5340 — Restarting the scheduler can fire boundaries from while it was stopped

Verdict: FIXED

Evidence: same root cause, and the fix is the same `ScriptClockGrid::prime()` seam.
`src/core/ScriptClock.cpp:208` stopped the timer when the schedule list emptied but left
the grid anchored where the song had been, so a later schedule resumed polling from that
stale position and fired up to `ScriptClockGrid::CrossCap` (4) boundaries crossed while
nothing was scheduled.

Commit: `3e720e32a`

Test: `ScriptClockTest::resumingAfterAnEmptyPeriodDoesNotFireStaleBoundaries`

Red/green:
- red: build EXIT=0; `ctest -R '^ScriptClockTest$' --output-on-failure` EXIT=8 —
  `Actual (hookRuns(clock) - before): 4, Expected: 0` (exactly the CrossCap).
- green: build EXIT=0; `ctest -R '^ScriptClockTest$' --output-on-failure` EXIT=0.

The fix adds `ScriptClockGrid::prime()` (seeds the grid from the transport, emitting no
edge or crossing) and `ScriptClock::primeGrid()`, called from `syncTimer` whenever the
timer starts or resumes. One commit covers both clusters because they are one defect.

Gates before that commit: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.
No other suite regressed: `ctest -R '^(ScriptClockTest|ControlRegistryTest|ScriptBindingsTest|ControlLivecodeCommands)$'` EXIT=0, 4/4 passed.

Leftovers: cluster 5328 is real and still unfixed — it needs a `QSaveFile` write with a
deterministic short-write test, which this headless environment cannot produce; no
product code was changed for it. No test file was added, so `tests/CMakeLists.txt` and
`tests/fork-sources.txt` needed no edits.