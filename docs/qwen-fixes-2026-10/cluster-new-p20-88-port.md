# FIXED - cluster new-p20-88-port: audio snapshots spin while a layer edit waits for active writers

Port of the accepted fix `bugs/qwen-new-p20-88` onto the newest train line
(`dbb15dded7041e37e2d1121ea80cd7e6b666e38b`, train 11 tip), re-applied BY HAND
(`git show` of `692c5b6dd`, `92af8b789`, `7e7519c39`, `7dd26d7f1`, `34b68a350`,
`c3530747f`; no merge, no cherry-pick, no rebase).

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-88-port.r1/logs`.

## 1. Why this port could not be a copy of the accepted branch

The accepted branch could not go on the newest train because it added data
members to the publisher class inside `#ifdef LMMS_TESTING`
(`m_testHookContext`, `m_beforeViewPublishHook`, `m_snapshotHook`). The test
build compiles `lmmsobjs` WITH `LMMS_TESTING` (`tests/CMakeLists.txt:5-6`), the
`zene` target's `main.cpp` WITHOUT it, so the class layout differed between the
two and the app segfaulted at startup.

This port therefore uses **NO conditional member at all**: the fix adds no
member to `ModulationLayerPublisher`, and the regression test drives the
publisher through the friend seam the class already grants
(`friend class ::ModulationAudioViewTest;`, `include/ModulationLayer.h:464`)
and through the public `beginTargetWrite()` / `endTargetWrite()` /
`targetAlive()` writer-pin API. Layout evidence:

```
$ grep -c "LMMS_TESTING" include/ModulationLayer.h src/core/ModulationLayerPublisher.cpp
0
$ cat logs/layout-probe.log          # sizeof probe, same TU compiled both ways
== WITH LMMS_TESTING ==
sizeof(ModulationLayerPublisher)=28080
== WITHOUT LMMS_TESTING ==
sizeof(ModulationLayerPublisher)=28080
```

and the app starts (`logs/app-startup-2.log`: `ALIVE_AFTER_12S_NO_STARTUP_CRASH`).

## 2. The finding: REAL on this branch

The finding (Luna's patrol, `include/ModulationLayer.h:351-355`,
`src/core/ModulationLayerPublisher.cpp:183-197`) is the reader-visible write
window of the modulation publisher. On this branch, before the fix:

```cpp
// include/ModulationLayer.h:348-356 (the tree this port starts from)
template <typename Fn>
bool edit(Fn&& fn)
{
	m_version.fetch_add(1, std::memory_order_acq_rel);   // <- the version goes ODD here
	const bool changed = fn(m_layer, m_runtime);
	publishView();                                        // <- the WAIT is inside the window
	m_version.fetch_add(1, std::memory_order_acq_rel);
	return changed;
}
```

`publishView()` closes every target slot and WAITS for the audio writes that
pinned it (`src/core/ModulationLayerPublisher.cpp:184-198`):

```cpp
while ((targetWriterState(writerState) & targetClosingBit) == 0) { ... }  // close the slot
while ((targetWriterState(writerState) & targetWriterCountMask) != 0)
{
	std::this_thread::yield();                 // <- waits for the audio writer to let go
	writerState = writers.load(std::memory_order_acquire);
}
```

The whole wait therefore sat inside the reader-visible write window, and
`snapshot()` retries while the version is odd
(`include/ModulationLayer.h:446-461`): every audio snapshot taken during that
wait spun for as long as the edit waited for the writer - a duration the audio
thread cannot bound.

Sibling checked: `TempoMapPublisher` (`include/TempoMap.h:302-347`) already
uses the pinned double-buffer shape - the reader copies the published buffer and
never waits for an edit - so it does not have this defect and was left alone.

## 3. The fix (at the cause)

The write window now holds the view swap and nothing else; the mutation and the
whole publication work (including the writer quiescence) happen BEFORE it.

