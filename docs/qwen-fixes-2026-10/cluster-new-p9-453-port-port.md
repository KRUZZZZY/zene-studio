ALREADY-FIXED

# Cluster new-p9-453-port-port: the SF2 audio path took mutexes — already fixed on this line

## What the unit asked for

The brief this run was composed from carries the cluster id and an **empty finding body**:

```json
{ "cluster": "new-p9-453-port-port", "title": "" }
```

"Where to start" is empty as well (`.../runs/new-p9-453-port-port.r1/brief.md`). The unit is the
third hop of one fix chain, and the cluster names the bug:

- `bugs/qwen-new-p9-453` — cluster `new-p9-453`, the accepted fix.
- `bugs/qwen-new-p9-453-port` — cluster `new-p9-453-port`, the first hand port; its review
  (`~/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-453-port.r1.md`) reads `ACCEPT-FIXED`.
- this branch — the same port asked again on the newest line (`FIX-BASE` is `050/m0-train-53`,
  HEAD `3436a808b6` when the work started).

The only inventory record for the cluster is `BUG-INVENTORY.jsonl` `finding_id 7683`, cluster
`new-p9-453`:

> BUG 9: The SF2 audio path takes mutexes
> where: `plugins/Sf2Player/Sf2Player.cpp:685-688, 716-717, 775-777, 874`
> what: Note processing and rendering lock `m_playingNotesMutex`, `m_synthMutex` and
>       `m_notesRunningMutex` on the audio path.
> when: A UI or reload operation holds one of these mutexes while the audio callback needs it;
>       the callback blocks and can miss its deadline.

That is the finding I adjudicated. It was real on the revision it was reported against
(`ec338df42`), and on this line it is already fixed. No product file was changed.

## Evidence: the defect is absent on this line

**(1) The three mutexes the finding names do not exist in the module.** fff `multi_grep` over
`plugins/Sf2Player/` for `m_playingNotesMutex`, `m_synthMutex`, `m_notesRunningMutex` returns
**0 matches**.

**(2) The one lock that is left is UI-side, and no audio-path function reaches it.** A grep of the
whole module for `std::mutex|QMutex|lock_guard|unique_lock|try_lock` returns exactly these lines
(plus two comments):

| file:line | what |
| --- | --- |
| `plugins/Sf2Player/Sf2Player.h:407-408` | `m_synthHandoffMutex` — "Serialises UI-side producers/consumers of the two queues; the audio thread never takes it." |
| `plugins/Sf2Player/Sf2Player.cpp:474` | the only `std::lock_guard`, inside `publishSynth()` (`:466-486`) |
| `plugins/Sf2Player/Sf2Player.cpp:492` | `drainRetiredSynths()` (`:490-494`) |

Their callers are UI-side only: `publishSynth()` from `openFile()` (`:582`) and `reloadSynth()`
(`:822`); `drainRetiredSynths()` from `openFile()` (`:551`), `updatePatch()` (`:652`) and
`getCurrentPatchName()` (`:661`). A grep for `wait(|sleep(|msleep|QThread::` in
`plugins/Sf2Player/Sf2Player.cpp` matches no call, only two comments that mention waiting.

**(3) What replaced the mutexes, on the audio thread:**

| file:line | what |
| --- | --- |
| `Sf2Player.cpp:418` | every synth is created with `synth.threadsafe-api = 0`, so no FluidSynth call the render thread makes can take FluidSynth's API mutex |
| `Sf2Player.cpp:1120-1165` | `serviceSynthHandoff()` adopts a published synth at a period boundary through `m_pendingSynths` (bounded, lock-free `Sf2PointerQueue`, `Sf2Player.h:402`) and retires the old one through `m_retiredSynths` (`Sf2Player.h:404`); the retired synth is deleted by the UI thread (`Sf2Player.cpp:498-506`), never by the audio thread |
| `Sf2Player.h:435` | `MidiHandoffQueue<NoteEventCapacity, Sf2NoteEvent> m_noteEvents` — the bounded lock-free note hand-off, "neither allocates, grows a container nor waits for a lock" (`Sf2Player.h:430-434`) |
| `Sf2Player.h:438` | `m_pluginDataSlots` — preallocated per-note plugin data; a note claims a slot for its life (`Sf2Player.cpp:888-894`) |

