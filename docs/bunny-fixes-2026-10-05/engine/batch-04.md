# Engine lane — batch 04 (2026-10-05)

Branch `bugs/bunny-engine-2026-10-05`. Six hypotheses: three REAL and fixed,
one REAL but left untestable headlessly, two FALSE-POSITIVE. No finding was
skipped for scope and no gate, tolerance or assertion was weakened.

### Cluster 2706 — C++17 build uses `std::span`

Verdict: FALSE-POSITIVE

Evidence: the project sets C++20 explicitly at `src/CMakeLists.txt:25`
(`SET(CMAKE_CXX_STANDARD 20)`) with `CMAKE_CXX_STANDARD_REQUIRED ON` at
`CMakeLists.txt:319`. The `<span>` include and `std::span` uses at
`src/core/Clip.cpp:30` and `src/core/ClipConsolidate.cpp:29` are therefore
correct for this tree, and both targets build clean. This is exactly the
"the project requires C++20, C++17 complaints are false" case.

Commit: none

Test: none

Red/green: none (no build failure reproduced; `cmake --build build --target zene`
EXIT=0 throughout this batch).

### Cluster 5571 — Consolidation omits clips that overlap the range start

Verdict: FIXED

Evidence: `src/core/ClipConsolidate.cpp:63` (pre-fix) computed the mix offset
from the clip's own start, `at = (from - start) * framesPerTick`. For a clip
starting before the range start that value is negative, and casting it to the
unsigned `f_cnt_t` (`include/LmmsTypes.h:43`) produced a huge number, so
`room` at line 66 collapsed to 0 and the clip rendered silence. Note the
`clip.consolidate` command path never reaches this: `sortIntoRegion`
(`src/core/ControlCommandsConsolidate.cpp:104-107`) refuses a clip crossing a
region edge first — which is why this sat undetected. `ClipConsolidate::render`
is exported and is also called directly from
`src/core/ControlCommandsLibraryClips.cpp:140`, so the library underneath was
genuinely wrong. `mixClip` now starts at `begin = std::max(from, start)` and
derives the source window from that tick (`src/core/ClipConsolidate.cpp:59-67`),
which is exactly the fix the report proposed.

Commit: 8d919de1e

Test: ClipConsolidateTest::aClipOverlappingTheRangeStartIsRenderedFromWhereTheRangeBegins

Red/green: `cmake --build build --target ClipConsolidateTest` EXIT=0, then
`cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure`
EXIT=8 — `FAIL! ... 'the part of the clip inside the range is missing from the
rendered file'`. After the fix, `cmake --build build --target zene
ClipConsolidateTest` EXIT=0 and `ctest -R '^ClipConsolidateTest$'
--output-on-failure` EXIT=0 (all 7 cases, including the pre-existing
`refusalsChangeNothing`).

### Cluster 1876 — A failed WAV write can delete an existing destination

Verdict: REAL-UNTESTED

Evidence: `src/core/ClipConsolidate.cpp:94-115`. `sf_open(..., SFM_WRITE, ...)`
truncates an existing destination at open time, and if the subsequent
`sf_writef_float` short-writes or `sf_close` fails, line 112 `QFile::remove(path)`
deletes the path — destroying the previous file as well as the failed output.
The call is real: `sf_open` with `SFM_WRITE` truncates, and `uniqueOutPath`
(`src/core/ControlCommandsConsolidate.cpp:147-166`) only guarantees a fresh name
for the *default* path, so an explicit `out` argument naming an existing WAV —
which `ControlCommandsLibraryClips` passes for clip exports — can be destroyed.
I did not change the code: forcing a deterministic mid-write failure
(sf_writef_float short write or sf_close failure) headlessly needs a fault
injection into libsndfile or a full filesystem, and neither is available in
this environment, so there is no RED to anchor a fix to. The fix (write to a
temporary path, `QFile::rename` onto the destination only after a successful
close) mirrors what `writeClip` already does for MIDI at
`src/core/ControlCommandsLibraryClips.cpp:145-153` and is left for a run with
a way to make the write fail.

Commit: none

Test: none

Red/green: none (no deterministic failing test could be constructed headlessly).

### Cluster 4675 — Invalid envelope ticks silently become tick zero

Verdict: FIXED