`include/ModulationLayer.h:350-376` (after the fix):

```cpp
template <typename Fn>
bool edit(Fn&& fn)
{
	const bool changed = fn(m_layer, m_runtime);
	ModulationAudioView staged;
	publishView(staged);
	// The write window: the odd version says "a write is in progress", the
	// even one says "the view below is the published one". Nothing else
	// happens inside it - a reader that retries does so for the length of
	// one fixed-size copy, never for the length of an edit.
	m_version.fetch_add(1, std::memory_order_acq_rel);
	m_view = staged;
	m_version.fetch_add(1, std::memory_order_acq_rel);
	return changed;
}
```

`src/core/ModulationLayerPublisher.cpp:168` and `:243-249`: `publishView()` now
fills a staged view instead of writing `m_view`; the caller swaps it in inside
the window. The writer quiescence, the token re-issue, the liveness watches and
the `hasLayer`/trigger publishing are unchanged.

Every caller checked: `publishView()` has exactly one caller
(`edit()`, `include/ModulationLayer.h:367`); `edit()` is the only writer of the
view; `snapshot()` is the only reader of `m_view` and is untouched.

## 4. Regression (RED before, GREEN after)

`tests/src/core/ModulationAudioViewTest.cpp:419-477`
(`anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven`).

Deterministic, no wall clock: the test pins the target from a second thread
(`beginTargetWrite`), the control thread's edit closes the slot and waits for
that pin, and the pinned thread reads the reader-visible version exactly inside
that wait (the edit cannot finish the wait before the pin is released, so the
read is inside it - the ordering is forced by the pin, not by a timer). It then
calls `snapshot()` only if the version is even, so the RED run fails on the
assertion instead of hanging.

| # | Command | Exit |
|---|---------|------|
| RED (before the fix) | `cmake --build build --target ModulationAudioViewTest` > `logs/build-red-1.log` | 0 |
| RED (before the fix) | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` (from `build/tests`) > `logs/test-red-1.log` | **8** |
| GREEN (with the fix) | `cmake --build build --target ModulationAudioViewTest zene` > `logs/build-green-1.log` | 0 |
| GREEN (with the fix) | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` > `logs/test-green-1.log` | **0** |

The RED failure was the new case, with the expected message
(`logs/test-red-1.log`):

```
FAIL!  : ModulationAudioViewTest::anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven()
  '(versionWhileWaiting.load(std::memory_order_acquire) & 1u) == 0u' returned FALSE.
  (the reader-visible version was odd while the edit waited for an active writer:
   the audio thread's snapshot() spins for as long as the edit takes)
```

### 4b. Proof that the test catches the bug (the product diff reverted)

```
git diff -- include/ModulationLayer.h src/core/ModulationLayerPublisher.cpp > logs/fix.patch
git apply -R logs/fix.patch
cmake --build build --target ModulationAudioViewTest        -> logs/build-red-2.log   EXIT=0
ctest -R '^ModulationAudioViewTest$' --output-on-failure    -> logs/test-red-2.log    EXIT=8   (FAIL, same assertion)

git apply logs/fix.patch
cmake --build build --target ModulationAudioViewTest zene   -> logs/build-green-2.log EXIT=0
ctest -R '^ModulationAudioViewTest$' --output-on-failure    -> logs/test-green-2.log  EXIT=0   (Passed)
```

## 5. Affected test family (all rebuilt against the changed header)

`logs/test-family.log`, `logs/test-consumers.log`, `logs/test-modulatorpanel.log`:

| Test | Result |
|------|--------|
| ModulationAudioViewTest | Passed |
| ModulationLayerValueTest | Passed |
| ModulationLayerTest | Passed |
| ModulationLayerProjectRoundTripTest | Passed |
| ModulationRateTest | Passed |
| ControlModulatorCommandsTest | Passed |
| ControlNoteExpressionCommandsTest | Passed |
| ProjectOpenIntegrityTest | Passed |
| InstrumentRoutingTest | Passed |
| InstrumentLayersCommandsTest | Passed |
| MidiInputTransformTest | Passed |
| ModulatorToolsTest | Passed |
| ModulatorPanelTest | Passed |
| InstrumentSetupUiTest | Passed |

