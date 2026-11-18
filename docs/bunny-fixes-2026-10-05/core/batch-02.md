# Space Bunny fixer lane `core` — batch-02

Worktree: `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Six findings reviewed against the
current tree. Two product files changed (`include/ExportDitherMode.h`, `include/InputChannelRing.h`),
one new test file added.

Verdicts: FIXED 2, DUPLICATE 1, ALREADY-FIXED 1, FALSE-POSITIVE 2.

### Cluster 3153 — The dither-mode parser crashes for a null output pointer

Verdict: FIXED
Evidence: `include/ExportDitherMode.h:75-82` (pre-fix) — `ditherModeFromName` stored through
`*mode` at `:80` with no check, so `ditherModeFromName("tpdf", nullptr)` dereferenced null. The
sibling `srcQualityFromName` does check its out pointer
(`src/core/ExportRenderSettings.cpp:110`, declared `noexcept` at `include/SrcQuality.h:64`). The
existing regression case lived in `tests/src/core/ExportNoiseShaperTest.cpp:185`
(`theBoolApiMapsOntoTheModes`), which already covered a bad name but never a null pointer; the null
cases were added there.
Commit: 332369cd8
Test: ExportNoiseShaperTest::theBoolApiMapsOntoTheModes
Red/green:
Red:   `cmake --build build --target zene ExportNoiseShaperTest` EXIT=0;
       `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` EXIT=8 (SEGFAULT, stack frame #4
       `lmms::ditherModeFromName`, frame #5 `theBoolApiMapsOntoTheModes`)
Green: `cmake --build build --target zene ExportNoiseShaperTest` EXIT=0;
       `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` EXIT=0 (1/1 passed)

### Cluster 5099 — New DSP code requires C++20 APIs in a C++17 project

Verdict: FALSE-POSITIVE
Evidence: `src/CMakeLists.txt:24-25` — the comment "# Enable C++20" and
`SET(CMAKE_CXX_STANDARD 20)`; the root `CMakeLists.txt:319` sets `CMAKE_CXX_STANDARD_REQUIRED ON`,
and every test target pins `cxx_std_20` (e.g. `tests/CMakeLists.txt:980`). `include/GrooveTemplate.h:29`
includes `<span>`, which is available under the standard the project actually compiles with. The
report's premise (a C++17 project) does not hold in this tree, and this class of complaint is called
out in the lane rules as a known false report.
Commit: none
Test: none
Red/green: none

### Cluster 4160 — New detection and groove code requires C++20

Verdict: FALSE-POSITIVE
Evidence: same evidence as cluster 5099: `src/CMakeLists.txt:25` sets C++20 and
`tests/CMakeLists.txt:980` requires `cxx_std_20` for every LMMS test. `include/ImportDetectionDsp.h:52`
includes `<span>`, and `tests/src/core/ImportDetectionTest.cpp` covers the translation unit, so the
code compiles and is tested under the real standard.
Commit: none
Test: none
Red/green: none

### Cluster 3185 — Huge ring capacities can hang or under-allocate storage

Verdict: FIXED
Evidence: `include/InputChannelRing.h:191-196` (pre-fix) — `nextPowerOfTwo` shifted until
`result >= value`, which wraps to zero and never terminates past the largest power of two a size_t
holds, and `m_data(m_capacity * static_cast<std::size_t>(m_maxChannels))` at `:83` could wrap the
storage product. The neighbouring ring already guards the first case:
`include/SampleFrameRingBuffer.h:141-151` throws `std::length_error` for an unroutable capacity; this
ring now matches, and `storageSize()` adds the product check that SampleFrameRingBuffer does not need
(one element per frame there, `channels` here). Both guards run in the constructor, which the class
comment (`include/InputChannelRing.h:52-53`) already requires to run off the audio and capture
threads — no audio path, allocation or lock was introduced.
Commit: dd604aa40
Test: InputChannelRingTest::refusesACapacityThatCannotBeRoundedUp,
InputChannelRingTest::refusesAStorageSizeThatOverflows (new file, registered in
`tests/CMakeLists.txt` LMMS_TESTS and `tests/fork-sources.txt`)
Red/green:
Red:   `cmake --build build --target InputChannelRingTest` EXIT=0;
       `./InputChannelRingTest refusesAStorageSizeThatOverflows` EXIT=1 ("Expected exception of type
       std::length_error to be thrown but no exception caught");
       `timeout 10 ./InputChannelRingTest refusesACapacityThatCannotBeRoundedUp` EXIT=124 (hung, the
       pre-fix shift loop — run under `timeout` because a hang cannot be observed otherwise)
Green: `cmake --build build --target zene InputChannelRingTest` EXIT=0;
       `ctest -R '^InputChannelRingTest$' --output-on-failure` EXIT=0 (1/1 passed);
       neighbours `ctest -R '^(RecordRingBufferTest|CapturePublisherTest)$' --output-on-failure`
       EXIT=0 (2/2 passed)

### Cluster 2263 — Oversized ring capacity can make construction hang

Verdict: DUPLICATE
Evidence: the same defect and the same lines as cluster 3185 —
`include/InputChannelRing.h:191-196` (pre-fix) held the shift loop that wraps to zero, and the
`SIZE_MAX` construction described here is exactly what
`InputChannelRingTest::refusesACapacityThatCannotBeRoundedUp` pins. Both clusters were fixed by the
single commit dd604aa40; no second change was made.
Commit: dd604aa40 (same commit as cluster 3185)
Test: InputChannelRingTest::refusesACapacityThatCannotBeRoundedUp
Red/green: same commands as cluster 3185 (`timeout 10 ./InputChannelRingTest
refusesACapacityThatCannotBeRoundedUp` EXIT=124 red; `ctest -R '^InputChannelRingTest$'` EXIT=0 green)

### Cluster 5079 — Retuning walks audio-owned handles without synchronization

Verdict: ALREADY-FIXED
Evidence: `include/InstrumentTrack.h:125-135` (`retunePlayingNotes`) walks `m_notes` and
`m_processHandles`, and its only caller in the tree, `src/core/SessionTuning.cpp:303`, runs inside
the engine's model-change lock taken at `src/core/SessionTuning.cpp:291`
(`Engine::audioEngine()->requestChangeInModel()`) and released at `:306` (`doneChangeInModel()`),
with a comment at `:288-290` naming `InstrumentTrack::updateBaseNote()` as the same precedent. The
method's own comment (`include/InstrumentTrack.h:116-124`) states the stores are plain bools by
design, picked up by `NotePlayHandle::play`. Handing the walk to the audio thread instead would mean
adding synchronization or deferred work on a realtime path, which rule 6 puts out of scope.
Commit: none
Test: none
Red/green: none

Leftovers: none. Both fixes are committed with regression tests, the three unresolved clusters were
resolved from source evidence without product changes, and `git status --short` is clean.

## Rework correction (2026-10-08)

Source: `25023e49a` (REWORK in `review-core.md`, blocking finding 1 and item 2).

- The claim that the new test file was "registered in `tests/fork-sources.txt`" was wrong at `dd604aa40`:
  the entry named `src/core/InputChannelRingTest.cpp`, which does not exist. The real file is
  `tests/src/core/InputChannelRingTest.cpp`, which `tests/CMakeLists.txt` already names. Corrected in
  `29b80e2ff` (`fix(rework dd604aa40)`). Check at HEAD before the fix: that path is missing; after: every
  fork-sources entry exists.
- The constructor guards themselves (`refusesACapacityThatCannotBeRoundedUp`,
  `refusesAStorageSizeThatOverflows`) were not changed by the rework.
