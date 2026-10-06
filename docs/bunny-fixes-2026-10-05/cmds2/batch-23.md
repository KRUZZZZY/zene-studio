# cmds2 batch-23 — warp.* command group

Lane: `cmds2`. Branch `bugs/bunny-cmds2-2026-10-05`, starting at `548aa2fde`.
6 findings: **1 FIXED**, **5 FALSE-POSITIVE**, 0 skipped.

### Cluster 3002 — A non-array `markers` value clears the warp map

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsWarpEdit.cpp:483` declares `markers` with
`markersProperty()`, which is `{"type":"array", ...}` (`src/core/ControlCommandsWarp.cpp:288-292`).
`ControlRegistry::invoke` runs `validateArgs` against that schema BEFORE dispatching to the
handler (`src/core/ControlRegistry.cpp:250-255`), and `checkType` rejects any non-array for
type `array` (`src/core/ControlSchema.cpp:56, 66-82`). So `markers: "bad"` is refused as
`invalid_args` at the gate; `readWarpSetArguments`' `toArray()` at
`ControlCommandsWarpEdit.cpp:109` is never reached with a wrong-typed value.

Commit: none

Test: none

Red/green: none

### Cluster 4946 — Fractional source-frame addresses can target the wrong warp marker

Verdict: FALSE-POSITIVE

Evidence: `source_frame` is declared `integerProperty(0, MaxSchemaInteger)` for both
`warp.move` and `warp.remove` (`src/core/ControlCommandsWarpEdit.cpp:438, 459`), and the
validator's integer rule is `std::floor(value) == value`
(`src/core/ControlSchema.cpp:50-53`) with `checkRange` enforcing the bounds
(`:84-99`). `source_frame: 12.9` therefore fails validation in
`ControlRegistry::invoke` (`:250-255`) and never reaches `addressMarker`'s
`static_cast<f_cnt_t>` at `ControlCommandsWarpEdit.cpp:158`.

Commit: none

Test: none

Red/green: none

### Cluster 4029 — Warp edits do not mark the project modified

Verdict: FALSE-POSITIVE

Evidence: every warp mutator reaches the engine's own setters, which mark the song
themselves: `applyMarkers` calls `clip->setWarpMarkers` (`src/core/ControlCommandsWarp.cpp:237`),
which calls `Engine::getSong()->setModified()` (`src/core/SampleClip.cpp:361`), and its
empty-list path calls `clearWarpMarkers()`, which does the same (`:369-370`).
`setWarpTempoMode` (`:380`), `setSourceTempo` (`:389`) and `setWarpStretchMode` (`:399`)
mark it too, so `warp.set` and `warp.stretch` are covered as well — `warp.quantize` goes
through `applyMarkers` like every other mutator.

Commit: none

Test: none

Red/green: none

### Cluster 3004 — `warp.move` can overflow while converting its requested offset

Verdict: FALSE-POSITIVE

Evidence: `offset_ticks` is declared by `offsetTicksProperty()` =
`integerProperty(-MaxSchemaInteger, MaxSchemaInteger)`
(`src/core/ControlCommandsWarp.cpp:294-300`), and `MaxSchemaInteger` is `2147483647`
(`include/ControlWarpSupport.h:55`) — exactly `tick_t`'s maximum, since
`tick_t` is `std::int32_t` (`include/LmmsTypes.h:35`). The validator rejects a
non-integral value (`ControlSchema.cpp:50-53`) and anything above the maximum
(`:93-97`), so `offset_ticks: 2147483648` and a fractional offset are both refused as
`invalid_args` before the cast at `ControlCommandsWarpEdit.cpp:222`. The schema bound is
in step with the destination type, so there is no overflow to guard against.

Commit: none

Test: none

Red/green: none

### Cluster 1262 — Zero-strength quantization can replace and alter an existing warp map

Verdict: FALSE-POSITIVE

Evidence: replacing the map is the command's declared contract, not an accident: the
registered description states "the new markers replace the old ones (at most 128, in order)"
(`src/core/ControlCommandsWarpQuantize.cpp:104`) and the result reports the new marker set
(`:88-89`). `strength` is documented as how far each transient moves TOWARD the grid, not as
whether the map is built (`:102-104`), and the existing case pins the strength-0 outcome —
anchors at the bursts, not the grid (`tests/src/core/WarpQuantizeTest.cpp:111-117`).
Making strength 0 a no-op would change documented, tested behaviour, which is a design
decision and not a defect fix.

Commit: none

Test: none

Red/green: none

### Cluster 3007 — Quantizing drops valid negative offsets

Verdict: FIXED

Evidence: `quantisedMarkers` seeded `tick_t last = -1` as its "nothing emitted yet"
sentinel (`src/core/ControlCommandsWarpQuantize.cpp:55`), but a marker offset is signed —
`offsetTicksProperty()` documents that explicitly ("the engine's rule is 'strictly
increasing', not 'non-negative'", `src/core/ControlCommandsWarp.cpp:296-299`). Any transient
at or before tick -1 was skipped by `if (moved <= last)`, so a clip whose existing map pins
its audio before its own origin returned `not_found` "no transient found in the clip's audio"
although the detector had found every transient. Replaced the tick-valued sentinel with an
explicit `emitted` flag; the monotonicity rule is unchanged.

Commit: 4f698d02c

Test: `WarpQuantizeTest::negativeRelativeTicksAreStillEmitted`

Red/green:
  red:   `cmake --build build --target zene WarpQuantizeTest` # EXIT=0
         `ctest -R '^WarpQuantizeTest$' --output-on-failure` # EXIT=8
         `FAIL! : WarpQuantizeTest::negativeRelativeTicksAreStillEmitted() 'done.ok' returned FALSE. (no transient found in the clip's audio)`
  green: `cmake --build build --target zene WarpQuantizeTest` # EXIT=0
         `ctest -R '^WarpQuantizeTest$' --output-on-failure` # EXIT=0
         `ctest -R '^(WarpQuantizeTest|ControlWarpCommandsTest|WarpMarkersTest|ClipWarpPersistenceTest|SampleClipWarpTest)$' --output-on-failure` # EXIT=0
  gates: `bash tests/complexity-gate.sh --check` # EXIT=0, `bash tests/file-length-gate.sh --check` # EXIT=0

Leftovers: none — Cluster 1262 was closed as documented behaviour rather than a defect, so a
strength-0 no-op (or map preservation) remains an open design question for the owner, not
outstanding work in this lane.