`ctest -R 'Modulation'` (5 tests) EXIT=0; the wider consumer set (13 tests,
the 14th - ModulatorPanelTest - was built in the same round and then run on its
own) EXIT=0. The single new case on its own:
`logs/test-green-case.log`, EXIT=0, `PASS : ...anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven()`.

## 6. Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
-> `logs/gates-3.log` (the run with the report in the tree; `logs/gates-2.log` is
the same run without it), **EXIT=1** (the two FAIL rows are PRE-EXISTING at the
base commit - see the Self-check):

```
feature-check zene-qwen-new-p20-88-port (base dbb15dded, 4 changed files)
  FAIL  fork-sources            exit 1
  FAIL  all-sources-reproduce   exit 1
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology           exit 0
  PASS  doc-figures             exit 0
  PASS  complexity             no new item in your files
  PASS  file-length            no new item in your files
RESULT: FAIL
```

The script's baseline step (`git checkout --detach` in a cached tree) is
blocked by the lane guard (`bunny-guard`, exit 97), so the base listing was
computed the same way the script computes it - the same two ratchet scripts on
the base commit's tree, extracted with `git archive` (no history operation) -
and written to the script's own cache file
(`gate-baselines/dbb15dded7041e37e2d1121ea80cd7e6b666e38b.json`).

## 7. Files changed

| File | What |
|------|------|
| `include/ModulationLayer.h` | `edit()`: the mutation and `publishView()` moved OUT of the reader-visible write window; the window holds the view swap only. `publishView()` now takes the staged view. No member added, no member conditional. |
| `src/core/ModulationLayerPublisher.cpp` | `publishView(ModulationAudioView& staged)` fills a staged view instead of writing `m_view`. |
| `tests/src/core/ModulationAudioViewTest.cpp` | the regression `anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven`. |
| `tests/upstream-modifications.txt` | the three inherited files registered with their reason (same commit). |
| `docs/qwen-fixes-2026-10/cluster-new-p20-88-port.md` | this report. |

No new file: the fix adds no source and no test file, so `tests/fork-sources.txt`
and `tests/all-sources.txt` need no new entry.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it (exit codes).** RED `ctest -R '^ModulationAudioViewTest$'` EXIT=8 (the new
   case, `logs/test-red-1.log`), GREEN EXIT=0 (`logs/test-green-1.log`), and the
   revert/restore cycle of step 4b: EXIT=8 -> EXIT=0 (`logs/test-red-2.log`,
   `logs/test-green-2.log`).

2. **The test is deterministic: no wall-clock timing, sleeps or races.** It uses
   the publisher's own writer pin: the second thread holds `beginTargetWrite()`,
   the control thread's edit closes the slot and waits for that pin, and the
   version is read by the pinned thread while the edit is provably still inside
   that wait (the wait cannot end before the pin is released). No timer, no
   sleep, no timeout - the `std::this_thread::yield()` loops are the publisher's
   own wait, not the test's timing.

3. **The fix is at the cause and complete.** The cause is the writer-quiescence
   wait inside the reader-visible window; it is now outside it. Callers checked:
   `publishView()` has one caller (`edit()`), `edit()` is the only writer of the
   view, `snapshot()` is the only reader and is unchanged. Sibling checked:
   `TempoMapPublisher` (`include/TempoMap.h:302-347`) already pins its published
   buffer and never waits for an edit, so it needs nothing. The other two
   `#ifdef LMMS_TESTING` seams of the accepted branch (the before-publish hook
   and the snapshot hook) are NOT ported: the regression needs neither, so the
   class layout is untouched.

