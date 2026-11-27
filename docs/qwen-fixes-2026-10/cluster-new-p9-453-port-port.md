FIXED

# Cluster new-p9-453-port-port: the SF2 player kept the bytes of a SoundFont it had failed to load

## What the unit asked for

The brief carries the cluster id and an **empty finding body** (`{"cluster": "new-p9-453-port-port",
"title": ""}`), and "Where to start" is empty too. The cluster is the third hop of one fix chain,
and the chain names the code:

- `bugs/qwen-new-p9-453` — cluster `new-p9-453`, the accepted fix for `BUG-INVENTORY.jsonl`
  `finding_id 7683`: *"The SF2 audio path takes mutexes"*.
- `bugs/qwen-new-p9-453-port` — cluster `new-p9-453-port`, the first hand port; its review
  (`qa/reviews/new-p9-453-port.r1.md`) reads `ACCEPT-FIXED`.
- this branch — the same port asked again on the newest line (`FIX-BASE` `050/m0-train-53`).

I checked what the accepted branch still carries that this line does not:
`git diff HEAD f439759d3b -- plugins/Sf2Player include/MidiHandoffQueue.h` leaves exactly two
non-test behaviours this line does not have (items 2 and 3 below); item 1 is the finding's own
defect.

1. The mutex finding itself is **already fixed here**: the three mutexes do not exist, the only
   lock is UI-side (`Sf2Player.cpp:474`, `:492`, callers at `:551`, `:587`, `:659`, `:668`, `:829`),
   every synth runs with `synth.threadsafe-api = 0` (`:418`), and `RtSafetySweep` — which declares
   `Sf2Instrument::playImpl`, `playNoteImpl`, `noteOn`, `noteOff`, `serviceSynthHandoff`,
   `applyControls` and `publishNoteEvent` in `tests/rt-safety-scope.txt` — passes (**EXIT=0**, below).
2. The `m_synthGeneration` guard from that branch is **dead code on this design**: `adopt()` retires
   the old synth and clears every slot's `fluidVoices` in the same audio-thread call that swaps
   `m_synth` (`Sf2Player.cpp:1131-1152`), and the teardown path never dereferences a voice pointer
   (`:1498-1537`), so there is no stale-pointer window to guard. Not ported.
3. The **font rollback** (`previousFontData`, qa-fix `dbffc38daf`, not an ancestor of this line) is
   missing here, and it is a real defect that this branch now fixes and tests.

## The defect

`Sf2Instrument::openFile()` reads the candidate SoundFont into the instrument's own copy
*`before`* it knows a synth can be built from it (`plugins/Sf2Player/Sf2Player.cpp:623-637`,
assignment at `:635`). When the load then fails — the file's header passes `fluid_is_soundfont()`
but FluidSynth cannot parse the chunks, or the candidate synth cannot be created or published —
both failure branches (`:602-616`) kept the **new, unusable bytes** in `m_fontData` while
`m_filename`, `m_sourceFile` and `m_presets` still named the font the player is really playing.

The player then lies about its own state: the next `reloadSynth()` feeds those bytes back to
FluidSynth, `addFontTo()` fails, and `reloadSynth()` takes its "keep the synth that still plays it"
path (`:818-824`) and silently does nothing. The instrument can no longer be rebuilt, its patch
dialog synth (`createPatchDialogSynth()`, `:844-864`) fails too, and the divergence only clears
when another font is loaded successfully.

## The fix (3 product lines)

`plugins/Sf2Player/Sf2Player.cpp`, in `openFile()`: remember the bytes the player is playing
(`:559`), and put them back on both failure branches (`:605`, `:613`). Success paths, the missing
file path, and `loadSettings()`/`loadFile()` behaviour are untouched.

```
git diff --stat
 plugins/Sf2Player/Sf2Player.cpp             |  7 ++++
 tests/src/plugins/Sf2PlayerRealtimeTest.cpp | 65 +++++++++++++++++++++++++++++
 tests/upstream-modifications.txt            |  1 +
```

## The regression test

`tests/src/plugins/Sf2PlayerRealtimeTest.cpp`, case `failedLoadKeepsTheFontForTheNextReload`
(`:508-570`), in the existing file for the SF2 player area, driven through the shipped module:

1. loads the real SoundFont (`SF2_TEST_SOUNDFONT`) and lets the render thread adopt the synth;
2. writes a **truncated copy** of that font (first 4 KiB) and asserts
   `fluid_is_soundfont()` accepts it — the premise that makes the load fail *after* the read;
3. calls `openFile()` with it: nothing is published, the playing synth stays;
4. calls `reloadSynth()` and asserts a **new** synth was published, carries exactly one SoundFont,
   and still plays a tone above 40 Hz.

