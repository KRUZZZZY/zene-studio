FIXED

## Current QA rework (Round 4)

This subsection is authoritative for the current tree; §§9-11 below retain the evidence
from earlier rounds.

Luna's latest review asked that the 24-bit normalization change and its dedicated test be
removed from this one-bug change. I restored the original divisors at
`plugins/GigPlayer/GigPlayer.cpp:1372,1385` (`0x100000000`) and removed
`gigplayer24BitSamplePlaysAtFullScale()` and its private fixture helpers. This keeps the
sample conversion behavior at the branch base. The callback handoff and the segmented-pool
fixes remain.

### Decision and current code evidence

**Verdict: FIXED (REAL).** At the branch base, `openFile()` held `m_synthMutex` while
constructing `GigInstance`, and `playImpl()` took `m_synthMutex` and `m_notesMutex`; the
pre-fix file:line evidence is in §1. In the current tree, `openFile()` prepares a private
`GigFileState` at `plugins/GigPlayer/GigPlayer.cpp:363`, then
`publishFileState()` swaps it through `m_fileState.exchange()` at `:331-339`.
`playImpl()` at `:900-912` acquires a reader reference and loads the state atomically; it
does not take a mutex. Retired states are reclaimed off the callback when no render holds a
reader reference (`:346-358`).

The deterministic regression is `gigplayerCallbackDoesNotWaitForAFileLoad()` at
`tests/src/plugins/PluginPortsMigrationTest.cpp:2955`. It pauses file preparation with the
load hook and waits for a render completion signal. The three pool regressions at `:3157`,
`:3283`, and `:3375` still cover more than 512 notes, twelve matching layers, and voice-pool
growth.

### Rework evidence

| Check | Command and working directory | Exit | Result |
|---|---|---:|---|
| Initial compile after removing the unrelated test | `cmake --build build --target zene gigplayer PluginPortsMigrationTest -j8` (worktree) | 2 | `renderGigPeak()` became unused; removed that orphan helper. `build-1-review-rework.log`. |
| Rebuild | same command | 0 | `build-2-review-rework.log`. |
| Callback and pool regressions | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigplayerCallbackDoesNotWaitForAFileLoad gigplayerPlaysMoreThan512OverlappingNotes gigplayerKeepsEveryLayerOfANote gigplayerSurvivesGlobalVoicePoolExhaustion` (`build/tests`) | 0 | All four pass; `test-3-focused-green.log`. |
| Registered test family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (`build/tests`) | 8 | QtTest totals: 78 passed, 1 failed, 2 skipped. The only failure is `nonRealtimeLadspaEffectsAreInvalid()` because the SWH Decimator fixture is unavailable; all nine GIG slots in the family pass. `test-4-family-ctest.log`. |
| Lock regression proof | Temporarily put a shared `QMutexLocker` around `openFile()` preparation and `playImpl()`, then `cmake --build build --target gigplayer PluginPortsMigrationTest -j8` | 0 | The temporary lock reproduces the finding; `build-5-lock-mutation.log`. |
| RED | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigplayerCallbackDoesNotWaitForAFileLoad` (`build/tests`) | 1 | Fails at `renderFinished` with “the audio callback waited for the GIG file that was being loaded”; `test-6-lock-mutation-RED.log`. |
| Restore and rebuild | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/red-mutation.patch` (0), `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/fix.patch` (0), then `cmake --build build --target zene gigplayer PluginPortsMigrationTest -j8` | 0 | Temporary lock removed and intended baseline sample conversion restored; `build-7-restored-green.log`. |
| GREEN | same callback test command as RED (`build/tests`) | 0 | `test-8-lock-restored-GREEN.log`. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-149-port` | 0 | All eight rows PASS, including complexity and file-length (no new item in touched files); `gate-14-feature-check-final.log`. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-149-port new-p9-149-port` | 0 | Commits, clean tree, report, test registration, and gates PASS; the five prompts are answered in this section. `submit-13-final.log`. |

The product `fix.patch` here contains only the two 24-bit divisor restorations, so reversing
that patch cannot undo the already-committed callback fix. For a meaningful RED comparison,
`red-mutation.patch` records the temporary shared-lock mutation. Reversing that mutation and
reapplying `fix.patch` returns the source to the reviewed, lock-free behavior. Both patch
files and every build/test log for this rework are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs`.

The exact patch and proof commands, with observed exits:

