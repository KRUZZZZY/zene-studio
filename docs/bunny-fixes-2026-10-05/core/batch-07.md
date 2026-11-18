# Space Bunny batch 07 — `core` lane

Six Luna findings, each checked against the current tree
(`bugs/bunny-core-2026-10-05`, head before this batch `8878685e6`). Line numbers
are from the current tree, not the revisions the findings quoted.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 5356 — Small positive follow-action times can never fire | FIXED | `1810e4a13` |
| 3462 — Malformed follow-action times cause undefined behaviour | FIXED | `2449b6837` |
| 3463 — Chance and target selection reuse the same random draw | FIXED | `a8ce61b8d` |
| 3565 — Export tail and length inputs are not bounded | REAL-UNTESTED | none |
| 5313 — The audio reader can spin while the tuning writer is paused | SKIPPED-SCOPE | none |
| 2495 — Disarming Arrangement Record can leave an unmatched event | SKIPPED-SCOPE | none |

### Cluster 5356 — Small positive follow-action times can never fire
Verdict: FIXED
Evidence: `include/SessionFollow.h:171-172` computed
`ticks = timeBars * ticksPerBar` and returned `ticks > 0.5 ? static_cast<tick_t>(ticks + 0.5) : 0`.
Any positive unlinked time whose product rounded below one tick therefore
answered 0, which the header's own rule (`:158-159`, "unlinked ... rounded to the
nearest tick and never zero") forbids. At 192 ticks per bar, 0.001 bars made the
chain permanently unfireable.
Fix: the value is floored at one tick whenever it is positive, so only a
non-positive time answers 0 ("cannot fire").
Commit: 1810e4a13
Test: `SessionFollowTest::positiveFollowTimesAreNeverZero`
Red/green: `cmake --build build --target zene SessionFollowTest` → EXIT=0;
`cd build/tests && ctest -R '^SessionFollowTest$' --output-on-failure` → EXIT=8
(`followActionTicks(subTick, kTicksPerBar)`: Actual `0`, Expected `1`) before the
fix, EXIT=0 after.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 3462 — Malformed follow-action times cause undefined behaviour on the audio thread
Verdict: FIXED
Evidence: the same `include/SessionFollow.h:171-172` cast an unchecked `double`
product to `tick_t` (`std::int32_t`, `include/LmmsTypes.h:35`) with no finiteness
or range check. The command layer rejects only a negative `time_bars`
(`src/core/ControlCommandsSessionFollow.cpp:115-122`), so an unlinked time of
`1e100` — or an infinity/NaN — reached an out-of-range floating-to-integer
conversion on the audio thread, which is undefined behaviour. The regression test
observed it directly: the conversion returned `-2147483648`.
Fix: a non-positive or out-of-`tick_t`-range product is answered 0 ("cannot
fire") before the conversion; ordinary in-range rounding is untouched.
Commit: 2449b6837
Test: `SessionFollowTest::malformedFollowTimesAreRefusedBeforeConversion`
Red/green: `cmake --build build --target zene SessionFollowTest` → EXIT=0;
`cd build/tests && ctest -R '^SessionFollowTest$' --output-on-failure` → EXIT=8
(`followActionTicks(plan, kTicksPerBar)`: Actual `-2147483648`, Expected `0`)
before the fix, EXIT=0 after.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 3463 — Chance selection and target selection reuse the same random draw
Verdict: FIXED
Evidence: `include/SessionFollow.h:306` picks the chain entry from `eval.rngUnit`
and `:329-330` passed the SAME `eval.rngUnit` to `followTargetScene`, which uses it
for `Any` and `Other` (`:271`, `:275`). The two choices were therefore correlated:
holding the entry draw fixed pinned the target to one scene row, which the
regression test shows directly.
Fix: `FollowEval` gained a second, independent `targetRngUnit`
(`include/SessionFollow.h:146-151`), drawn separately at both audio-thread call
sites (`src/core/SessionFollow.cpp:274`, `src/core/SessionSceneFollow.cpp:211`) and
used only for the target pick (`:330`). The decision remains pure and
allocation-free; the chance weighting itself is unchanged.
Commit: a8ce61b8d
Test: `SessionFollowTest::chanceAndTargetUseIndependentDraws`
Red/green: `cmake --build build --target zene SessionFollowTest SessionSceneFollowTest` → EXIT=0;
`cd build/tests && ctest -R '^(SessionFollowTest|SessionSceneFollowTest)$' --output-on-failure`
→ EXIT=8 (`decideFollowFire(anyPlan, high).targetScene`: Actual `0`, Expected `1`)
before the fix, EXIT=0 after.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 3565 — Export tail and length inputs are not bounded
Verdict: REAL-UNTESTED
Evidence: the setters are unclamped (`include/Song.h:276-279`, `:290-293`) and
`startExport()` converts them without bounds (`src/core/Song.cpp:1020-1023`
`static_cast<bar_t>(m_exportLengthOverrideBars)`, `:1032`
`m_exportSongEnd += TimePos(m_exportTailBars, 0)`), so an out-of-range integer
could overflow the tick arithmetic. The finding is not reachable through the
shipped callers, which all validate: the command layer rejects a negative
`tail_bars` (`src/core/ControlCommandsRenderStems.cpp:133-141`) under a schema
bounded to 0..64 (`:273`, `:279`), the CLI rejects a negative `--tail-bars`
(`src/core/main.cpp:776-791`), and `RenderManager` clamps to `std::max(tailBars, 0)`
(`src/core/RenderManager.cpp:180`) and derives the override from `Song::length()`
(`:165-178`). No deterministic headless failing test could be built for the
remaining setter-level exposure within this batch, so per the procedure no
product code was changed.
Commit: none
Test: none
Red/green: none

### Cluster 5313 — The audio reader can spin while the tuning writer is paused
Verdict: SKIPPED-SCOPE
Evidence: `SessionTuning.cpp:236-282` claims the back slot with `WriterBit`,
writes the table, CAS-publishes `m_front` (`:270-271`) and only then clears
`WriterBit` (`:274`). `acquireReadSlot()` spins while the current FRONT slot has
that bit (`include/SessionTuning.h:211-231`), so a freshly published front can
still be flagged and an audio callback can wait on the control thread. The
seemingly obvious reorder — clear the bit before publishing — is unsafe: it lets a
second writer claim and overwrite a slot another writer still owns. Any correct
fix reworks the slot publication protocol itself, which is the realtime
concurrency design (rule 6) rather than a behaviour-preserving patch.
Commit: none
Test: none (no deterministic headless test can pin a cross-thread scheduling race)
Red/green: none

### Cluster 2495 — Disarming Arrangement Record can leave an unmatched event
Verdict: SKIPPED-SCOPE
Evidence: `include/SessionArrangementRecorder.h:110-141` has the audio thread read
`armed` and then push, with no barrier against the model thread disarming and
snapshotting in between, so an in-flight start can land in the ring without its
stop. The proposed fix is a disarm handshake that makes the CONSUMER wait for an
in-flight producer write, i.e. a wait on the audio-thread path (rule 6). The
practical impact is also bounded: the consumer refuses rather than dropping data
(`src/core/ControlCommandsSessionRecord.cpp:133-148` reports `open_starts` and
`landable_pairs`), so the failure mode is a refused landing, not lost audio.
Commit: none
Test: none (the race needs a thread interleaving a deterministic test cannot force)
Red/green: none

Leftovers: Cluster 3565 is real but untested — the clamp belongs in the two
`Song.h` setters, and no headless test in this lane drives them; Cluster 5313 and
Cluster 2495 both need a deliberate concurrency-protocol decision by the parent
lane rather than a local patch.
