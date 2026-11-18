# core batch-19 — Zene Studio fixer lane

Findings: clusters 5125, 5123, 5124, 4210, 5128, 5127. One fix, five not actioned.

### Cluster 5125 — Mastering reports measure the pre-encoded signal
Verdict: REAL-UNTESTED
Evidence: `src/core/MasteringJob.cpp:407` measures the in-memory float `frames` and
`src/core/MasteringJob.cpp:416` then encodes them at `m_outputSettings.getBitDepth()` via
`waveFormatFor()` (`src/core/MasteringJob.cpp:92-104`, 16/24-bit PCM or 32-bit float). The report
therefore describes the pre-quantization signal, as the finding says. No product code was changed:
a deterministic headless RED needs a signal whose true peak lands inside the 16-bit quantization
error (~-96 dBFS, one LSB) of a pass threshold, and the only headroom in the comparison is
`TruePeakSlackDb = 0.05f` (`src/core/MasteringJob.cpp:56`), so no candidate can be constructed that
flips a pass flag on quantization alone. Whether the report should describe the working buffer or
the delivered file is also a product decision, not a defect.
Commit: none
Test: none
Red/green: none

### Cluster 5123 — The MCU surface can remain banked past the last track
Verdict: FIXED
Evidence: `m_bank` was clamped only inside `setBank()` (`src/core/McuSurface.cpp:128-133`), so
`stripTrack()`'s `m_bank + strip` (now `src/core/McuSurface.cpp:152`) ran past the end of
`bankable()` after tracks were deleted and every strip resolved to `nullptr` until the user paged.
The bank is now clamped where the strips are resolved: `McuSurface::bankedFirst()`
(`src/core/McuSurface.cpp:140-146`, declared `include/McuSurface.h:91`), used by `stripTrack()`
(`src/core/McuSurface.cpp:152`) and by `bank()`/`state()`
(`src/core/McuSurface.cpp:137`, `src/core/McuSurface.cpp:309`) so every read agrees. `m_bank` itself keeps the value the user set, so paging
back reveals the same tracks as before.
Commit: df86a54db
Test: McuSurfaceTest::theBankFollowsTheTrackList
Red/green: RED `cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure` EXIT=8
(`'surface->stripTrack(0) != nullptr' returned FALSE`); GREEN same command EXIT=0, 7 passed.

### Cluster 5124 — MCU fader and pan changes bypass the undo journal
Verdict: FALSE-POSITIVE
Evidence: the behaviour is documented as the design at `include/McuSurface.h:52-54`: "What the
surface does is therefore what a click or an agent does - recorded and undone the same way -
except the faders and pots, which move their models directly as a MIDI controller does", and
`src/core/McuSurface.cpp:172-187` matches that text exactly — mute/solo/select/rec and the
transport buttons all go through `run()` and the registry (`src/core/McuSurface.cpp:204-235`), only
`Kind::Fader` and `Kind::VPot` touch the models. The finding's own fix (route them through
journal-aware commands and coalesce a gesture) is a design change to the journal, not a defect fix.
Commit: none
Test: none
Red/green: none

### Cluster 4210 — Metronome clicks allocate on the audio thread
Verdict: DUPLICATE (of cluster 5127)
Evidence: the cited lines (`src/core/Metronome.cpp:39`, `src/core/MetronomeOptions.cpp:150-154`) are
the one `click()` call path, the same per-click `new Sample` / `new SamplePlayHandle` reported as
cluster 5127. Verdict and reasoning are recorded there.
Commit: none
Test: none
Red/green: none

### Cluster 5128 — An early metronome click can read and decode files on its calling thread
Verdict: ALREADY-FIXED
Evidence: every sound is decoded once on the main thread into `s.buffers`
(`src/core/MetronomeOptions.cpp:95-99`), which `load()` fills before publishing `loaded`
(`src/core/MetronomeOptions.cpp:87-101`), and `click()` reads only that array
(`src/core/MetronomeOptions.cpp:155`). `load()` is reached eagerly at two registration points, not
at the first click: `src/core/ProjectSectionRegistry.cpp:65` and
`src/core/ControlCommandsRecordTools.cpp:175`. What remains is the defensive fallback at
`src/core/MetronomeOptions.cpp:156-159`, taken only for a click arriving before registration — no
in-tree caller can reach it, and removing it would trade a harmless fallback for a silent click.
Commit: none
Test: none
Red/green: none

### Cluster 5127 — Metronome clicks allocate play objects on the realtime path
Verdict: SKIPPED-SCOPE
Evidence: `click()` does `new Sample` and `new SamplePlayHandle` per click
(`src/core/MetronomeOptions.cpp:160-162`), called from the audio path at
`src/core/Metronome.cpp:39`. The fix the finding asks for — a reusable preallocated play-handle pool
— is a new realtime ownership/lifetime design on an audio-thread path (who frees an in-flight
handle, how a live pool entry is recycled, how the per-click level rides on a reused object), which
rule 6 places out of scope for this lane and which exceeds a behaviour-preserving defect fix.
Commit: none
Test: none
Red/green: none

Leftovers: nothing half-done. Cluster 5125 is left REAL-UNTESTED with product code untouched — it
needs a product decision on what the report describes and a threshold-sensitive fixture that cannot
be built deterministically today.