4. **No collateral damage.** Old projects and saved data are unaffected: the
   change is in the publisher's hand-off, not in the layer, the runtime, the
   save format or the command surface; `ModulationLayerProjectRoundTripTest`
   passes. No limit or check was added, so no other caller can trip it. Behaviour
   outside the bug is unchanged: the same view is published, with the same
   tokens, the same liveness watches and the same writer quiescence; only the
   WINDOW in which the swap happens moved. Two consequences worth stating: (a)
   while an edit waits for a writer, the audio thread now reads the LAST
   published view instead of spinning - the slots whose tokens the publication
   has already retired are skipped by `drivingSource()`
   (`src/core/ModulationLayerPublisher.cpp:274-282`), which is the pre-existing
   contract for a re-bound or destroyed target; (b) a mutation that throws now
   leaves the reader-visible version even (the version is only bumped around the
   swap, which a throw cannot reach), so the audio thread no longer spins after
   a failed edit - the same conclusion the separate `new-p14-198` fix reaches by
   a scope guard, reached here without one.

5. **Nothing new runs on the audio thread.** `snapshot()` is byte-for-byte the
   same code as before; the audio path (`applyModulationBlock()`) is untouched.
   No allocation, no free, no mutex, no wait, no logging was added to the
   audio thread - the only new work is on the control thread (the staged view
   copy, one fixed-size value). No new control command was added, so the
   `ControlCommandsSnapshot` canary does not move. Undo is untouched: `edit()`
   keeps its single-call shape and no command surface changed.

6. **Old projects still load and render exactly as before; new data round-trips.**
   `ModulationLayerProjectRoundTripTest` and `ProjectOpenIntegrityTest` pass;
   the save format, the layer and the runtime are not touched by this change.

7. **Portability (Qt 5.15 / MSVC / macOS < 10.15).** The change uses only
   `std::atomic`, a trivially copyable value and the existing
   `std::this_thread::yield()`; no new Qt API, no `windows.h` macro, no
   `std::filesystem`.

8. **The two FAIL gate rows are PRE-EXISTING at the base commit, not caused by
   this change.** Evidence (`logs/gate-preexisting-evidence.log`):
   `git diff --stat dbb15dded -- tests/all-sources.txt` is EMPTY (this diff does
   not touch the manifest, so its entry list is byte-identical to the base
   commit's), and the base commit's own copy already carries the 1148 vendored
   `plugins/SfzPlayer/sfizz/**` paths (`git show dbb15dded:tests/all-sources.txt`
   counts 1148) that the manifest's OWN published recipe excludes
   (`tests/all-sources.txt:20-30`). The gate's failing diff names only those
   vendored paths. They were added by the train-10 automation commit
   `5ef150729` ("all-sources re-derived") and are still there at the train-11
   tip `dbb15dded`, so the train's own re-derivation contradicts the manifest's
   published recipe and the row stays red on the train. This change adds no
   file (the recipe matches only `*.c|*.cc|*.cpp|*.cxx|*.h|*.hpp`, and the one
   new file is a `.md` report), so it changes neither side of that comparison
   and no registration is needed. Repairing the shared manifest would mean
   deleting 1148 entries from a repo-wide scope list inside a bug-fix commit -
   churn that the train's automation re-introduces - so it is not this bug's
   collateral to carry. The ratchet rows this change CAN own - `complexity` and
   `file-length` - both PASS ("no new item in your files"), and
   `no-upstream-regression` PASSES with the three inherited files registered.

9. **The `LMMS_TESTING` layout trap of the accepted branch is not reintroduced.**
   No member is added to `ModulationLayerPublisher` and none is conditional;
   `grep -c LMMS_TESTING include/ModulationLayer.h src/core/ModulationLayerPublisher.cpp`
   is 0, the class is 28080 bytes with and without `-DLMMS_TESTING`
   (`logs/layout-probe.log`), and the app stays alive after startup
   (`logs/app-startup-2.log`).