```sh
git diff -- plugins/GigPlayer/GigPlayer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/fix.patch                 # EXIT=0
git diff -- plugins/GigPlayer/GigPlayer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/red-mutation.patch       # EXIT=0
cmake --build build --target gigplayer PluginPortsMigrationTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/build-5-lock-mutation.log 2>&1 # EXIT=0
QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigplayerCallbackDoesNotWaitForAFileLoad > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/test-6-lock-mutation-RED.log 2>&1 # EXIT=1
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/red-mutation.patch # EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/fix.patch # EXIT=0
cmake --build build --target zene gigplayer PluginPortsMigrationTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/build-7-restored-green.log 2>&1 # EXIT=0
QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigplayerCallbackDoesNotWaitForAFileLoad > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/test-8-lock-restored-GREEN.log 2>&1 # EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-149-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/gate-14-feature-check-final.log 2>&1 # EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-149-port new-p9-149-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r5/logs/submit-13-final.log 2>&1 # EXIT=0
```

## Self-check

- **Regression RED/GREEN:** `gigplayerCallbackDoesNotWaitForAFileLoad()` at
  `PluginPortsMigrationTest.cpp:2955` fails at `renderFinished` with a temporary shared
  mutex around load preparation and `playImpl()` (EXIT 1), then passes after reversing that
  mutation and restoring the lock-free code (EXIT 0). Logs: `test-6-lock-mutation-RED.log`
  and `test-8-lock-restored-GREEN.log` in the r5 log folder.
- **Deterministic test:** the file-load hook pauses the loader at a condition-variable
  handshake; the render thread signals completion through that same test synchronization
  object. There are no sleeps or elapsed-time assertions. `QDeadlineTimer` is only a
  deadlock guard so a broken callback cannot hang the test process indefinitely.
- **Cause and sibling paths:** `openFile()` prepares a private state then publishes through
  `publishFileState()` (`GigPlayer.cpp:363,331-339`); `freeInstance()` uses that same publish
  path (`:317-323`). Render reads are protected by the reader counter in `playImpl()`
  (`:900-917`), and the other file-state readers `getCurrentPatchName()` (`:419-428`),
  `getInstrument()` (`:1698-1710`), and GUI `showPatchDialog()` (`:1907-1921`) use the same
  reference protocol. `playNoteImpl()` stores the current state identity (`:502`), and the
  render drops notes from an older state. No mutex remains on the render path.
- **Compatibility:** saved `src`, `bank`, `patch`, and `gain` attributes are unchanged; the
  24-bit divisors are restored to their branch-base values at `GigPlayer.cpp:1372,1385`.
  Pool growth retries on the existing path and adds no serialized limit or control command.
  No undo behavior changed, and the audio callback adds no allocation, free, log, or lock.
- **Verdict evidence:** this is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The branch-base
  `openFile()` and `playImpl()` mutex evidence is in §1; the current atomic handoff is at
  `GigPlayer.cpp:331-339,900-917`.
- **Registration and family:** no new files were introduced. The existing test source is
  registered in `tests/upstream-modifications.txt`; the family run had 78 passes, one
  unrelated missing-fixture failure, and two skips. All focused GIG cases passed.

new-p9-149-port: FIXED

# new-p9-149-port - GIG playback waited on blocking mutexes in the audio callback

Verdict: **FIXED** (REAL - the audio callback did lock, and it did wait for the file
loader). Round 3 keeps that fix and completes it: the two silent truncations the round-2
review found in the replacement pools (a note refused at 512 slots, a note keeping at most
eight matching samples) are removed by making both pools grow in segments, so nothing is
refused or dropped for being beyond a pool size any more.

Branch base: `07d6d02bd` (train 12). Previous rounds on this branch: `ffe21f471` (port),
`cd4b98482`, `77caf94e9`, `ded8ba316`.

## 1. Decision and evidence (pre-fix tree, `git show 07d6d02bd:plugins/GigPlayer/GigPlayer.cpp`)

The finding's `GigPlayer.cpp:329-330,481-482` is the lock pair at the older numbering; on
the branch base the same code is:

| Pre-fix evidence | What it did |
|---|---|
| `GigPlayer.cpp:253` `openFile()` -> `:261` `QMutexLocker locker( &m_synthMutex )` | held `m_synthMutex` while `new GigInstance(...)` read the whole file |
| `GigPlayer.cpp:370` `playImpl()` -> `:378` `m_synthMutex.lock()`, `:379` `m_notesMutex.lock()` | the audio callback waited for the loader, and for every note edit, once per period |
| `GigPlayer.cpp:339` `playNoteImpl()` -> `:360` `m_notesMutex.lock()` | note-on waited on the audio thread |
| `GigPlayer.cpp:1053` `deleteNotePluginData()` -> `QMutexLocker` | note-off waited on the audio thread |

So the finding is REAL: a GIG file load or a note edit blocked the render callback, which
is an underrun by construction, not a theoretical race.