Evidence: `src/core/ClipEdits.cpp:135` (pre-fix) called
`part.left(colon).toInt()` without an ok flag, so `toInt()`'s zero-on-failure
put a malformed tick at tick 0 — `gainenv="oops:-24"` became a -24 dB point at
the clip's start and the clip played 24 dB down. The dB field was already
guarded (`!std::isfinite(db)`), only the tick was not. The parse now checks
conversion success for the tick (`src/core/ClipEdits.cpp:131-137`); the
non-finite dB guard and the `std::max(0, ...)` clamp are unchanged.

Commit: 35a0f5102

Test: ClipGainEnvelopeTest::aMalformedTickIsDroppedInsteadOfLandingAtTickZero

Red/green: `cmake --build build --target zene ClipGainEnvelopeTest` EXIT=0, then
`cd build/tests && ctest -R '^ClipGainEnvelopeTest$' --output-on-failure`
EXIT=8 — `Actual (edits.envelopeCount): 2 / Expected (1)`. After the fix,
`cmake --build build --target zene ClipGainEnvelopeTest` EXIT=0 and
`ctest -R '^ClipGainEnvelopeTest$' --output-on-failure` EXIT=0, plus the other
tests covering `ClipEdits.cpp`: `ClipEditsTest`, `ClipFadesRenderTest`,
`ClipTransposeTest`, `AudioEditingTest` EXIT=0.

### Cluster 5574 — Pre-fade envelope interpolation is linear in amplitude

Verdict: FALSE-POSITIVE

Evidence: `src/core/ClipPreFades.cpp:140-155` does interpolate the endpoint
amplitudes across the block, but it is deliberately the same block-rate
evaluation as the ordinary path: `SamplePlayHandle::applyClipEdits`
(`src/core/SamplePlayHandle.cpp:329-345`) computes `from`/`to` the same way and
carries the comment "the gain envelope at block rate - its value at this block's
first and last frame, ramped linearly across it (two lookups per block, never
one per frame)". `applyPreFaded` is the pre-fade twin of that loop
(`applyClipEdits` dispatches to it at line 330), so the two paths agree by
design and the existing render test `aGainEnvelopeShapesTheClipAtBlockRate`
(`tests/src/core/ClipFadesRenderTest.cpp:354`) pins that behaviour. Changing it
would be a deliberate feature change to the audio path, not a defect fix, and
it would contradict the tested contract of the main path.

Commit: none

Test: none

Red/green: none (`ctest -R '^ClipFadesRenderTest$'` EXIT=0 as part of the
Cluster 4675 neighbour run; the block-rate contract holds).

### Cluster 3259 — Malformed controller point attributes become events at tick zero

Verdict: FIXED

Evidence: `src/core/MidiClipControllers.cpp:167` (pre-fix) read
`p.attribute("t").toInt()` and `p.attribute("v").toInt()` without ok flags, so
`t="bad"`, a missing attribute or a value beyond `int` all became a point at
tick 0 with value 0 — a jump in the curve at the clip's start. The lane already
had no coverage for this loader, so the new test file
`tests/src/core/MidiClipControllersLoadTest.cpp` was added and registered in
both `tests/CMakeLists.txt` (LMMS_TESTS) and `tests/fork-sources.txt`. The
loader now drops a point whose tick or value did not convert
(`src/core/MidiClipControllers.cpp:161-172`); the value still clamps to the
lane's range (a CC value of 200 still becomes 127).

Commit: cd83468aa

Test: MidiClipControllersLoadTest::aMalformedTickOrValueIsDroppedInsteadOfBecomingAPointAtZero

Red/green: `cmake --build build --target MidiClipControllersLoadTest` EXIT=0,
then `cd build/tests && ctest -R '^MidiClipControllersLoadTest$'
--output-on-failure` EXIT=8 — `Actual (lane->points.size()): 2 / Expected
(std::size_t{1})`. After the fix, `cmake --build build --target zene
MidiClipControllersLoadTest` EXIT=0 and `ctest -R
'^MidiClipControllersLoadTest$' --output-on-failure` EXIT=0 (both cases,
including the round-trip and clamp case), plus the other tests touching
controller lanes: `NoteEditingCommandsTest`, `RecordingPassTest`,
`UnclaimedElementsTest` EXIT=0.

Leftovers: cluster 1876 (a failed WAV write can delete the destination) is
confirmed real but unfixed — it needs a way to make libsndfile's write or close
fail headlessly, and the temp-file-then-rename fix should land with that RED.
The new test target also required a CMake regeneration to appear, which happened
implicitly via the `zene` build; no configure was run by hand.