**(4) The fix is on this line by ancestry, not by my edit.** `git merge-base --is-ancestor <c> HEAD`
reports these as ancestors of `3436a808b6`:

- `370df75734` fix(sf2): realtime note tracking and lock-free synth hand-off (new-p9-454, new-p9-453)
- `3c57b7e545` fix(sf2): one settings object per synth, no control call on the render path
- `d73caeacaf` fix(sf2): a render path that cannot wait, a reload that keeps the font
- `4510382de8` fix(sf2): one SoundFont instance per synth
- `4094d8d438` fix(sf2): a teardown note-off the full note event queue refused

The accepted branch's own commits are **not** ancestors (`6072eee267`, `422971ae8b`): the same
behaviour reached the train through the `new-p9-454` line, which is why this port has nothing left
to apply.

## Execution evidence (this session, this worktree)

Logs are in `~/.config/kads/zene/state/runs/new-p9-453-port-port.r1/logs/`.

| command | result |
| --- | --- |
| `cmake --build build --target Sf2PlayerRealtimeTest -j4` → `build-1-sf2realtime.log` | **EXIT=0** |
| `cd build/tests && ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` → `test-1-sf2realtime.log` | **EXIT=0**, `1/1 Test #236: Sf2PlayerRealtimeTest ... Passed` |
| `cd build/tests && ./Sf2PlayerRealtimeTest` → `test-2-sf2realtime-direct.log` | **EXIT=0**, `Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → `gates-1.log` | **EXIT=0**, `RESULT: PASS` (0 changed files at that point) |
| `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-453-port-port` → `submit-1.log` | see the log; recorded below |

The guard that would fail if this finding came back is already in the tree, and it is what I ran:

- `renderPathAppliesTheState` (`tests/src/plugins/Sf2PlayerRealtimeTest.cpp:230-281`) reads the live
  synth and asserts `synth.threadsafe-api == 0` (`:250-252`), then arms the allocation counter and
  asserts one render period allocates **0** times (`:272-280`).
- `notePathAllocatesNothing` (`:196-220`) asserts the note stage allocates 0 times around
  `playNote`.
- `aStartedNoteIsStoppedWhenTheEventQueueIsFull` and `renderSurvivesAPendingReload` cover the
  exhaustion and reconfiguration paths.

All 14 cases passed with **0 skipped**, so these are real assertions and not skips.

## Test matrix (procedure step 3)

I added no test, because there is no product change to guard: a test written here would pass with
and without any edit, which the rules call a wrong test. The matrix below is what the existing
suite already exercises, and I ran all of it:

| path | case | result |
| --- | --- | --- |
| normal | `playsWithARealSoundFont`, `notePathAllocatesNothing` | PASS |
| failure / refusal | `aStartedNoteIsStoppedWhenTheEventQueueIsFull` | PASS |
| exhaustion | `moreNotesThanTheOldPoolGetSlots` | PASS |
| reconfiguration | `renderSurvivesAPendingReload`, `everySynthHasItsOwnSettings` | PASS |
| audio-thread constraints | `renderPathAppliesTheState` (threadsafe-api 0, 0 allocations) | PASS |
| font lifetime | `reloadKeepsTheSoundFontWhenTheFileIsGone`, `everySynthHasItsOwnSoundFont` | PASS |
| platform build | Linux only here; no product file changed, so hosted CI sees the same code | n/a |

## Why the remaining hunks of the accepted branch are not ported here

`git diff HEAD f439759d3b -- plugins/Sf2Player include/MidiHandoffQueue.h` leaves exactly two
non-test behaviours. I checked both against the code rather than copying them:

1. **The `m_synthGeneration` guard in `playNoteImpl`/`noteOn`** (the accepted batch's new-p9-451
   part). That branch needed it because a swap could run while voice pointers were live. Here
   `adopt()` retires the old synth and clears **every** slot's `fluidVoices` in the same audio-thread
   call that swaps `m_synth`: `m_retiredSynths.push` then `m_synth = next.synth` then the
   `for (auto& slot : m_pluginDataSlots) slot.data.fluidVoices.clear()` (`Sf2Player.cpp:1127-1144`).
   The note teardown path never dereferences a voice pointer — it uses only the slot generation
   word (`Sf2Player.cpp:1491-1530`). There is no window in which a note can reach a retired synth,
   so the guard would be dead code here, and unprovable by a failing test.
2. **The `previousFontData` restore in `openFile()`** (qa-fix `dbffc38daf`, also not an ancestor).
   On this line `loadFont()` assigns `m_fontData` only on its success path
   (`Sf2Player.cpp:616-630`, assignment at `:628`), and a later reload refuses to build from an
   unloadable font and keeps the synth that still plays (`Sf2Player.cpp:810-817`). The residual
   divergence is recorded under **Other findings** instead of fixed here: it is a font-bytes
   finding, not the locks finding, and no public path in the test harness can show it failing.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  There is no product change to revert: `git diff --stat 050/m0-train-53..HEAD` lists this report
  only. The contract test that guards the finding is `Sf2PlayerRealtimeTest`, which I ran with
  **EXIT=0** and `Totals: 14 passed, 0 failed, 0 skipped`. I did not stage a red run: without a
  product edit any such run would be a test that passes for the wrong reason.
- **The test is deterministic.** No wall-clock timing, sleeps or races. The counts come from a
  replacement global `operator new` armed around exactly one call (`allocationCount` / `counting`,
  `Sf2PlayerRealtimeTest.cpp:212-220`), and the state is read back through
  `currentSynthAddress`, `runningNotesForTest` and `fillNoteEventQueueForTest`
  (`Sf2PlayerFixture.h:120-172`, `QMetaObject::invokeMethod` with `Qt::DirectConnection`) and
  FluidSynth's own `fluid_settings_getint`, `fluid_synth_get_gain`, `fluid_synth_get_sfont`.
- **The fix is at the cause and complete.** The cause — one `fluid_synth_t*` shared between the
  render thread and the UI side behind three mutexes — is gone. Every audio-thread entry point
  (`playImpl`, `playNoteImpl`, `noteOn`, `noteOff`, `applyControls`, `renderFrames`,
  `serviceSynthHandoff`, `applyPendingTeardownNoteOffs`) takes no lock; the only lock left is
  `m_synthHandoffMutex`, and I listed all three of its call sites and their five callers above, all
  UI-side. Retirement and deletion are split by `m_retiredSynths` so the audio thread never frees.
- **No collateral damage.** No product or data file changed, so projects save and load exactly as
  before; `reloadKeepsTheSoundFontWhenTheFileIsGone` covers a project whose SoundFont file has gone,
  and `everySynthHasItsOwnSoundFont` covers two instruments on one font.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.**
  `plugins/Sf2Player/Sf2Player.cpp:466-494` (the only lock, UI-side), `:1120-1165` (the lock-free
  adopt path), `:418` (FluidSynth's API mutex switched off),
  `plugins/Sf2Player/Sf2Player.h:401-408` and `:430-438` (the bounded queues and the preallocated
  slots), and the passing `renderPathAppliesTheState` case that asserts the contract on a live synth.

## Other findings

1. **Font bytes are not rolled back after a failed load.** `openFile()` reads the new font into
   `m_fontData` before it knows a synth can be built (`Sf2Player.cpp:616-630`), and on both failure
   paths (`:596-609`) it leaves the new bytes in place while `m_filename`, `m_sourceFile` and
   `m_presets` still name the old font. Playback survives, because `reloadSynth()` refuses an
   unloadable font and keeps the playing synth (`:810-817`), but `createPatchDialogSynth()` then
   fails at `:847-853`, so the Patches dialog is empty after a failed load. The accepted branch
   restored the previous bytes (`dbffc38daf`); that commit is not an ancestor of this line. Not
   fixed here: it is a different finding, and no public harness path can currently show it failing.
2. **The accepted branch rewrote `tests/all-sources.txt`** — its commit stat shows `1149 +---` — which
   looks like a hand regeneration. Noted so the queue does not merge that file from the stale branch.