## 2. Fix, part 1 (round 1, still in place): the callback takes no lock

* `plugins/GigPlayer/GigPlayer.cpp:363` `openFile()` prepares a whole `GigFileState` (file
  + selected instrument) with no lock, and `publishFileState()` publishes it with one
  `m_fileState.exchange()`; the loader never waits for the callback.
* `plugins/GigPlayer/GigPlayer.h:464` `std::atomic<GigFileState*> m_fileState` and `:469`
  `std::atomic<int> m_fileReaders`; `reapRetiredStates()` deletes a replaced state only when
  no audio thread is inside a render, so the file is never freed under the callback.
* `plugins/GigPlayer/GigPlayer.cpp:900` `playImpl()` takes no lock: the file state is one
  atomic load, the notes live in published pool segments, and a key release is one atomic
  store per note slot.
* `plugins/GigPlayer/GigPlayer.cpp:1453` `deleteNotePluginData()` only publishes that
  release request (one atomic store into a published slot); the audio thread picks it up.
* The test seam is `GigInstrument::setFileLoadHook()` (`GigPlayer.cpp:275`) and its export
  `lmms_gigplayer_set_file_load_hook()` (`:284`). The hook is called inside the `try` block
  of `openFile()` (`:340`-`:344`), which is exactly the block the old code wrapped in
  `QMutexLocker( &m_synthMutex )`, so a test that pauses there pauses where the old lock was
  held. Regression: `gigplayerCallbackDoesNotWaitForAFileLoad()`.

## 3. Fix, part 2 (round 3, this round): the pools grow instead of truncating

The round-2 review asked for the removal of two silent truncations the replacement pools had
introduced, without giving up realtime safety, plus three regressions. Both pools are now
**segmented and growable**.

### Structures (`plugins/GigPlayer/GigPlayer.h`)

| Item | Line | Purpose |
|---|---|---|
| `GigNoteSegmentShift = 9` (512 slots/segment) | :100 | note-segment size |
| `GigVoiceSegmentShift = 6` (64 voices/segment) | :104 | voice-segment size |
| `MaxGigSegments = 1024` | :112 | bounds memory, not behaviour (1024 segments = 524288 notes or 65536 voices) |
| `InitialGigNoteSegments = 1`, `InitialGigVoiceSegments = 8` | :115, :116 | starting sizes, explicitly not ceilings |
| `struct GigNoteSlot` | :286 | carries `firstVoice`/`lastVoice`/`voiceCount`, the intrusive `orderPrev`/`orderNext`, the release request atomics and `releasePassPending`; **no `voices[8]` and no per-note cap** |
| `struct GigNoteSegment` | :327 | `noteSlots[]` + `freeSlots[]` + `freeCount`, built complete before publication |
| `struct GigVoice` | :345 | one `GigSample` plus its intrusive `nextInNote`/`prevInNote` |
| `struct GigVoiceSegment` | :359 | `voices[]` + `freeVoices[]` + `freeCount` |
| `std::array<std::atomic<GigNoteSegment*>, MaxGigSegments> m_noteSegments` | :485 | published note segments |
| `std::array<std::atomic<GigVoiceSegment*>, MaxGigSegments> m_voiceSegments` | :504 | published voice segments |
| `QTimer m_poolGrowthTimer` | :518 | drives the growth service on the instrument's own thread |

### Behaviour (`plugins/GigPlayer/GigPlayer.cpp`)

* `:514` `buildInitialPools()` runs in the constructor, before the audio thread can claim
  anything, and builds one note segment and eight voice segments. Every `GigSample` and its
  resampler therefore already exists when a note starts.
* `:524` `growNotePool()` and `:554` `growVoicePool()` allocate a whole segment, fill its
  free list, and only then `store(..., release)` it and bump the segment count. A reader
  never sees a free list that is still being filled.
* `:588` `servicePoolGrowth()` is the only place that grows. It returns immediately when
  `RealtimeScope::isActive()` or when it is not on the instrument's own thread, so it cannot
  allocate on the audio thread even if a test calls it from there; the constructor connects
  it to `QTimer::timeout` and starts the timer at 25 ms.
* `:643` `acquireNoteSlot()` and `:705` `acquireVoice()` pop a prepared index from a
  published free list; a full pool only returns `GigNoIndex`. They allocate nothing.
* `:481` `playNoteImpl()` asks for another note segment (`requestNotePoolGrowth()`) and
  leaves the note to be offered again on the next period instead of refusing it. This is
  the round-2 cap at the old `:443-449`, removed.
