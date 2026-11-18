# Space Bunny fixer lane `core` — batch-04

Worktree: `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Six findings reviewed against the
current tree. One was fixed (the `RecordRingBuffer` overflow loop); the rest were classified without a
product change. Line numbers were re-located in the current tree rather than taken from the reports,
which were written against older revisions.

Verdicts: FIXED 1, SKIPPED-SCOPE 3, REAL-UNTESTED 1, FALSE-POSITIVE 1.

### Cluster 5248 — Oversized ring-buffer capacity can make construction loop forever

Verdict: FIXED
Evidence: `include/RecordRingBuffer.h:161` shifted `result <<= 1` with no overflow check, so a
capacity above the largest power of two a `size_t` holds wraps `result` to zero and the loop never
terminates — the constructor never returns. The report's claim is correct and genuinely unfixed: the
four sibling rings already carry exactly this guard (`include/SampleFrameRingBuffer.h:141`,
`include/RetroAudioRing.h:231`, `include/InputChannelRing.h:193` — the last added by `dd604aa40`, which
fixed `InputChannelRing` alone and left this ring behind). The fix applies the same condition and the
same `std::length_error` those siblings use, so the four rings stay uniform.

One note on the test: with the capacity written as a literal, GCC constant-folds the shift loop, the
fold stops at 2^63, and `std::vector`'s constructor throws a `length_error` of its own — the assertion
passes while the runtime path still spins forever. The regression reads the capacity through a
`volatile std::size_t` so the loop genuinely executes; without that the test is a false green.
Commit: ccc9513ca
Test: RecordRingBufferTest::refusesACapacityThatCannotBeRoundedUp
Red/green:
```
cmake --build build --target zene RecordRingBufferTest            -> EXIT=0
cd build/tests && timeout 20 ./RecordRingBufferTest refusesACapacityThatCannotBeRoundedUp
                                                                -> EXIT=124 (hang, no exit)
cd build/tests && ctest -R '^RecordRingBufferTest$' --output-on-failure   -> EXIT=0 (1 test run)
cd build/tests && ctest -R '^(RecordRingBufferTest|TwoTrackRecordingHarness|SampleClipWindowTest)$' --output-on-failure
                                                                -> EXIT=0 (3 tests run)
bash tests/complexity-gate.sh --check -> EXIT=0 ; bash tests/file-length-gate.sh --check -> EXIT=0
```
`TwoTrackRecordingHarness` and `SampleClipWindowTest` are the only other executables that include
`RecordRingBuffer.h` (`grep -rl RecordRingBuffer tests/CMakeLists.txt tests/src`), so all three were
rebuilt against the changed header and rerun.

### Cluster 5172 — Seqlock version check does not make the view copy race-safe

Verdict: SKIPPED-SCOPE
Evidence: `include/ModulationLayer.h:290-313` — `ModulationAudioView` is a deliberately trivial POD
guarded by `static_assert(std::is_trivially_copyable_v<ModulationAudioView>, "the audio thread's seqlock
copy must be plain bytes")` at `:314`, and `m_view` is published by `publishView` at `:412-420`. The
report is right that a seqlock version check is not in itself a C++ data-race remedy for a
non-atomic `m_view`. It is by design, of the same shape as `TempoMapPublisher`, and the check exists
precisely so the audio thread can copy plain bytes. Fixing it properly — immutable snapshots with
lifetime management, or atomic storage — means redesigning the audio-thread hand-off, which is
rule 6 (realtime path) and rule 7 territory, not a behaviour-preserving one-word fix.
Commit: none
Test: none
Red/green: none

### Cluster 5215 — Output-only patch wiring is reported as empty

Verdict: SKIPPED-SCOPE
Evidence: `include/PatchWiring.h:126` is `auto isEmpty() const -> bool { return m_edges.empty(); }` and
ignores `m_output` (`:124`), so a wiring with an output and no edges reports empty. The state is
reachable: `patcher.set_wiring` accepts `edges: []` with `output: "effect:0"` because
`validateWiring`/`wiringOutputExists` (`src/core/ControlCommandsPatcherEdit.cpp:248-302`) short-circuit on
the empty edge list. But making `isEmpty()` also require an unset output flips an existing control
command's observable result from silent-derive to typed refusal, which changes a command's result schema
— rule 8, and the parent's ControlCommandsSnapshot call, not a core fix.
Commit: none
Test: none
Red/green: none

### Cluster 5212 — Missing pattern index is confused with pattern zero

Verdict: SKIPPED-SCOPE
Evidence: `include/PatternTrack.h:77-80` returns `s_infoMap.value(this)`, documented at `:65-66` as
"This track's pattern number, or 0 if it has none" — so 0 is the documented "none" result and the
report's premise ("0 is a valid pattern index" used as "none") does not hold for this API. The exposure
is narrow: the read is GUI-side (`src/gui/tracks/PatternTrackView.cpp:58-74`) and only matters for
teardown ordering, where the surrounding comment shows the same hazard was already reasoned about. A
real fix changes the return type across roughly ten call sites — an API change well past the ~150-line
behaviour-preserving budget.
Commit: none
Test: none
Red/green: none

### Cluster 3393 — NaN period inputs can reach an invalid floating-to-frame conversion

Verdict: REAL-UNTESTED
Evidence: `include/PunchWindow.h:56-70` — the guard at `:62` is
`if (!songPlaying || framesPerTick <= 0.0 || punchEnd <= punchBegin)`, and `NaN <= 0.0` is false, so a
NaN `framesPerTick` or `periodStartFrame` survives it; `std::clamp` then returns NaN unchanged and
`static_cast<f_cnt_t>(NaN)` is undefined behaviour. The defect is real. A regression test was written
and it passed: on this toolchain GCC 13 (`-O2 -g -DNDEBUG -std=gnu++20`, no fast-math) folds the
floating-to-integer conversion of NaN to `0`, so the assertion could not be made to fail
deterministically. Per procedure no product change was made and the test was reverted in full
(`tests/src/core/PunchWindowTest.cpp` is byte-identical to HEAD; `git diff` on it is empty). The fix
the report asks for — an `std::isfinite` check on both inputs and the computed bounds — is a two-line
change and remains available for whoever can observe the UB.
Commit: none
Test: none (written, passed, reverted — it proved nothing)
Red/green: none

### Cluster 5242 — Remote audio code requires C++20 despite the C++17 target

Verdict: FALSE-POSITIVE
Evidence: `include/RemotePluginAudioPorts.h:82` declares
`template<AudioPortsSettings settings> class RemotePluginAudioPorts`, a C++20 class non-type template
parameter. The finding is correct that this needs C++20; its premise that the project targets C++17 is
not. The compile line for every test and library object in this tree carries `-std=gnu++20` (observed
directly on the `cc1plus` invocation), and rule 9 lists C++17 complaints as false by design. There is no
C++17 build for this to break, so the "fix" — rewriting the code for a standard the project does not
use — would be a downgrade, not a repair. `std::span` and `std::ranges::fill` are codebase-wide idioms
here, as recorded for the same class of finding in `batch-03.md` (cluster 4170).
Commit: none
Test: none
Red/green: none

Leftovers: none. The tree is clean; `tests/src/core/PunchWindowTest.cpp` is unmodified and every
`/tmp` probe file written while investigating cluster 3393 was removed.