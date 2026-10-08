# Bunny core lane — batch 13

Six findings, each read against the current tree before any code was touched.
Two are real and fixed with a regression test that failed first; three are false
positives, argued from the source line; one is real but out of this lane's scope.

### Cluster 5501 — Incomplete field searches match the entire library

Verdict: FIXED

Evidence: `BrowserSearch`'s constructor dropped a term whose field value was
empty (`tag:` parses to a term with a field and no value, which the term loop
discarded), so `parseExpression` left `m_groups` empty, and `matches()`
returned true for anything when there are no groups
(`src/core/BrowserSearch.cpp:155`) — and a bare `OR` did the same. So `tag:` or
`OR` matched the whole library. An expression that parses to no usable term now
falls back to the pre-WF-003 rule (a plain substring search of the words); only a
genuinely empty query matches everything. The term loop moved into a new private
`parseExpression(const QStringList&)` (`include/BrowserSearch.h:78`) because the
constructor reached CCN 11 once the fallback was added.

Commit: b551f1285
Test: LibraryProjectsTest::anIncompleteExpressionDoesNotMatchTheWholeLibrary
Red: `cmake --build build --target zene LibraryProjectsTest` → EXIT=0, then
`cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure` → EXIT=8
(`FAIL!` on `!BrowserSearch("tag:").matches(kick)` at
`tests/src/core/LibraryProjectsTest.cpp:123` — the kick sample did match, which is
the reported defect)
Green: `cmake --build build --target zene LibraryProjectsTest HelpAccessTest
LibraryMenusTest` → EXIT=0, then
`cd build/tests && ctest -R '^(LibraryProjectsTest|HelpAccessTest|LibraryMenusTest)$' --output-on-failure`
→ EXIT=0 (3/3 passed)

### Cluster 5568 — Slip readers can play beyond the clip's trimmed source window

Verdict: FALSE-POSITIVE

Evidence: the report reads `CompSlipReaders::render` as playing "from the
underlying sample without a source-window end bound"
(`src/core/CompSlip.cpp:99`). The bound is not on the reader, it is on the
sample: `Sample::render` returns silence for a `Loop::Off` read once
`m_frameIndex >= m_endFrame` (`src/core/Sample.cpp:161`), and
`SampleClip::installWindow` mirrors the clip's authored window into that field —
`m_sample.setEndFrame(static_cast<int>(m_window.sourceOut))`
(`src/core/SampleClip.cpp:491`) — on the very `Sample` object the slip readers are
played from. A slipped read past the outpoint therefore reads a frame index at or
beyond `m_endFrame` and comes back silent, which is the documented contract
("a read past the take's window is silence", `include/CompSlip.h:53`). Confirmed
empirically: a take whose window is the first half of a longer sample, comped so
its second half reads a quarter late, profiled `0.5000` inside the window and
`0.0000` past the outpoint — no leak. That probe was a throwaway, not a
regression worth keeping (it asserts a bound the sample already enforces).

Commit: none
Test: none
Red/green: none

### Cluster 5039 — The alternate signal stack is installed only on the calling thread

Verdict: SKIPPED-SCOPE

Evidence: the finding is correct — `install()` calls `sigaltstack()` once, on
the thread that calls it (`src/core/CrashReporter.cpp:252`), against a single
static stack buffer, while dispositions are process-wide and the handlers are
installed with `SA_ONSTACK` (`src/core/CrashReporter.cpp:260`); `sigaltstack` is
a per-thread attribute, so another thread (the audio thread above all) runs its
handler with `SA_ONSTACK` set and no alternate stack. But the fix is "register an
alternate stack on each thread that may need crash handling", which means
touching thread creation and the audio-thread bootstrap, not a core-model file:
a per-thread `sigaltstack` registration across the audio and other threads is a
design/platform-level change well outside this lane's scope. The limitation is
already stated in the source at `src/core/CrashReporter.cpp:93-94`. No code
changed.

Commit: none
Test: none
Red/green: none

### Cluster 5041 — A new crash can remain marked as already offered