* `:744` `appendToNoteOrder()` / `:764` `removeFromNoteOrder()` keep the playing order as an
  intrusive list through the slots (`m_noteOrderHead`/`m_noteOrderTail`), so starting or
  ending a note never makes a container grow on the audio thread. This also removes the old
  order-index wrap the previous test only just reached.
* `:826` `appendVoiceToNote()` / `:834` `releaseVoiceOfNote()` keep each note's voices as a
  doubly linked list through the pool: a note may take one voice per matching region, with
  **no per-note ceiling**. This is the round-2 cap at the old `:1194-1204`, removed.
* `:1495` `addSamples()` attaches a voice for every matching region of the file. When a
  voice cannot be had it calls `rollBackVoices()` (`:794`) and reports `false`, so a pass is
  all-or-nothing and no layer is silently skipped. `playImpl()` retries that pass on a later
  period (`:1007`-`:1021` for the release pass, `:1022`-`:1033` for the key-down pass), and
  keeps a note that is still waiting for its pass instead of deleting it as "ended".
* `:900` `playImpl()` walks the note order and each note's voice list by index, and calls
  `noteSlot()`/`voice()` (`:620`/`:631`) - an acquire load plus an offset.

Realtime rule: the audio-thread functions (`playImpl`, `playNoteImpl`,
`deleteNotePluginData`, `addSamples` and the pool helpers) only claim, release and link
prepared objects. They never allocate, free, lock, wait, log or grow a container. Growth
happens only in `servicePoolGrowth()` on the instrument's thread, and a segment is never
moved or freed while the instrument lives.

## 4. Regressions (all three new, all behavioural)

The measurements are levels, not counters or timings: each case renders audio and compares
the summed |L|+|R| of the render, so it fails when the notes or layers it asked for are not
in the output.

| Regression | Line | What it proves |
|---|---|---|
| `gigplayerPlaysMoreThan512OverlappingNotes()` | `PluginPortsMigrationTest.cpp:3334` | 512 notes and 600 notes are rendered; the 600-note render must be more than 1.1x the 512-note render. A 512-slot pool gives exactly the 512-note level for both. Uses `dc.gig`, whose constant level makes the sum exactly proportional to the voices that played whatever period each started in. |
| `gigplayerKeepsEveryLayerOfANote()` | `:3460` | one note on `layered12.gig` (twelve regions, all matching the default key) must reach more than 9x the level of the same note on `dc.gig` (one region). The old eight-voice cap lands at 8x; the fixed code at 12x. |
| `gigplayerSurvivesGlobalVoicePoolExhaustion()` | `:3552` | 512 one-voice notes empty the starting voice pool, then 64 more notes are offered; the 576-note render must exceed 1.06x the 512-note render. A pool that dropped the notes that found it empty adds exactly nothing. |

The existing GIG cases were not weakened; they all still pass:
`gigplayerKeepsEveryOverlappingNote` (`:3251`, still checks the 129th and 256th note are
audible), `gigplayerAudioPathDoesNotAllocate` (`:3085`), `gigplayerCallbackDoesNotWaitForAFileLoad`
(`:3132`), `gigOneFrameReleaseSampleIsAudible`, `gigPlayerKeepsReleaseTriggerAcrossRegions`,
`gigplayerBidirectionalLoopReflectsAtExclusiveEnd`, `gigplayerSingleFrameLoopRepeatsFrame`,
`gigKeyboardDimensionUsesPressedKey`, `gigplayerNoProgressFallbackKeepsEarlierSamples`,
`gigPatchDialogPreviewsSelectedProgram`.

### Fixtures

`tests/data/gigproof/make_gig.cpp` gained two modes, preserving every existing mode:
`--layers N` (N regions all covering the same key, each with its own sample) and `--dc`
(a constant-level sample). Both new fixtures are generated by that generator:

* `tests/data/gigproof/dc.gig` — `make_gig --dc`
* `tests/data/gigproof/layered12.gig` — `make_gig --dc --layers 12`

They are registered in `tests/upstream-modifications.txt` (with a reason each) and
documented in `tests/data/gigproof/README.md`.

## 5. RED / GREEN proof (4b), with exit codes

The whole product diff was written to `logs/fix.patch`, reverse-applied, rebuilt, tested
(RED), then re-applied, rebuilt and tested (GREEN). Every command and exit code:

```
$ git diff -- plugins/GigPlayer/GigPlayer.cpp plugins/GigPlayer/GigPlayer.h > $LOGS/fix.patch
$ git apply -R --check $LOGS/fix.patch                                      EXIT=0
$ git apply -R $LOGS/fix.patch                                              EXIT=0   (logs/4b-apply-reverse.log)
$ cmake --build build --target gigplayer -j8                                EXIT=0   (logs/build-10-reverted-module.log)
$ cmake --build build --target PluginPortsMigrationTest -j8                 EXIT=0   (logs/build-11-reverted-test.log)
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest \
      gigplayerPlaysMoreThan512OverlappingNotes \
      gigplayerKeepsEveryLayerOfANote \
      gigplayerSurvivesGlobalVoicePoolExhaustion                            EXIT=3   (logs/test-6-RED.log)
```

