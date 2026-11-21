# Cluster new-p10-412-port-port: tempo map snapshot can spin without a real-time bound

**Verdict: FIXED**

## What the finding said

> `include/TempoMap.h:313-326` - "The reader retries until the version is even and unchanged.
> There is no retry bound, so this audio-thread path can spin while a writer is paused or while
> edits keep arriving." Fix asked for: "Use a publication scheme that lets the reader return a
> consistent snapshot in bounded time."

## Evidence - the bug was REAL, on the pre-fix implementation

The pre-fix implementation (commit `2da2ed356`, the parent of the round-3 fix) is a seqlock.
`git show 2da2ed356:include/TempoMap.h`, lines 326-341 of that revision:

```cpp
	TempoMap snapshot() const noexcept
	{
		TempoMap copy;
		unsigned version = 0;
		do
		{
			version = m_version.load(std::memory_order_acquire);
			if ((version & 1u) == 0u) { copy = m_map; }  // odd: a write is in progress
			std::atomic_thread_fence(std::memory_order_acquire);
		}
		while ((version & 1u) != 0u || m_version.load(std::memory_order_relaxed) != version);
		return copy;
	}
```

and `edit()` of that revision made the version ODD before running the caller's mutation and EVEN
again after it (`m_version.fetch_add(1, ...)` at the head of `edit()` and after `fn(m_map)`), so a
control-thread edit that is paused inside its callback leaves the version odd forever and the audio
reader's `do ... while` spins with no bound and no exit. The measured spin is in the RED log below:
the reader made **179** copy attempts while the writer was paused.

The round-3 commit on this branch (`540c43a68`) had already replaced the seqlock with a two-buffer
publication scheme, but its regression test decided pass/fail with a 250 ms wall-clock deadline, and
this round's brief forbids a wall-clock verdict. This commit keeps the scheme, hardens it, and
replaces the test with a deterministic one.

## The fix - `include/TempoMap.h`

* `TempoMapPublisher::snapshot()` (include/TempoMap.h:339-347) is now ONE copy attempt: it pins the
  published buffer with a reader counter, copies it once, releases the pin. No retry loop, no
  version, no wait - so a reader cannot spin on a paused writer, and it answers with the map as of
  the last COMPLETED edit (include/TempoMap.h:335-338 documents that contract).
* `TempoMapPublisher::edit()` (include/TempoMap.h:303-330) publishes into the other buffer
  (include/TempoMap.h:318-319) and waits for the readers of the previous buffer to finish
  (include/TempoMap.h:314-317) - on the CONTROL thread only, with a `std::this_thread::yield()` in
  the wait (added this round: the old wait was a hot spin).
* The two buffers, the reader counter and the published index are at include/TempoMap.h:400-402.
* `setSnapshotProbeForTest()` (include/TempoMap.h:383) is the new TEST SEAM: `snapshot()` calls it
  once per copy attempt (include/TempoMap.h:341). It is null in product use, so the audio path pays
  one null-pointer test. It mirrors the existing `setPinProbeForTest()` seam (include/TempoMap.h:378).

### The regression test - `tests/src/core/TempoMapTest.cpp`

`TempoMapTest::thePublisherSnapshotIsBoundedWhileAnEditIsInFlight()`
(tests/src/core/TempoMapTest.cpp:496-556) replaces the round-3 wall-clock test:

1. a writer thread pauses inside `edit()` (a `std::latch` the test owns - no product seam needed);
2. a reader thread calls `snapshot()`; the publisher's test seam counts its copy attempts;
3. the test waits on a condition variable for "the reader answered" OR "the reader retried" - either
   outcome wakes it, so no clock decides anything;
4. the assertions are: exactly **one** copy attempt, the reader answered **while the edit was still
   in flight**, and the map it answered with is the last published one.

## RED / GREEN / reversion proof (every command and exit code)

The RED state is the pre-fix seqlock (commit `2da2ed356`) **plus the same test seam** - the seam is
test-only instrumentation and must be present in both states so that the identical test code compiles
and runs against both. It is built here:

* `logs/seam-on-prefix.patch` - the seam applied to `2da2ed356:include/TempoMap.h`
  (one call inside the retry loop, one setter, one member).
* `logs/fix.patch` - the unified diff from that RED state to the fixed state
  (`diff -u --label a/include/TempoMap.h --label b/include/TempoMap.h <red> <fixed>`).

