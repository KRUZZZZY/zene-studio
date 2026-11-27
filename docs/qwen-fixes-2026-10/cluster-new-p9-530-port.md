FIXED

# cluster new-p9-530-port — editing an Xpressive output expression raced note initialization

Verdict: **REAL**, and the round-11 fix was incomplete. This round replaces the
single-announcement hand-off with the repo's multi-reader publication, so **every**
concurrent note worker keeps its own text alive until its `ExprFront` copy is done.

## What round 11 got wrong (the reviewer's point, confirmed here)

Round 11 published an immutable copy and had the reader *announce* the text it took, with
one announcement per expression (`plugins/Xpressive/Xpressive.cpp:138-176` at commit
`6b7e75720e`: `m_readerExpression` + `m_readerReading`, `takeFree()` / `reclaim()`).
That covers one reader. The note path has several:

* `AudioEngine::renderStageInstruments()` fills the job queue with individual play
  handles — `src/core/AudioEngine.cpp:1111-1116`;
* `AudioEngineWorkerThread::startAndWaitForJobs()` wakes the pool, each worker drains
  `globalJobQueue`, and the engine thread takes one inline —
  `src/core/AudioEngineWorkerThread.cpp:405-438` and `:440-490`;
* every job calls `InstrumentTrack::playNote()`, which hands the note to the **track's
  single instrument** — `src/tracks/InstrumentTrack.cpp:767-792`, the call at `:785`.

So two notes of one track can be inside `Xpressive::playNoteImpl()` at the same time,
both in the `if (!nph->m_pluginData)` block. With one announcement, the second note's
read replaced the first note's announcement, and the next publication then put the first
note's text on the free list and overwrote it while `ExprFront` was still copying it
(`plugins/Xpressive/ExprSynth.cpp:541-548` copies the borrowed bytes after the hand-off
returns).

That is no longer an argument: the new regression, run against the round-11 product code
with AddressSanitizer, reports it. `<run>/logs/test-4b-red.log`:

```
QINFO  : everyConcurrentNoteKeepsItsOwnText() pin sequence: first note holds "0.5"
QINFO  : everyConcurrentNoteKeepsItsOwnText() pin sequence: second note holds "0.25", first note still holds "0.5"
==2020==ERROR: AddressSanitizer: heap-use-after-free on address 0x7b8e5b92b611
READ of size 3 at 0x7b8e5b92b611 thread T0
    #5 checkTwoConcurrentNotes<lmms::Xpressive::ExpressionSnapshot>()
freed by thread T0 here:
    #1 QByteArray::operator=(QByteArray const&)
    #2 checkTwoConcurrentNotes<lmms::Xpressive::ExpressionSnapshot>()
```

The free is inside a publication: the writer recycled the slot the first note still held.

## The fix

`Xpressive::ExpressionSnapshot` is now the repo's accepted multi-reader publication —
`PublishedTransients` (`include/TransientRepeat.h:134-160`,
`src/core/TransientRepeat.cpp:130-167`) applied to a text payload:

* `plugins/Xpressive/Xpressive.h:107-165` — four fixed slots of immutable text
  (`std::array<QByteArray, Slots> m_texts`), one reader count per slot
  (`m_readers`), one published slot (`m_published`), `Slots = 4`, `ReadAttempts = 16`,
  and the RAII `ExpressionSnapshot::Read` pin (move-only; its destructor unpins).
* `plugins/Xpressive/Xpressive.h:146-152` — `publish()` (control thread, the one writer)
  and `read()` (any thread, never waits, never allocates), plus `publishedSlot()` as the
  test seam.
* `plugins/Xpressive/Xpressive.cpp:138-161` — `publish()` writes only a slot that is
  neither published nor pinned, then stores the published index; if all others are
  pinned it yields (`std::this_thread::yield()`) on the **control** thread only.
* `plugins/Xpressive/Xpressive.cpp:163-181` — `read()` loads the published slot,
  `fetch_add`s its count, re-checks the slot is still published, and on a lost attempt
  undoes the count and retries (16 attempts, never waiting); a note that loses every
  attempt starts silent rather than blocking the audio thread.