The RED run fails all three for the intended behavioural reason, not for a missing symbol
(the growth-service seam is resolved at run time, and the cases work without it):

```
FAIL!  : gigplayerPlaysMoreThan512OverlappingNotes() 'pastCap > 1.1 * atCap' returned FALSE.
         (the notes past 512 were dropped: 512 notes summed 2742906.8, 600 notes summed 2742906.8)
FAIL!  : gigplayerKeepsEveryLayerOfANote() 'twelve > 9.0 * one' returned FALSE.
         (the note kept only a fraction of its matching layers: one layer summed 1785.7406,
          twelve matching regions summed 14285.924; the eight-voice cap is back)
FAIL!  : gigplayerSurvivesGlobalVoicePoolExhaustion() 'withLate > 1.06 * fillersOnly' returned FALSE.
         (the notes that found the voice pool empty never played: 512 notes summed 1828604.5,
          512 notes plus 64 after the pool grew summed 1828604.5)
Totals: 2 passed, 3 failed
```

Note the first and third RED numbers: 512 and 600 notes (and 512 and 576 notes) render
**exactly the same** level on the old code, which is the silent cap itself. The second is
8/12 of the one-layer level, i.e. exactly the eight-voice cap.

Restore and GREEN:

```
$ git apply $LOGS/fix.patch                                                 EXIT=0   (logs/4b-apply-forward.log)
$ cmake --build build --target zene gigplayer -j8                           EXIT=0   (logs/build-12-restored.log)
$ cmake --build build --target PluginPortsMigrationTest -j8                 EXIT=0   (logs/build-13-restored-test.log)
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest \
      gigplayerPlaysMoreThan512OverlappingNotes \
      gigplayerKeepsEveryLayerOfANote \
      gigplayerSurvivesGlobalVoicePoolExhaustion                            EXIT=0   (logs/test-7-GREEN-restored.log)
Totals: 5 passed, 0 failed
```

The full GIG family GREEN run (13 passed, 0 failed):

```
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest <11 GIG cases>
                                                                            EXIT=0   (logs/test-5-gig-green.log)
Totals: 13 passed, 0 failed, 0 skipped
```

The locking fix from round 1 has its own RED/GREEN on record from that round (a temporary
shared mutex restored the blocking handoff and made `renderFinished` fail), and its case
still passes here.

## 6. Gates

Run one command at a time, each logged:

```
$ cmake --build build --target zene -j8                                     EXIT=0   (logs/build-9-clean.log)
$ cmake --build build --target PluginPortsMigrationTest -j8                 EXIT=0   (logs/build-9-clean.log)
$ bash tests/fork-sources-gate.sh                                           EXIT=0   (logs/gates/fork-sources.log)
$ bash tests/all-sources-reproduce.sh                                       EXIT=0   (logs/gates/all-sources-reproduce.log, REPRODUCES)
$ bash tests/no-upstream-regression-gate.sh                                 EXIT=0   (logs/gates/no-upstream-regression.log)
$ python3 tests/scripted/check-namespace                                    EXIT=0   (logs/gates/check-namespace.log)
$ bash tests/no-tautology-gate.sh                                           EXIT=0   (logs/gates/no-tautology.log)
$ python3 tests/doc-figures-gate.py                                         EXIT=0   (logs/gates/doc-figures.log)
$ python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt \
      --allowlist tests/rt-safety-allowlist.txt --check                     EXIT=0   (PASS, 0 fresh)
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure          EXIT=0   (logs/ctest-rtsafety.log)
$ bash tests/complexity-gate.sh --check                                     EXIT=1   (logs/gates/complexity.log)
$ bash tests/file-length-gate.sh --check                                    EXIT=1   (logs/gates/file-length.log)
```

The two ratchet gates are red on the train on purpose. Judged by `feature_check.py`'s own
rule (an item fails only when it is NEW against the cached base listing **and** lives in a
file this branch changed), both rows pass:

```
base 07d6d02bd, 11 changed files
complexity:  total=7  new-vs-base=0  in-my-files=0  -> PASS
file-length: total=19 new-vs-base=0  in-my-files=0  -> PASS
RESULT: PASS
```