Verdict: FIXED

Evidence: `writeReportIfIdle` truncated and rewrote the fixed report file but
left the offered marker in place, so after a report was acknowledged
(`acknowledgePendingReport` writes the marker, `src/core/CrashReporter.cpp:396-399`)
a later crash overwrote the report while the marker survived; on the next launch
`hasPendingReport()` saw the marker and reported the new crash as already
offered. `writeReportIfIdle` now clears the marker through the already-open crash
report directory fd with `::unlinkat(subFd, kOfferedMarkerName, 0)`
(`src/core/CrashReporter.cpp:438`) before it opens the report — `unlinkat` is
async-signal-safe, like everything else in the handler.

Commit: 33ca4263f
Test: CrashReporterTest::aNewReportIsOfferedAgainAfterAcknowledgement
Red: `cmake --build build --target zene CrashReporterTest` → EXIT=0, then
`cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure` → EXIT=8
(`FAIL!` on `hasPendingReport()` returning FALSE after the second report, at
`tests/src/core/CrashReporterTest.cpp:178`)
Green: `cmake --build build --target zene CrashReporterTest CrashReporterArmTest
SafeStartTest` → EXIT=0, then
`cd build/tests && ctest -R '^(CrashReporterTest|CrashReporterArmTest|SafeStartTest|ControlCrashReporter)$' --output-on-failure`
→ EXIT=0 (4/4 passed)

### Cluster 3163 — Edit locks refuse reads in mutating command groups

Verdict: FALSE-POSITIVE

Evidence: `editsContent` does classify whole command groups as edits
(`src/core/EditLocks.cpp:47-56`), and it does so before `refuses` looks at
anything — so the finding's description of the code is accurate. But
`EditLocks::refuses` is never consulted for a read. The registry gates it on the
command's own metadata: `lockedOut` returns early unless `mutating`
(`src/core/ControlRegistry.cpp:306-308`) and the preflight calls it only for a
mutating command (`src/core/ControlRegistry.cpp:320`). `mutating` defaults to
false (`include/ControlRegistry.h:97`) and the read commands in those very groups
set it false explicitly — `warp.list` (`src/core/ControlCommandsWarp.cpp:330`),
`groove.list` (`src/core/ControlCommandsGroove.cpp:238`), `chord.get_state`
(`src/core/ControlCommandsChord.cpp:204`), `clip.select`
(`src/core/ControlCommandsClip.cpp:467`, whose comment states the reasoning
explicitly). So `clip.get_state` naming a locked clip is never refused: the
registry reaches its handler and returns state.

Commit: none
Test: none
Red/green: none

### Cluster 5049 — Untrusted clip positions can overflow during lock checks

Verdict: FALSE-POSITIVE

Evidence: the unchecked cast is real — `moveRefused` does
`static_cast<tick_t>(args.value("position").toDouble())` and then
`start + ref.clip->length().getTicks()`
(`src/core/EditLocks.cpp:105-106`) — but `moveRefused` cannot be reached with an
out-of-range position. `clip.move`'s args schema bounds `position` with
`control::integerProperty(0, MaxSongLength)`
(`src/core/ControlCommandsClip.cpp:168`), and the registry runs `validateArgs`
(`src/core/ControlRegistry.cpp:251`) before it reaches the lock preflight
(`src/core/ControlRegistry.cpp:320`), returning `InvalidArgs` on anything the
schema rejects. `tick_t` is `int32_t`, and `MaxSongLength` is well inside it, so
by the time the cast runs the value is a validated in-range integer; `toDouble`
cannot yield an out-of-range double either. The remaining addition is bounded
input plus a bounded clip length, so no undefined behavior and no missed locked
clip. Validating twice is redundant, not unsafe.

Commit: none
Test: none
Red/green: none

Leftovers: none. The 5568 slip-window probe built while checking that finding was
removed and `TakeLaneCompPlaybackTest` rebuilt and rerun green (EXIT=0) with the
tree clean; nothing is half-done.
