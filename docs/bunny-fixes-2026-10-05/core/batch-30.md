# Space Bunny fixer lane `core` — batch 30 (2026-10-05)

Six Luna findings across `RevisionTimelineCompare.cpp`, `RevisionTimelineGit.cpp` and
`RoutingGraph.cpp`. One fixed, one real-but-untestable, four rejected against the current
tree. Verdicts: FIXED 1, REAL-UNTESTED 1, FALSE-POSITIVE 3, ALREADY-FIXED 1.

### Cluster 4387 — Comparing a compressed revision can allocate far beyond the document cap

Verdict: ALREADY-FIXED

Evidence: `src/core/RevisionTimelineCompare.cpp:187` reads qCompress's 4-byte big-endian
header and refuses the document when the declared inflated size is over
`Bounds::MaxDocumentBytes` (64 MiB) — that check is *before* `qUncompress(raw)` at line 188,
not after it, and the comment at 185-186 records the reason ("qUncompress allocates that up
front"). Line 189 then re-checks the inflated result's size as a second line of defence. The
finding's "qUncompress runs before the 64 MiB check" describes a revision before this guard
existed; the allocation the report warns about is already bounded by the declared size.

Commit: none

Test: none (existing behaviour, no test change)

Red/green: none

### Cluster 5291 — Git operations can exceed the stated timeout

Verdict: REAL-UNTESTED

Evidence: `src/core/RevisionTimelineGit.cpp:74` gives `waitForStarted` a full
`Bounds::GitTimeoutMs` (2500 ms, `include/RevisionTimeline.h:79`) and line 80 gives
`waitForFinished` another full one, so a child that starts slowly and then runs slowly can
block the UI thread for close to twice the advertised bound. The report's reading of the code
is correct. No product code was changed: the doubling is only observable when *process
startup itself* consumes most of the timeout, which cannot be provoked deterministically
headlessly (a stub `git` that merely sleeps is killed by the second wait in both the old and
a deadline-tracking implementation, so the test would not fail before the fix). `runGit` is
also in an anonymous namespace and `gitExecutable()` caches `QStandardPaths::findExecutable`
in a function-local `static`, so no existing test target can inject a slow-starting child.
The fix is a `QDeadlineTimer`/elapsed-time remainder passed to the second wait; it is
recorded here for the parent rather than shipped untested.

Commit: none

Test: none

Red/green: none

### Cluster 2452 — Git revision reads enforce the size cap after buffering the whole blob

Verdict: ALREADY-FIXED

Evidence: `src/core/RevisionTimelineGit.cpp:240-243` runs `git cat-file -s <spec>` FIRST,
parses the declared blob size and applies `withinRevisionCap()` before any `cat-file blob`
child is started at line 244-246; the comment at 238-239 states the intent outright
("runGit() reads a child's whole output into memory, so the cap has to be applied before
`cat-file blob`, not to what it buffered"). Line 252 re-checks the buffered result. The
finding describes the pre-fix ordering; the current tree never buffers an over-cap blob.

Commit: none

Test: none (existing behaviour, no test change)

Red/green: none

### Cluster 4394 — Unwritten host-buffer channels and frames retain stale samples

Verdict: FALSE-POSITIVE

Evidence: `src/core/RoutingGraph.cpp:260-274` does copy only the `min(m_frames, frames)` ×
`min(m_channels, totalChannels())` overlap, and that is a deliberate, test-locked contract
rather than an oversight: `tests/src/core/RoutingGraphTest.cpp:597` `ProcessClampsToPreparedSize`
fills a 32-frame / 2-channel buffer with `-123.0f`, prepares a 16-frame / 1-channel graph, and
asserts that the extra frames and the extra channel still hold `-123.0f` afterwards, under the
comment "process() must write only the prepared frame/channel window ... everything outside the
prepared window must stay untouched." Production callers never present a wider destination:
`src/core/EffectChain.cpp:358-359` and `src/core/Rack.cpp:260-261` both gate on
`m_graph->frames() == bus.frames()` and prepare with `DEFAULT_CHANNELS` into a matching stereo
buffer. The report's "clear all unwritten channels" fix would break that existing test, which
rule 5 forbids. The residual `assumeNonSilent` pessimisation over host channels it mentions is
covered by the same deliberate decision and is not a data-corruption path.

Commit: none

Test: RoutingGraphTest::ProcessClampsToPreparedSize (existing, unchanged)

Red/green: none

### Cluster 5295 — Graph processing can leave stale audio in the destination

Verdict: FALSE-POSITIVE

Evidence: two claims. (a) "returns without clearing the destination when there is no output
node" — `src/core/RoutingGraph.cpp:256-257` does return early, but both production callers set
an output node *before* `prepare()`, at `src/core/Rack.cpp:172` (`setOutputNode(sumId)`) and
via `wireGraph()` in `src/core/EffectChainPatcher.cpp:101`, which refuses the wiring outright
(`return graph.setOutputNode(output)` after a negative lookup) rather than leaving a prepared
graph with no output. So the state is unreachable from the product, and `rebuildRoutingGraph()`
re-establishes the pair on every rebuild. (b) "extra channels are left untouched" — this is
the identical finding to cluster 4394 above and is answered by the same test-locked clamp in
`tests/src/core/RoutingGraphTest.cpp:597`; the destination is never wider than the prepared
graph in production.

Commit: none

Test: RoutingGraphTest::ProcessClampsToPreparedSize (existing, unchanged)

Red/green: none

### Cluster 5293 — Malformed graph connection numbers silently become zero

Verdict: FIXED

Evidence: `src/core/RoutingGraph.cpp:328-334` resolved a `<connection>`'s `from`, `to`,
`fromport` and `toport` with `QString::toInt()` and never checked the success flag, while
`loadNodes()` seven lines earlier (`:88-91`) *did* check it for a node's `id`. `toInt()`
answers 0 for non-numeric text, and file id 0 exists in a well-formed document, so
`from="bad"` attached the edge to node 0 and the graph loaded successfully with a wire the
author never wrote; a bad port attribute was quietly range-checked as the valid port 0. The
same unchecked pattern applied to the `output` attribute at line 340. Fixed by extracting
`readConnection()` (checks each attribute's flag before resolving it through `idMap`),
`loadConnections()` and `applyOutputNode()`, which keeps `load()` under the complexity budget.

Commit: 03169b9b6

Test: RoutingGraphTest::LoadMalformedDocuments (extended with four documents carrying
non-numeric `from`, `to`, `fromport` and `toport`)

Red/green:
- red: `cmake --build build --target zene RoutingGraphTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0;
  `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8,
  `FAIL! : RoutingGraphTest::LoadMalformedDocuments() '!graph.load(document.documentElement())' returned FALSE`
- green: same two commands → EXIT=0 / EXIT=0. Coverage run:
  `ctest -R '^(RoutingGraphTest|RoutingGraphLiveTest|RoutingGraphScheduleTest|RackTest|AutomationModesTest|PatcherCommandsTest)$'`
  → EXIT=0, 6 of 6 passed. Gates: `bash tests/complexity-gate.sh --check` → EXIT=0,
  `bash tests/file-length-gate.sh --check` → EXIT=0.

Leftovers: cluster 5291's doubled git wait is a real, unfixed bound and is recorded above as
REAL-UNTESTED with the `QDeadlineTimer` approach named — it needs either an injectable slow-start
child (a seam in `runGit`/`gitExecutable`) or an accepted timing test before product code should change.
No other finding is half-done.