| step | command | exit | log |
|---|---|---|---|
| RED (fix reverted) | `git apply -R logs/fix.patch` | 0 | - |
| RED build | `cmake --build build --target TempoMapTest -j 8` | 0 | logs/build-13-red-final.log |
| RED test | `cd build/tests && ctest -R '^TempoMapTest$' --output-on-failure` | **8** | logs/test-14-red-final.log |
| GREEN (fix restored) | `git apply logs/fix.patch` | 0 | - |
| GREEN build | `cmake --build build --target TempoMapTest TempoMapPersistenceTest ControlTempoMapCommandsTest FreezeRegionRepairTest AudioEditingTest SmfInterchangeTest zene -j 8` | 0 | logs/build-15-green-final.log |
| GREEN test | `cd build/tests && ctest -R '^(TempoMapTest\|TempoMapPersistenceTest\|ControlTempoMapCommandsTest\|FreezeRegionRepairTest\|AudioEditingTest\|SmfInterchangeTest)$' --output-on-failure` | **0** (6/6) | logs/test-16-green-final.log |

RED output (logs/test-14-red-final.log):

```
FAIL!  : TempoMapTest::thePublisherSnapshotIsBoundedWhileAnEditIsInFlight() Compared values are not the same
   Actual   (state.attempts): 133
   Expected (1)             : 1
   Loc: [../../tests/src/core/TempoMapTest.cpp(551)]
Totals: 12 passed, 1 failed, 0 skipped, 0 blacklisted, 1053ms
```

133 attempts is the spin itself: the reader retried until the writer was allowed to finish. The
first run of the same cycle (before the final whitespace-only tidy of the header) measured 179
attempts in logs/test-06-red.log, exit 8; the cycle was repeated end to end with the committed
patch, so the numbers above are the ones the committed patch produces.

Why the patch is not a plain `git diff`: the seqlock -> two-buffer change was already committed in
round 3 (`540c43a68`), so `git diff -- include/TempoMap.h` in this session only shows this round's
delta (the seam, the yield, the comments) and reversing it would land on the FIXED scheme, not on
the bug. `logs/fix.patch` is therefore the diff that spans the whole fix - pre-fix seqlock (with
the seam) -> fixed - and it is what `git apply -R` / `git apply` were run with above. The literal
`git diff -- include/TempoMap.h` of this session is in logs/session-fix.patch.

## Affected-family tests (all from `build/tests`)

| command | exit | log |
|---|---|---|
| `ctest -R '^(TempoMapTest\|TempoMapPersistenceTest\|ControlTempoMapCommandsTest\|FreezeRegionRepairTest\|AudioEditingTest\|SmfInterchangeTest)$' --output-on-failure` | **0** (6/6 passed) | logs/test-16-green-final.log |
| `cmake --build build --target zene -j 8` | 0 | logs/build-15-green-final.log |

`include/TempoMap.h` is a header, so every target that includes it was rebuilt (`zene`, the six
test binaries above, and `lmmsobjs` underneath them).

## Gates

| gate | command | exit | log |
|---|---|---|---|
| all gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) | logs/gates-17-feature-check-final.log |

`include/TempoMap.h` is fork-NEW (tests/fork-sources.txt:1096), so it needs no
tests/upstream-modifications.txt entry; the changed test file lives under tests/ and is allowed by
the same gate. Complexity after the change (`lizard include/TempoMap.h`): `edit` CCN 3, `snapshot`
CCN 2, `setSnapshotProbeForTest` CCN 1 - no new item in the complexity or file-length ratchets.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  RED `ctest -R '^TempoMapTest$'` exit **8** (logs/test-14-red-final.log, assertion
  `state.attempts` 133 vs 1), GREEN exit **0** (logs/test-16-green-final.log, 6/6), the revert /
  re-apply pair `git apply -R logs/fix.patch` / `git apply logs/fix.patch` both exit 0, and the
  GREEN build of every target that includes the header exit 0 (logs/build-15-green-final.log).
* **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
  values it uses).** The seam is `TempoMapPublisher::setSnapshotProbeForTest()`
  (include/TempoMap.h:383, called at include/TempoMap.h:341): it reports every copy attempt the
  reader makes, and the verdict is the attempt COUNT (exactly one) plus the map the reader answered
  with - never a duration. The synchronisation is `std::latch` (writer paused inside `edit()`,
  writer entered the edit) plus a `std::condition_variable` whose predicate is "the reader answered
  OR the reader retried", so either implementation wakes the test. The one `wait_for(30s)` is a
  deadlock guard for a hypothetical third implementation: it can only turn a hang into a FAIL, it
  can never turn a FAIL into a PASS, so the pass/fail decision is timing-independent. No sleeps, no
  polling loop, no wall-clock comparison anywhere in the test.