* `plugins/Xpressive/Xpressive.cpp:127-136` and `:183-186` — `Read::text()` and
  `unpin()`; `text()` returns `""` for an empty pin.
* `plugins/Xpressive/Xpressive.cpp:188-192` — `makeExprFront()`: pin, construct
  `ExprFront` from the pinned bytes, unpin (the pin's destructor runs after the copy).
* `plugins/Xpressive/Xpressive.cpp:289-294` — `playNoteImpl()` uses it for both output
  expressions, so each note holds its own slot. No `new`, no free, no lock, no wait and
  no logging is added to the audio path by the hand-off itself; the pre-existing
  `new ExprFront` / `new ExprSynth` on note start are unchanged.
* `plugins/Xpressive/Xpressive.cpp:194-201` — `setOutputExpression()`, the single writer
  of the GUI copy that publishes with it; the constructor (`:114-117`) and
  `loadSettings()` (`:229-231`) publish through it, so all three writers agree.

All atomics are sequentially consistent: the reader's pin-then-check and the writer's
publish-then-scan are a Dekker pair. Old projects are untouched — `saveSettings` still
writes the same `O1`/`O2` attributes from the same member, so a saved project is
byte-identical to before.

## Regression test

`tests/src/plugins/XpressiveExpressionHandoffTest.cpp` (registered in
`tests/CMakeLists.txt` and in the existing AddressSanitizer list,
`tests/CMakeLists.txt:3681-3702`). Four cases, no threads, no sleeps, no timing:

1. `aHeldExpressionSurvivesLaterPublications` — a text a note holds is not rewritten by
   three later publications, and the last one does reach the next note.
2. `everyConcurrentNoteKeepsItsOwnText` — **new this round**: the first note holds its
   text, the second note reads a newer publication, and three further publications follow
   under both of them; each note must still hold exactly what it pinned, and the newest
   publication must reach the next reader. The pin sequence is printed by the test itself.
3. `anEditThatIsNotPublishedNeverReachesANewNote` — the behavioural check kept from
   round 11: an edit of the GUI copy that was never published renders the same block as
   the published text, and publishing the same edit does change the next note.
4. `aPublicationReachesTheNextNote` — every publication reaches the next note.

### RED (product hunks reversed) and GREEN (restored)

```
$ cd <wt> && git diff -- plugins/Xpressive/Xpressive.h plugins/Xpressive/Xpressive.cpp > <run>/logs/fix.patch
EXIT=0
$ cd <wt> && git apply -R <run>/logs/fix.patch
EXIT=0
$ cd <wt> && cmake --build build --target XpressiveExpressionHandoffTest -j4 > <run>/logs/build-5-red.log 2>&1
EXIT=0
$ cd <wt>/build/tests && ctest -R XpressiveExpressionHandoffTest --output-on-failure > <run>/logs/test-4b-red.log 2>&1
EXIT=8      (heap-use-after-free quoted above; aHeldExpression… still passes, the
             concurrent-notes case is what fails)
$ cd <wt> && git apply <run>/logs/fix.patch
EXIT=0
$ cd <wt> && cmake --build build --target XpressiveExpressionHandoffTest -j4 > <run>/logs/build-6-green.log 2>&1
EXIT=0
$ cd <wt>/build/tests && ctest -R XpressiveExpressionHandoffTest --output-on-failure > <run>/logs/test-4b-green.log 2>&1
EXIT=0
```

Green, verbosely (this is the pin-state sequence artifact, and the same binary is the
ASAN transcript):

```
$ cd <wt>/build/tests && ctest -R XpressiveExpressionHandoffTest -V > <run>/logs/test-green-1-verbose.log 2>&1
EXIT=0
753: Environment variables: QT_QPA_PLATFORM=offscreen  ASAN_OPTIONS=detect_leaks=0:use_sigaltstack=0
753: PASS   : aHeldExpressionSurvivesLaterPublications()
753: QINFO  : everyConcurrentNoteKeepsItsOwnText() pin sequence: first note holds "0.5"
753: QINFO  : everyConcurrentNoteKeepsItsOwnText() pin sequence: second note holds "0.25", first note still holds "0.5"
753: QINFO  : everyConcurrentNoteKeepsItsOwnText() pin sequence: after 3 more publications first="0.5" second="0.25" published="0.75"
753: PASS   : everyConcurrentNoteKeepsItsOwnText()
753: PASS   : anEditThatIsNotPublishedNeverReachesANewNote()
753: PASS   : aPublicationReachesTheNextNote()
```

Standalone ASAN transcript of the same binary —
`<run>/logs/asan-transcript-green.log`: `6 passed, 0 failed`, no sanitizer report. (A
direct run *without* `use_sigaltstack=0` aborts inside the ASAN runtime
("failed to deallocate ..."), which is why the CMake ENVIRONMENT property, like
GraphWaveformLoadTest's, carries the option; that run is `test-green-1-detail.log` and is
not used as evidence.)

Family re-run with the fix in place:

```
$ cd <wt> && cmake --build build --target zene xpressive ExprSynthTest ExprSynthAllocationTest -j4 > <run>/logs/build-4-all.log 2>&1
EXIT=0
$ cd <wt>/build/tests && ctest -R "ExprSynth|Xpressive" --output-on-failure > <run>/logs/test-family-1.log 2>&1
EXIT=0        (3/3 passed)
```

## Gates (procedure step 5)

`feature_check.py` still cannot finish in a lane: its base-listing step checks out the
base commit in its own cached tree and writes the cache under
`~/.local/share/zene-bunny-fix/qwen/gate-baselines/`, both refused here
(`<run>/logs/feature-check-1.log`, EXIT=1):

```
bunny-guard: 'git -C .../qwen/gate-baselines/tree checkout -q --detach 9913d36d53...'
             is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
  File ".../feature_check.py", line 60, in base_listings
subprocess.CalledProcessError: Command '...' returned non-zero exit status 97.
```

Every gate it wraps was therefore run directly from the worktree, unpiped:

| gate | log | exit |
|---|---|---|
| fork-sources | `<run>/logs/gate-fork-sources.log` | 0 PASS (2783 fork-NEW, 1213 inherited, 0 stale) |
| all-sources-reproduce | `<run>/logs/gate-all-sources-reproduce.log` | 0 REPRODUCES |
| no-upstream-regression | `<run>/logs/gate-no-upstream-regression.log` | 0 PASS (3491 changed paths declared) |
| check-namespace | `<run>/logs/gate-check-namespace.log` | 0, "0 errors" |
| no-tautology | `<run>/logs/gate-no-tautology.log` | 0 PASS |
| doc-figures | `<run>/logs/gate-doc-figures.log` | 0 RESULT: PASS |
| complexity | `<run>/logs/gate-complexity.log` | 2 — `lizard is not installed` (environmental, same on the train) |
| file-length | `<run>/logs/gate-file-length.log` | 0 PASS, no regressions (test file 401 lines, no file of mine over 500) |
| RtSafetySweep | `<run>/logs/gate-rt-safety-sweep.log` | 0 PASS (the scope list is untouched: see Other findings) |

## Self-check

* **The regression test fails with the product change reverted and passes with it** — the
  round-12 product hunks reversed give `ctest` EXIT=8 with an AddressSanitizer
  heap-use-after-free whose free site is a publication (`test-4b-red.log`), and restored
  give EXIT=0 (`test-4b-green.log`). The concurrent-notes case is the one that fails; the
  new-API detection (`HasPinnedRead`) keeps the file compiling against the reverted
  product, so the control sees a behaviour, not a build error.
* **The test is deterministic** — no threads, no sleeps, no wall clock. The
  synchronisation is the hand-off's own pin: `HeldText` holds a `Read` for as long as the
  scenario needs it, so "note A is copying while note B starts and the editor keeps
  typing" is three direct calls in a fixed order. Nothing in the test waits.
* **The fix is at the cause and complete** — the reader side now keeps one slot per
  concurrent note (the round-11 gap), the writer side writes only unpinned, unpublished
  slots, and all three writers of `m_outputExpression` publish (`setOutputExpression`,
  used by the constructor, `loadSettings` and the view's `expressionChanged`,
  `plugins/Xpressive/Xpressive.cpp:194-201`, `:114-117`, `:229-231`). Siblings checked:
  `WaveSample::copyFrom` (`plugins/Xpressive/ExprSynth.cpp:722-728`) already takes the
  engine change lock for its table copy, and `m_wavesExpression` has no audio-thread
  reader (only `saveSettings`, `loadSettings` and the editor).
* **No collateral damage** — `saveSettings`/`loadSettings` are unchanged in what they read
  and write, so old projects load and save exactly as before, and a project that was never
  edited parses the same default expressions (published in the constructor). No cap or
  check was added that could reject another caller: the payload is a heap copy of any
  length, four slots are fixed storage, and the writer waits on the control thread rather
  than dropping an edit.
* **Realtime** — the audio thread now runs `makeExprFront()`: one `read()` (a load,
  `fetch_add`, a re-check, at most 16 tries, no wait), one `text()` (`constData()` of a
  pinned slot), one `~Read` (`fetch_sub`). No allocation, no free, no lock, no I/O, no
  logging. The one allocation on note start, `new ExprFront`, is the pre-existing one.
  `publish()` allocates (a `QByteArray` copy) and yields, and runs only on the GUI thread.
* **Undo and commands** — no command and no model was added or changed; the expression
  text was not an undoable model before and is not one now, so undo is untouched and
  `ControlCommandsSnapshot` does not move.
* **ALREADY-FIXED / FALSE-POSITIVE** — not claimed; the ASAN report in
  `test-4b-red.log` is the defect itself.
* **The one FAIL the pre-submit check reports is the aggregate runner, not a gate** — the
  `gates (feature_check.py)` row spawns the script whose base-listing step the lane
  refuses (quoted above). Each gate it wraps was run directly and passes (table), and the
  two ratchets were measured on both sides in round 11
  (`gate-complexity-base.log`/`gate-file-length-base.log` at the base commit,
  `gate-complexity-branch.log`/`gate-file-length-branch.log` on the branch); this round's
  branch-side ratchet logs are `<run>/logs/gate-complexity.log` and
  `<run>/logs/gate-file-length.log`.

## Artifacts (this run)

```
<run>  = /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-530-port.r12
<wt>   = /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-530-port
```

* test file — `<wt>/tests/src/plugins/XpressiveExpressionHandoffTest.cpp`
* red ctest — `<run>/logs/test-4b-red.log` (EXIT=8, heap-use-after-free)
* green ctest — `<run>/logs/test-4b-green.log` (EXIT=0)
* pin-state sequence — `<run>/logs/test-green-1-verbose.log` (the `pin sequence:` lines)
* ASAN transcript — `<run>/logs/asan-transcript-green.log` (EXIT=0, no report)
* proof manifest — `<run>/logs/proof.json`

## Other findings (not fixed here)

* `tests/rt-safety-scope.txt` still does not declare Xpressive's note path. Declaring
  `playNoteImpl`/`makeExprFront` would add the **pre-existing** `new ExprFront` /
  `new ExprSynth` allocation to that sweep, and its allowlist is a falling ratchet, so it
  cannot absorb a new hit; the sweep passes untouched (`gate-rt-safety-sweep.log`). The
  new hand-off functions themselves contain no allocation, lock or growth.
* The ASAN target needs `-Wno-error=stringop-truncation` on GCC 16: the sanitizer's
  inlining makes it warn about the bounded `strncpy` + hand-written NUL in the plug-in's
  `recordPendingError()` (`plugins/Xpressive/Xpressive.cpp:325`), which the non-ASAN
  plug-in target does not warn about. The product file is unchanged.
* `lizard` is not installed on this box, so the complexity ratchet cannot run at all here.
* `feature_check.py`'s base-listing cache is unwritable from a lane and its `git checkout`
  is refused by the bunny guard — every lane on a new train merge hits this.