Step 4 is the discriminator: with the defect the reload is refused and the old synth stays.

Deterministic: no timing, sleeps or threads. The inputs are a file the test writes itself, the
player's own published synth address (`currentSynthAddress`), `fluid_synth_sfcount()` and the
existing autocorrelation-based `playedFrequency()` helper.

## Evidence (this session, this worktree)

Logs: `~/.config/kads/zene/state/runs/new-p9-453-port-port.r2/logs/`.

| # | command | result |
| --- | --- | --- |
| 1 | `cmake --build build --target Sf2PlayerRealtimeTest -j4` → `build-1.log` | **EXIT=0** |
| 2 | `ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` (before the fix) → `test-red-1.log` | **EXIT=8**, `14 passed, 1 failed, 0 skipped`; failing case `failedLoadKeepsTheFontForTheNextReload`, `'reloadedSynth != loadedSynth' returned FALSE` at test line 565, with `fluidsynth: error: Failed to load SoundFont "zene-sf2-memory-font"` |
| 3 | `cmake --build build --target Sf2PlayerRealtimeTest -j4` (with the fix) → `build-2.log` | **EXIT=0** |
| 4 | `ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` (with the fix) → `test-green-2.log` | **EXIT=0**, `100% tests passed out of 1` |
| 5 | `python3 .../feature_check.py <worktree>` → `gates-1.log` | **EXIT=0**, `RESULT: PASS` (8 gates, incl. `file-length no new item in your files`, `complexity no new item in your files`) |
| 6 | `ctest -R 'RtSafetySweep\|RtSafetySelfTest'` → `test-rt-1.log` | **EXIT=0**, `100% tests passed out of 2` |
| 7 | `cmake --build build --target PluginPortsMigrationTest -j4` → `build-sibling-1.log` | **EXIT=0** |
| 8 | `ctest -R '^PluginPortsMigrationTest$'` → `test-sibling-2.log` | **EXIT=8**: `95 passed, 4 failed, 2 skipped`. Both SF2 cases are **skipped** (`SF2Player is not built in this configuration`), so this target does not compile the file I changed and gives no signal either way; the 4 failures are `malletsVoiceLimitIsPerInstrument`, `malletsVoiceLimitSurvivesDeviceChange`, `nonRealtimeLadspaEffectsAreInvalid`, `slice8PluginsAreLiveAndExact` — Mallets/LADSPA/slice8, see Other findings |

### Step 4b — the test catches the bug

| command | result |
| --- | --- |
| `git diff -- plugins/Sf2Player/Sf2Player.cpp > <logs>/fix.patch` | **EXIT=0** |
| `git apply -R <logs>/fix.patch` → `patch-reverse-1.log` | **EXIT=0** |
| `cmake --build build --target Sf2PlayerRealtimeTest -j4` → `build-red-2.log` | **EXIT=0** |
| `ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` → `test-red-2.log` | **EXIT=8** — `failedLoadKeepsTheFontForTheNextReload`, `'reloadedSynth != loadedSynth' returned FALSE (the reload could not rebuild from the font the instrument still plays)` |
| `git apply <logs>/fix.patch` → `patch-restore-1.log` | **EXIT=0** |
| `cmake --build build --target Sf2PlayerRealtimeTest -j4` → `build-green-3.log` | **EXIT=0** |
| `ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` → `test-green-3.log` | **EXIT=0**, `100% tests passed out of 1` |

## Test matrix

| path | case | result |
| --- | --- | --- |
| normal load | `playsWithARealSoundFont`, `notePathAllocatesNothing` | PASS |
| failure (read fails early: missing file) | `PluginPortsMigrationTest::sf2FailedLoadPreservesCurrentFontAndSource` | skipped in that configuration (row 8) |
| failure after read (this fix) | `failedLoadKeepsTheFontForTheNextReload` | RED 8 → GREEN 0 |
| exhaustion | `moreNotesThanTheOldPoolGetSlots`, `aStartedNoteIsStoppedWhenTheEventQueueIsFull` | PASS |
| reconfiguration | `renderSurvivesAPendingReload`, `everySynthHasItsOwnSettings`, `reloadKeepsTheSoundFontWhenTheFileIsGone` | PASS |
| audio-thread constraints | `renderPathAppliesTheState`, `RtSafetySweep`, `RtSafetySelfTest` | PASS (EXIT=0) |
| supported platforms | no new API, no Qt-6-only call: `QByteArray`, `QFile::encodeName`, `QCOMPARE` — build checked on Linux; hosted CI covers Qt 5/MSVC | unverified here |

## Callers and siblings checked

- `m_fontData` is assigned in exactly three places after this change: the save (`:559`), the two
  restores (`:605`, `:613`), and `loadFont()`'s success path (`:635`). Nothing else can clobber it.