No `REGRESSION` line of either gate names `plugins/GigPlayer/...`,
`tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/data/gigproof/...` or this report.
The 7 complexity items are in `src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`,
`src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
`src/core/ControlCommandsSessionRecordLand.cpp`, `src/core/ControlCommandsMixerStrip.cpp`
and `src/gui/ControlCommandsShortcuts.cpp`; the 19 file-length items are in files this
branch did not touch.

### The rt-safety sweep and this change

Both audio-thread paths the earlier round declared were already in
`tests/rt-safety-scope.txt:162-163`. Because this round adds pool helpers that also run on
the audio thread, sixteen more of them are now **declared** (`noteSlot`, `voice`,
`acquireNoteSlot`, `acquireVoice`, `releaseNote`, `releaseVoice`, `appendToNoteOrder`,
`removeFromNoteOrder`, `appendVoiceToNote`, `releaseVoiceOfNote`, `rollBackVoices`,
`requestNotePoolGrowth`, `requestVoicePoolGrowth`, `clearNotes`, `addSamples`,
`deleteNotePluginData`), each with the reason it cannot allocate. The sweep then measures
**4060 region lines** across 125 declared pairs and reports
`allocation 0, locking 2, growth 2, blocking-io 0`, all four hits being the three inherited
upstream entries already in `tests/rt-safety-allowlist.txt`; `0 key(s) fresh` -> **PASS**.
So the claim is not "the sweep passed because it did not look": the new paths are in the
declared frontier and they measure clean.

### `feature_check.py` and the sandbox

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot write its
log directory in this session: it writes to `<worktree>/../<name>-feature-check`, which is
outside the writable set, and aborts before running a gate
(`PermissionError: [Errno 13] Permission denied: .../zene-qwen-new-p9-149-port-feature-check`,
`logs/feature-check-1.log`, EXIT=1). The directory exists with mode `0777` and is owned by
this user, and even `touch` inside it, in its parent, or in the mandated run log folder
returns `EACCES`; this is an environment fault, not a permission I can fix.

Every gate was also run individually (table above, all six SIMPLE gates EXIT=0), and the two
RATCHET rows were evaluated with the script's own `key()`/`item_file()` logic against its
cached `gate-baselines/07d6d02bd*.json` — the same computation the script performs.

Finally, the **unmodified script logic** was run with nothing changed but its two paths: its
`ROOT` (so it reads the real `FIX-BASE`/`gate-baselines`) and its log directory (pointed at
`$LOGS/fc`, since its own target is unwritable). `diff` against the shipped script shows
exactly those two lines and nothing else:

```
$ diff ~/.local/share/zene-bunny-fix/qwen/feature_check.py $LOGS/fc/feature_check_writable.py
19c19
< ROOT = Path(__file__).resolve().parent
---
> ROOT = Path("/home/kruzzzzy/.local/share/zene-bunny-fix/qwen")
70c70
<     logs = wt.parent / f"{wt.name}-feature-check"
---
>     logs = Path("/home/kruzzzzy/.cache/np9149-r3-logs/fc")
$ python3 $LOGS/fc/feature_check_writable.py <worktree>          EXIT=0   (logs/fc/gate-2.log)
feature-check zene-qwen-new-p9-149-port (base 07d6d02bd, 11 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

`RESULT: PASS`, EXIT=0. This first became green **after the round-3 commit**: on the
uncommitted tree the script classified the new files as untracked, so this table was
produced on the committed tree, which is the state the submitted branch is in.

## 7. Collateral damage

* Saved data is unchanged: the attributes are still `src`, `bank`, `patch`, `gain`
  (`saveSettings`, `GigPlayer.cpp:209`), and `loadSettings` (`:221`) opens the file the same
  way. An old project loads with the same instrument and the same patch.
* Nothing in the new pools changes rendering: `migratedPluginsPreserveBehaviour` still
  requires exactly the frozen reference output, and passes.
* `PluginPortsMigrationTest` as a whole: **79 passed, 1 failed, 2 skipped** (EXIT=1,
  `logs/test-8-full-migration.log`). The single failure is the pre-existing
  `nonRealtimeLadspaEffectsAreInvalid()` ("SWH Decimator test fixture was not loaded"), a
  host that has no `decimator_1202.so`; it needs no GIG file and is unrelated to this
  change, and it failed identically in round 2.
* The limits that remain are explicit and harmless: `MaxGigSegments` bounds memory at
  524288 notes / 65536 voices, far beyond any arrangement, and a request past it simply
  retries. No caller legitimately exceeds it.

## 8. Files changed in this round

* `plugins/GigPlayer/GigPlayer.h` — segmented pool types, constants, members, helpers.
* `plugins/GigPlayer/GigPlayer.cpp` — segmented acquire/release, control-thread growth
  service, intrusive note/voice lists, all-or-nothing `addSamples()` with rollback and
  retry, `deleteNotePluginData()` scanning published segments, and the
  `lmms_gigplayer_service_pool_growth` test seam.
* `tests/src/plugins/PluginPortsMigrationTest.cpp` — the three regressions.
* `tests/data/gigproof/make_gig.cpp`, `tests/data/gigproof/README.md`,
  `tests/data/gigproof/dc.gig`, `tests/data/gigproof/layered12.gig` — the two fixtures and
  the generator modes.
* `tests/rt-safety-scope.txt` — sixteen new declared audio-thread helpers.
* `tests/upstream-modifications.txt` — registration of the inherited files changed and of
  the two new fixtures (`fork-ADDED`), each with a reason.
* `docs/qwen-fixes-2026-10/cluster-new-p9-149-port.md` — this report.

## 9. Self-check (answers to every submit_check.py "?")

**"The regression test FAILS with your product change reverted and PASSES with it (exit
codes)."** — Yes, all three, with the product diff reverse-applied (`logs/fix.patch`,
`git apply -R` EXIT=0) and rebuilt: the three cases exit **3** (3 failed) with exactly the
cap messages quoted in §5 — 512 and 600 notes render the *identical* level, and the layered
note keeps 8/12 of its level. With the patch re-applied and rebuilt they exit **0**
(5 passed, `logs/test-7-GREEN-restored.log`). The cases fail on their level assertions, not
on a missing export: the growth-service seam is looked up at run time and skipped when
absent, so the same binary shape runs in both builds.

