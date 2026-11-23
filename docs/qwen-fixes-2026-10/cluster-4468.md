ALREADY-FIXED

# Cluster 4468 - SlicerT emits Qt signals from the audio path

The finding describes `plugins/SlicerT/SlicerT.cpp:138` (historical) in `playNoteImpl`
emitting `isPlaying` while rendering a note. That was a real defect and it is fixed on
this branch, with a regression test, before this session started. Round 2 re-ran the
whole proof (this run's logs are in `.../4468.r2/logs`) and fixes the three report
problems the round-1 review named: the wall-clock wait is now stated as such, the diff
snippet is the byte-valid one, and the no-weakening claim is backed by diffs.

## The fix that is on the branch

- `plugins/SlicerT/SlicerT.cpp:174` `SlicerT::playNoteImpl` - the render path. It emits
  nothing: every exit publishes three atomics through `publishPlayhead(...)` at `:204`,
  `:211`, `:221`, `:255`, `:257`.
- `plugins/SlicerT/SlicerT.cpp:402-407` `SlicerT::publishPlayhead` - three
  `std::atomic<float>::store(..., std::memory_order_relaxed)`, `noexcept`.
- `plugins/SlicerT/SlicerT.cpp:409-419` `SlicerT::pollPlayhead` - the only remaining
  `isPlaying` emission (`:418`), on the GUI thread.
- `plugins/SlicerT/SlicerTView.cpp:60,68,69` - `m_playheadPoll` (`QTimer`, parent the
  view), `connect(m_playheadPoll, &QTimer::timeout, instrument,
  &SlicerT::pollPlayhead); m_playheadPoll->start(20);`.
- Receiver unchanged: `plugins/SlicerT/SlicerTWaveform.cpp:90`
  `connect(instrument, &SlicerT::isPlaying, this, &SlicerTWaveform::isPlaying)`.
- `deleteNotePluginData` (`:260`), also a render-owner call (`tests/rt-safety-scope.txt:69`),
  publishes at `:274` and never emits.

Commits reachable from HEAD (`git merge-base --is-ancestor <sha> HEAD` printed `YES` for
both in round 1 and the shas are still in `git log`): `b8b70371ad` "fix(slicer): the
playhead reaches the GUI from a timer, not a signal emitted on the render thread
(cluster 4468)" and `e54efbc921` "fix(slicer): move playhead timer to the GUI view
(cluster 4468)".

Siblings checked: the other render-path functions `applySliceControls` (`:277`),
`acquirePlaybackSlot`, `publishPlayhead` contain no `emit`. The remaining
`emit dataChanged()` sites are control/GUI-thread entry points: `:398`
`setSliceControl` (reached by `plugin.slice_set`), `:528` `findSlices`, `:592`
`updateFile`, `:694` `loadSettings`.

## Regression test and what its two halves prove

`tests/src/core/SlicerTPlayheadTest.cpp`, test
`SlicerTPlayheadTest::aSoundingSliceNoteEmitsItsPlayheadFromTheGuiThreadOnly`.
Registered in `tests/CMakeLists.txt:596`, module dependency at `:2772`.

- `:287` eight driven `renderPeriod()` calls with a slice note sounding.
- `:289` `QCOMPARE(playheads.count(), 0);` - the oracle for this defect. No timer, no
  sleep, no injected value: the render thread either publishes or emits, and one
  emission fails the count.
- `:291` `QTest::qWait(120);` then `:292` `QVERIFY2(playheads.count() > 0, ...)`.
  **`QTest::qWait(120)` is a bounded wall-clock wait**: it pumps the event loop until
  120 ms have elapsed, and the assertion needs a real 20 ms tick of the GUI poll timer
  to pass. That is the design's own shape (a poll timer), not something the test can
  inject away without a seam in the product. It is a timing dependency in the delivery
  half of the test only; the defect oracle at `:289` has none, and every red run below
  fails at `:289`.

## Commands and exit codes (this run, `4468.r2/logs`)

| # | command | log | EXIT |
|---|---------|-----|------|
| 1 | `cmake --build build --target SlicerTPlayheadTest -j4` | `build-1.log` | 0 |
| 2 | `cmake --build build --target slicert -j4` | `build-2-slicert.log` | 0 |
| 3 | `ctest -R SlicerTPlayheadTest --output-on-failure` (HEAD) | `test-1-green.log` | 0 |
| 4 | python one-line edit of `SlicerT.cpp` + `git diff -- plugins/SlicerT/SlicerT.cpp > unfix.patch` (595 bytes) | `unfix.patch` | 0 |
| 5 | `git apply -R .../logs/unfix.patch` (patch is byte-valid, reverses cleanly) | - | 0 |
| 6 | `git apply .../logs/unfix.patch` (back to the emitted state) | - | 0 |
| 7 | `cmake --build build --target slicert -j4` (with the emission back) | `build-3-unfix.log` | 0 |
| 8 | `ctest -R SlicerTPlayheadTest --output-on-failure` (with the emission back) | `test-2-red.log` | 8 |
| 9 | `git checkout -- plugins/SlicerT/SlicerT.cpp` | - | 0 |
| 10 | `cmake --build build --target slicert -j4` (restored) | `build-4-restore.log` | 0 |
| 11 | `ctest -R SlicerTPlayheadTest --output-on-failure` (restored) | `test-3-green-restored.log` | 0 |
| 12 | `ctest -R "SlicerT" --output-on-failure` (family) | `test-4-family.log` | 0 |
| 13 | `python3 .../qwen/feature_check.py <worktree>` | `gates-1.log` | 0 |

Green at HEAD (run 3), EXIT 0:

```
1/1 Test #168: SlicerTPlayheadTest ..............   Passed    2.94 sec
100% tests passed out of 1
```

Red with the pre-4468 emission restored (run 8), EXIT 8:

```
FAIL!  : SlicerTPlayheadTest::aSoundingSliceNoteEmitsItsPlayheadFromTheGuiThreadOnly() Compared values are not the same
   Loc: [../../tests/src/core/SlicerTPlayheadTest.cpp(289)]
Totals: 6 passed, 1 failed, 0 skipped, 0 blacklisted, 3100ms
```

Green after restoration (run 11), EXIT 0:

```
1/1 Test #168: SlicerTPlayheadTest ..............   Passed    2.86 sec
100% tests passed out of 1
```

Family (run 12), EXIT 0: `SlicerTPlayheadTest`, `SlicerTSliceControlsTest`,
`SlicerTWaveformTest`, `SlicerTZoomTest` - `100% tests passed out of 4`.

Gates (run 13), EXIT 0: `RESULT: PASS`, all eight rows `PASS` (`fork-sources`,
`all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`,
`doc-figures`, `complexity`, `file-length`).

### The reintroduced emission line, exactly

`logs/unfix.patch` is machine-applicable: `git apply -R` (EXIT 0) and `git apply`
(EXIT 0) both succeed on it, so its whitespace is valid. The added line, tabs included
(two tabs, as `cat -A` shows `^I^Iemit isPlaying`):

```diff
@@ -253,6 +253,7 @@ void SlicerT::playNoteImpl(NotePlayHandle* handle, std::span<SampleFrame> out)
 
 		const auto currentNote = static_cast<float>(playbackState->frameIndex()) / m_originalSample.sampleSize();
 		publishPlayhead(currentNote, sliceStart, sliceEnd);
+		emit isPlaying(currentNote, sliceStart, sliceEnd);  // RED PROOF ONLY: the pre-4468 render-thread emission
 	}
 	else { publishPlayhead(-1, 0, 0); }
 }
```

The product edit existed only for runs 7-8 and was reverted at run 9
(`git status --short` empty afterwards, checked in the same command).

## Self-check

**1. "The regression test FAILS with your product change reverted and PASSES with it
(exit codes)."** This session makes no product change - the fix is `b8b70371ad` /
`e54efbc921`. I removed the fix's behaviour instead, for one build: one added
`emit isPlaying(...)` in `playNoteImpl` (runs 4-8, EXIT 8 at
`tests/src/core/SlicerTPlayheadTest.cpp:289`), then restored and reran (runs 9-11,
EXIT 0). Logs and exit codes are the table above.

**2. "The test is deterministic: no wall-clock timing, sleeps or races."** Precisely:
the defect oracle (`:289`) is deterministic - eight driven `renderPeriod()` calls and a
count of zero emissions, no timer and no injected value, because the render thread has
exactly two mutually exclusive options (publish three atomics, or emit) and the spy
counts the second one immediately. `QTest::qWait(120)` at `:291` **is a bounded
wall-clock wait**, and the delivery assertion at `:292` needs a real 20 ms tick of the
GUI poll timer; that half cannot be made timing-free without adding a poll seam to the
product, which is a design change outside this verdict. Every red run fails at `:289`,
never at `:292`, so the timing dependency is not what catches the bug.

**3. "The fix is at the cause and complete: every caller and sibling checked."** All
five exits of `playNoteImpl` publish (`:204`, `:211`, `:221`, `:255`, `:257`), the
render-owner teardown path publishes (`:274`), and no `emit` remains in
`playNoteImpl`, `deleteNotePluginData`, `applySliceControls`, `acquirePlaybackSlot` or
`publishPlayhead`. The single `isPlaying` emission is `pollPlayhead` (`:418`), wired to
the view's timer (`SlicerTView.cpp:68-69`), which is created after the instrument has
moved to the GUI thread (`SlicerTView.cpp:64-66`). Receiver and signal signature
(`SlicerT.h:51`) are unchanged, so every pre-existing GUI receiver still gets the
playhead.

**4. "No collateral damage."** No save/load code is touched (`saveSettings` `:605`,
`loadSettings` `:640` unchanged by the fix), so old projects load and render as before;
no limit or check was added that could reject another caller. The red/green runs render
the same slice audio and differ only in the emission; the fade, reuse and teardown tests
in the same file pass in both runs (6 of 7 test functions pass in the red run, the 7th
is the intentionally broken one). Audio thread: `publishPlayhead` is three relaxed
atomic stores; the `QTimer`, the polling and the emission are all GUI-thread work.

**5. "If your verdict is ALREADY-FIXED: the code (file:line) that shows it."** See "The
fix that is on the branch": `plugins/SlicerT/SlicerT.cpp:402-419` (handoff and poll),
`:204-274` (render path publishes only), `plugins/SlicerT/SlicerTView.cpp:60-69` (GUI
poll), `tests/src/core/SlicerTPlayheadTest.cpp:289` (the assertion that fails in ~3 s
if anyone puts the emission back).

**6. "Sibling commands in the command files you changed."** No command file changed:
both commits from round 1 touch only `docs/qwen-fixes-2026-10/cluster-4468.md`
(`git show --stat 113a6c78a7`, `git show --stat 69f4a09bfd`), so the
ControlCommandsSnapshot canary does not move. The ids the check lists (`app.*`,
`arrangement.*`, ...) are the train's diff between FIX-BASE `050/m0-train-35` and HEAD,
not this session.

**7. No test was weakened, and nothing was removed.** The round-1 review flagged a
removed assertion. There is none in this branch's history for this test: the file has
an empty diff between the merge base and HEAD, so no assertion was deleted or relaxed
anywhere along the way,
`git diff --stat fa4a5f2e78..HEAD -- tests/src/core/SlicerTPlayheadTest.cpp` prints
nothing (EXIT 0), and
`git diff --name-only 21c2f3ee2e..HEAD` lists exactly one path,
`docs/qwen-fixes-2026-10/cluster-4468.md`. The test's assertions at `:289`, `:292` and
its other `QVERIFY`/`QCOMPARE` lines are the ones `b8b70371ad` committed; this session
changed no test line and no product line (final `git status --short` is empty).

**8. Missing paths the round-1 review named.** `AGENTS.md` and a handover file are not
in this worktree and are not tracked:

```
$ ls AGENTS.md plans/CLAUDE-READ-FIRST.md
ls: cannot access 'AGENTS.md': No such file or directory
ls: cannot access 'plans/CLAUDE-READ-FIRST.md': No such file or directory
$ git ls-files | grep -ciE '^AGENTS\.md$|CLAUDE-READ-FIRST'
0
```

The realtime rule the fix answers is enforced from the repository instead:
`tests/rt-safety-scope.txt:67-69` lists `SlicerT::playNoteImpl`, `acquirePlaybackSlot`
and `deleteNotePluginData` as audio-path functions, and `docs/RT-SAFETY-SWEEP.md` is the
sweep record. The proof manifest for this run is `4468.r2/logs/proof.json` (round 1's
file lived under `4468.r1/logs`, which no longer exists).

**9. Completion check.**
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 4468`
is run after this report is committed; its table and exit code are quoted in the
follow-up commit "docs(4468): record the pre-submit check for round 2".

## Other findings

- The build tree had no plugin modules built (`build/plugins/libslicert.so` absent, and
  three of the four `SlicerT` test binaries unbuilt). A test that loads a plugin module
  fails with "The plugin \"slicert\" wasn't found or could not be loaded" until
  `cmake --build build --target slicert` has run; that message looks like a code
  regression on first read.
- `SlicerTPlayheadTest.cpp:291` keeps a 120 ms bounded wall-clock wait for a real 20 ms
  poll tick. It passed every run in both rounds, but it is the one wall-clock dependency
  in the file and the first thing that would flake on a loaded machine.