- `openFile()` callers: `loadSettings()` (`:322`), `loadFile()` (`:350`), the constructor's default
  font (`:213` via `loadFile`) and the patch dialog (`:1821`). All run on the UI thread.
- Readers of `m_fontData` are `addFontTo()` (`:643-649`) and `reloadSynth()`'s guard (`:818`);
  `createPatchDialogSynth()` goes through `addFontTo()`. They now see the bytes of the font the
  player is playing, which is exactly what they assume.
- Undo, commands and save formats: untouched. No control command added.

## Realtime

The change is UI-side only: `openFile()` is never called from the audio thread, and the restore is
a `shared_ptr` assignment plus a `shared_ptr` copy on the UI thread. Nothing was added to
`playImpl`, `playNoteImpl`, `noteOn`, `noteOff`, `serviceSynthHandoff`, `applyControls` or
`renderFrames`; `RtSafetySweep` and `RtSafetySelfTest` pass (**EXIT=0**).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `test-red-2.log` **EXIT=8** (`failedLoadKeepsTheFontForTheNextReload`,
  `'reloadedSynth != loadedSynth' returned FALSE`). Restored: `test-green-3.log` **EXIT=0**. The
  same case also failed before the fix was written (`test-red-1.log`, **EXIT=8**).
- **The test is deterministic.** No wall-clock timing, sleeps or races: it writes its own truncated
  font, injects nothing, and reads the player's own published synth address
  (`currentSynthAddress`), `fluid_synth_sfcount()` and the existing autocorrelation helper. The
  premise (`fluid_is_soundfont` accepts the truncated file) is asserted, so a change in FluidSynth's
  header check fails loudly instead of turning the test into a false pass.
- **The fix is at the cause and complete.** The cause is that the bytes were replaced before the
  load could fail; they are now saved and restored on both failure branches. Every writer and reader
  of `m_fontData` is listed above, and the sibling `reloadSynth()`/`createPatchDialogSynth()` paths
  are consistent with it.
- **No collateral damage.** The success path, the missing-file path and the saved-state path are
  unchanged, so old projects load exactly as before; the only behaviour that changes is the one the
  test pins. The whole `Sf2PlayerRealtimeTest` family passes, including
  `reloadKeepsTheSoundFontWhenTheFileIsGone`. `PluginPortsMigrationTest` — which owns the older
  missing-file case — skips both SF2 cases in this configuration (`SF2Player is not built`), so it
  neither exercises nor contradicts this change; its four failures are unrelated (Other findings).
- **Registration.** `plugins/Sf2Player/Sf2Player.cpp` is an inherited file: a line was added to
  `tests/upstream-modifications.txt` naming this cluster and the regression, in the same commit
  (`fork-sources` and `no-upstream-regression` gates PASS).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.** The verdict is
  `FIXED`, so this item does not apply to the change; for the record, the part of the cluster that
  was already fixed is the mutex defect, shown at `plugins/Sf2Player/Sf2Player.cpp:418` (FluidSynth's
  API mutex off), `:474` and `:492` (the only lock, UI-side), `:1131-1152` (the lock-free adopt path)
  and `plugins/Sf2Player/Sf2Player.h:401-408`, `:430-438` (bounded queues, preallocated slots).
- **Proof manifest fits the claim.** A `regression-test` artifact names
  `tests/src/plugins/Sf2PlayerRealtimeTest.cpp` (changed by this branch) and
  `failedLoadKeepsTheFontForTheNextReload`, with `red_exit 8` and `green_exit 0`.

## Other findings

1. The generation guard of the accepted branch stays unported: on this design `adopt()` clears the
   slots on the audio thread (`Sf2Player.cpp:1131-1152`) and nothing dereferences a retired voice, so
   porting it would add unprovable dead code.
2. The accepted branch rewrote `tests/all-sources.txt` (its stat shows `1149 +---`), which looks like
   a hand regeneration; the queue should not merge that file from the stale branch.

3. `PluginPortsMigrationTest` has four failing cases on this tree, none of them SF2 and none reachable
   from this change: `malletsVoiceLimitIsPerInstrument` (`:2801`), `malletsVoiceLimitSurvivesDeviceChange`
   (`:2959`), `nonRealtimeLadspaEffectsAreInvalid` (`:1633`, SWH Decimator fixture not loaded) and
   `slice8PluginsAreLiveAndExact` (`:6101`, malletsstk rendered silence) — `test-sibling-2.log`. That
   target reports `SF2Player is not built in this configuration`, so it does not compile the file this
   branch changed. I did not run a base-tree comparison for them; they are listed here, not fixed.