**"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses)."** — No sleep, no deadline, no wall-clock measurement. The seam
is `lmms_gigplayer_service_pool_growth` (`GigPlayer.cpp:297`), which calls
`servicePoolGrowthForTest()` (`:609`) — the same function the 25 ms `QTimer` calls in the
product — and the test calls it **between periods, on the thread that rendered**, exactly
where the control thread would. `servicePoolGrowth()` refuses to run inside a
`RealtimeScope` or off the instrument's thread, so the seam cannot accidentally allocate on
the audio thread. The assertions are ratios of summed render levels with fixed note counts
(512/600, 1 vs 12 layers, 512/576) and no dependence on sample rate, period size or
scheduling. The old locking regression uses the file-load hook plus a `QMutex`/
`QWaitCondition` pair and asserts the *order* of two synchronised events, with its 10 s
deadline only as a deadlock breaker.

**"The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent."** — Cause: the callback took locks (removed in round 1) and
the replacement pools capped counts (removed here). Every reader of the published file
state takes the reader reference (`getCurrentPatchName`, `playImpl`, `getInstrument`, the
patch dialog) and every writer goes through `publishFileState()` (`openFile`,
`freeInstance`). Every writer of note state is now accounted for: `playNoteImpl` claims a
slot, `playImpl` claims/releases voices and notes, `clearNotes` walks the intrusive order,
`deleteNotePluginData` only publishes a release request. Every accessor that previously
indexed a fixed array (`m_noteSlots`, `m_voices`, `m_noteOrder`, `m_freeNotes`,
`m_freeVoices`, `MaxGigNotes`, `MaxGigVoices`, `MaxGigSamplesPerNote`) was converted;
`grep` finds none of those symbols left in either product file. The two writers of the
pools are separated by thread: only the audio thread claims and releases, only
`servicePoolGrowth()` (instrument thread) appends, and a segment is complete before its
pointer is published with release ordering.

**"No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it."** — The saved
attributes (`src`, `bank`, `patch`, `gain`) and `loadSettings`/`saveSettings` are untouched,
so an old project loads with the same file and patch. Rendering is unchanged for everything
within the old bounds: `migratedPluginsPreserveBehaviour` (sample-exact against the frozen
reference) passes, as do all the pre-existing GIG regressions. The one limit added,
`MaxGigSegments = 1024`, is 1024 segments of 512 notes / 64 voices (524288 notes or 65536
voices), a memory bound that no project reaches; a request beyond it retries rather than
refusing or corrupting. Full binary: 79 passed, 1 pre-existing unrelated failure (missing
SWH Decimator LADSPA fixture), 2 skipped.

**"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it."** — Not applicable: the verdict is **FIXED**. The pre-fix locking
code is quoted with line numbers in §1 from
`git show 07d6d02bd:plugins/GigPlayer/GigPlayer.cpp`; it is real, and it was in the audio
callback path.

