== Gate 12 baseline on committed 40f468dc0 ==
  * NO COST, NO SYSCALL AND NO I/O CHECK: only allocation, locking and container growth, the three words of the AGENTS.md rule.
  * NO ALIASING ANALYSIS: a hit is a mention of a construct, not proof that it runs on the audio thread. An allowlist line with a reason is how a tolerated mention is said.

PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count.
SWEEP_BASELINE_EXIT=0
== ctest -R RtSafety (build/tests) ==

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   0.49 sec
CTEST_RTSAFETY_EXIT=0

== POSITIVE CONTROL: deliberate new float[4] in RoutingGraph::process ==
---- 1 problem(s) ----
FAIL: NEW rt-safety hit: src/core/RoutingGraph.cpp:RoutingGraph::process:alloc-new [allocation] - 1 occurrence(s), first at src/core/RoutingGraph.cpp:233: { float* rtProbe = new float[4]; rtProbe[0] = 0.0f; delete[] rtProbe; }
SWEEP_POSITIVE_CONTROL_EXIT=1

== revert: git checkout HEAD -- src/core/RoutingGraph.cpp ==
HEAD:  d3b1150da05effc23176b6bb79b2623871e5c540d1811852e6ba9cfe50d465d7
WORKTREE: d3b1150da05effc23176b6bb79b2623871e5c540d1811852e6ba9cfe50d465d7
SHA_MATCH=no
SWEEP_AFTER_REVERT_EXIT=0
git status --porcelain:
?? docs/mc-logs/rt-safety-slice01.txt
(end status)
SHA_FIELDS head=d3b1150da05effc23176b6bb79b2623871e5c540d1811852e6ba9cfe50d465d7 worktree=d3b1150da05effc23176b6bb79b2623871e5c540d1811852e6ba9cfe50d465d7
SHA_MATCH=yes