* **The fix is at the cause and complete: every caller and sibling of what you changed was checked
  and stays consistent.** Callers of the publisher: `Song::followTempoMap` and
  `Track::playFrozenTake` read the map through `withRealtimeMap()` (src/core/Song.cpp:1367,
  src/core/Track.cpp:1376) - that path is untouched and stays bounded (three attempts, include/TempoMap.h:355-371);
  every MUTATION goes through `edit()` (`SongSections.cpp:136` on load, `Song.cpp:1613` on clear,
  `ControlCommandsTransportMap.cpp:160` for the transport commands, `ControlCommandsInterchange.cpp`
  for the SMF import) - `map()` (include/TempoMap.h:333) hands out a const reference, so no caller
  can mutate the map behind the publisher. The sibling publication path in the same class
  (`m_rtSlots` / `publishRealtime()`, include/TempoMap.h:387-397) is unchanged and still consistent:
  `edit()` publishes to both schemes, and the throwing path rolls `m_map` back and re-publishes the
  realtime slots without publishing a snapshot, so `snapshot()` keeps answering the last COMPLETED
  edit (the pre-existing test `thePublisherRecoversAfterAThrowingEdit` still passes). The same
  seqlock shape exists in the sibling class `ModulationLayerPublisher::snapshot()`
  (include/ModulationLayer.h:426); that is a different subsystem with its own publisher and its own
  test suite, and it is reported here as a separate defect rather than changed silently in a
  tempo-map fix.
* **No collateral damage: old projects and saved data still load as before, and any limit or check
  you added does not hit other callers that legitimately exceed it.** Nothing in the save/load path
  changed: `TempoMap` itself (the value, its XML form, `shouldPersist()`) is untouched, and the
  project file format is untouched - `TempoMapPersistenceTest` passes (logs/test-16-green-final.log).
  The reader's answer changed from "the map after the edit in flight" to "the map as of the last
  completed edit" - that IS the fix (a bounded reader cannot wait for the writer) and it only
  affects a reader that reads DURING an edit; a reader that reads after the edit completes sees the
  same map as before. No limit, threshold or check was added to any caller-facing path.
* **Realtime: nothing added to the audio thread allocates, frees, locks, waits or logs.**
  `snapshot()` copies a fixed-size `TempoMap` (128 events, about 3 KiB) into a local, uses only
  atomics (`fetch_add`/`fetch_sub` on the reader counter, one `load`), and the new seam is a
  null-pointer test that is null in product use. No allocation, no mutex, no wait, no logging was
  added; the writer's wait (the only waiting left) is on the control thread. The `withRealtimeMap()`
  path used by the actual audio callback is unchanged.
* **Undo and commands:** untouched - no command was added, changed or removed for this fix
  (ControlCommandsTransportMap.cpp is not in this diff), so the ControlCommandsSnapshot canary does
  not move and the undo stack is unaffected (one edit is still one `edit()` call, which is one
  command's own undo step).
* **Portability:** C++20 only (`std::latch` was already used in this test file,
  `std::condition_variable`/`std::mutex` are standard), no Qt API added, no platform headers.
* **The verdict.** The verdict is FIXED, not ALREADY-FIXED and not FALSE-POSITIVE, so the
  "show the code that proves it" item does not apply to an already-fixed claim - but the evidence
  is given anyway: the pre-fix implementation is the seqlock quoted at the top of this report
  (`git show 2da2ed356:include/TempoMap.h`, the `do ... while` retry at lines 326-341 of that
  revision, with `edit()` making the version odd around the caller's mutation), and the RED run
  measures the spin it causes (133 copy attempts while the writer was paused, logs/test-14-red-final.log).
  The fix is on this branch now: include/TempoMap.h:339-347 is a single copy attempt with no retry.

## Files committed in this commit

- include/TempoMap.h - the bounded publication scheme (hardened), the `setSnapshotProbeForTest()`
  seam, the control-thread yield, the comments.
- tests/src/core/TempoMapTest.cpp - the deterministic regression test (replacing the round-3
  wall-clock test).
- docs/qwen-fixes-2026-10/cluster-new-p10-412-port-port.md - this report.

(The round-3 fix itself - the seqlock -> two-buffer change - is commit `540c43a68` on this branch;
this commit is the review round's follow-up on top of it.)