**Realtime, undo and commands.** The audio-thread functions add no allocation, free, lock,
wait, log or container growth: the sweep declares them and measures no allocation, no
locking and no growth for the new ones (§6), and `gigplayerAudioPathDoesNotAllocate` still
counts zero allocations and zero frees across repeated note starts on the render path. No
control command was added or changed, and no undo step was introduced or altered.

## 10. Commands (all logs in `$LOGS`)

```
$LOGS = /home/kruzzzzy/.cache/np9149-r3-logs
```

The run's mandated log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-149-port.r3/logs` is not
writable in this session (every write returns `EACCES`, including after `chmod 777`; the
parent `runs/` and `qwen/` deny writes too, while the worktree and `~/.cache` are writable).
The logs of this round are therefore in `$LOGS`, with the names used throughout this report;
they are copied into the mandated folder by whoever has write access there. This is stated
plainly rather than claimed otherwise.

```
$ cmake --build build --target zene PluginPortsMigrationTest -j8            EXIT=0   build-9-clean.log
$ cd build/tests && ./PluginPortsMigrationTest <11 GIG cases>               EXIT=0   test-5-gig-green.log
$ git diff -- plugins/GigPlayer/GigPlayer.cpp plugins/GigPlayer/GigPlayer.h > fix.patch
$ git apply -R --check fix.patch                                           EXIT=0
$ git apply -R fix.patch                                                   EXIT=0   4b-apply-reverse.log
$ cmake --build build --target gigplayer -j8                               EXIT=0   build-10-reverted-module.log
$ cmake --build build --target PluginPortsMigrationTest -j8                EXIT=0   build-11-reverted-test.log
$ cd build/tests && ./PluginPortsMigrationTest <the three new cases>       EXIT=3   test-6-RED.log
$ git apply fix.patch                                                      EXIT=0   4b-apply-forward.log
$ cmake --build build --target zene gigplayer -j8                          EXIT=0   build-12-restored.log
$ cmake --build build --target PluginPortsMigrationTest -j8                EXIT=0   build-13-restored-test.log
$ cd build/tests && ./PluginPortsMigrationTest <the three new cases>       EXIT=0   test-7-GREEN-restored.log
$ cd build/tests && ./PluginPortsMigrationTest                             EXIT=1   test-8-full-migration.log (79 passed, 1 pre-existing)
$ bash tests/fork-sources-gate.sh                                          EXIT=0   gates/fork-sources.log
$ bash tests/all-sources-reproduce.sh                                      EXIT=0   gates/all-sources-reproduce.log
$ bash tests/no-upstream-regression-gate.sh                                EXIT=0   gates/no-upstream-regression.log
$ python3 tests/scripted/check-namespace                                   EXIT=0   gates/check-namespace.log
$ bash tests/no-tautology-gate.sh                                          EXIT=0   gates/no-tautology.log
$ python3 tests/doc-figures-gate.py                                        EXIT=0   gates/doc-figures.log
$ python3 tests/rt-safety-sweep.py --check                                 EXIT=0   (PASS, 0 fresh)
$ cd build/tests && ctest -R '^RtSafetySweep$'                             EXIT=0   ctest-rtsafety.log
$ bash tests/complexity-gate.sh --check                                    EXIT=1   gates/complexity.log
$ bash tests/file-length-gate.sh --check                                   EXIT=1   gates/file-length.log
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>    EXIT=1   feature-check-1.log (sandbox: unwritable log dir)
$ python3 $LOGS/fc/feature_check_writable.py <worktree>                    EXIT=0   fc/gate-2.log (RESULT: PASS)
$ python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-149-port
                                                                           EXIT=1   submit-2.log (only the sandbox gate row)
```

## 11. Completion check (`submit_check.py`)

On the committed tree, `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree>
new-p9-149-port` reports (`logs/submit-2.log`, EXIT=1):

```
PRE-SUBMIT CHECK for new-p9-149-port
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p9-149-port.md
  PASS  tests added or changed - tests/data/gigproof/make_gig.cpp, tests/src/plugins/PluginPortsMigrationTest.cpp
  FAIL  gates (feature_check.py)
RESULT: FAIL - 1 item(s) to fix before submitting
```

The single FAIL is the sandbox, not a gate: `submit_check.py` launches the shipped
`feature_check.py`, which aborts with `EACCES` writing `<worktree>/../<name>-feature-check`
before it runs a single gate. The identical script with only that log path redirected (and
`ROOT` pinned to the real install directory, §6, two-line `diff` shown there) runs all eight
gates on this same committed tree and prints `RESULT: PASS`, EXIT=0
(`logs/fc/gate-2.log`). Every gate also exits 0 when run by hand (§6).

The five ASK items are answered in §9; no ALREADY-FIXED/FALSE-POSITIVE answer applies
because the verdict is FIXED.
