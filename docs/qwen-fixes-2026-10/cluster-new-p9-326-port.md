FIXED

Cluster new-p9-326-port - the OpulenZ instrument never re-applied the
velocity-scaled operator levels to a voice that was already sounding, so a
held note kept the level it was given at note-on.

## The bug in the current tree

`plugins/OpulenZ/OpulenZ.cpp:578` (before this change) read

```cpp
	for( int voice = 0; voice < OPL2_VOICES ; ++voice) {
		if(voiceNote[voice] && OPL2_VOICE_FREE == 0) {
			setVoiceVelocity(voice, velocities[voiceNote[voice]] );
		}
	}
```

`OPL2_VOICE_FREE` is 128 (`plugins/OpulenZ/OpulenZ.h:49`), so `OPL2_VOICE_FREE == 0`
is a compile-time-false constant and the loop body never ran: `updatePatch()`
never re-scaled a sounding voice after an operator level changed.

The flag itself is a bit in the voice's note number, not a separate state:
`voiceNote[i]` is initialised to `OPL2_VOICE_FREE` (`OpulenZ.cpp:154`),
a note-on stores the bare key (`OpulenZ.cpp:312`) and a note-off sets the bit
(`OpulenZ.cpp:323`). The sibling code in the same file already uses that
encoding - the pitch-bend handler asks for the flag bit alone
(`OpulenZ.cpp:348`: `int playing = (voiceNote[v] & OPL2_VOICE_FREE) == 0;`).

## The fix

`plugins/OpulenZ/OpulenZ.cpp:576-584` now asks whether the free flag is clear
instead of comparing the constant to zero:

```cpp
	for( int voice = 0; voice < OPL2_VOICES ; ++voice) {
		if((voiceNote[voice] & OPL2_VOICE_FREE) == 0) {
			setVoiceVelocity(voice, velocities[voiceNote[voice]] );
		}
	}
```

A free voice (`voiceNote == OPL2_VOICE_FREE`) and a released voice
(`key | OPL2_VOICE_FREE`) both carry the bit and are skipped, so
`velocities[voiceNote[voice]]` is only read for a voice that is still
sounding - and then `voiceNote[voice]` is the bare key (0..127), inside the
`velocities[128]` array. This is the same test the pitch-bend handler uses, so
the two places that read the flag now agree.

Scope of the change:

* the mutex is NOT touched: `updatePatch()` already holds `emulatorMutex`
  (`OpulenZ.cpp:539`, `QMutexLocker locker(&emulatorMutex);`) and
  `setVoiceVelocity()` is documented as "only to be called from code protected
  by the holy Mutex" (`OpulenZ.cpp:246`). The upstream fix's second commit
  (797dc0b65) added that lock; this tree already carries it from the sibling
  cluster new-p9-327, whose regression is
  `PluginPortsMigrationTest::opulenzUpdatePatchLocksEmulatorForWholeUpdate`
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2538`).
* the audio thread is not touched: `playImpl()` (`OpulenZ.cpp:394`) is
  unchanged; the new emulator writes happen in `updatePatch()`, on the
  thread that changed the model.
* no new control command, no new file in the product, no change to what a
  saved project contains: the fix only decides which already-playing voices
  are re-scaled.

## The regression

`tests/src/plugins/PluginPortsMigrationTest.cpp:2573`
(`PluginPortsMigrationTest::opulenzReappliesVelocityScalingToHeldVoice`),
registered as the focused CTest `OpulenZ`
(`tests/CMakeLists.txt:2797-2798`).

The test loads the real plugin module, opens a voice with a velocity-127 note
on, never releases it, and then changes both operator levels while the note
is held. The OPL2 operator level register is inverted, so level 63 is register
value 0 (loudest) and level 0 is register value 63 (silent):

* before the change the held voice renders energy 1031.38,
* after the change it must render less than one percent of that.

The test drives the plugin through the product's public surface - the models
are addressed by the attribute names the plugin's own
`saveSettings()`/`loadSettings()` pair uses (the same round trip the track
uses to persist an instrument) - so the test binary carries no dependency on
the plugin's private header and no copy of the plugin's class layout.

### Ownership of the instrument the test creates

The instance the module entry point hands out is owned by the test, not by
the track: `Instrument`'s QObject parent is null
(`src/core/Instrument.cpp:70`, `Plugin(_descriptor, nullptr/* _instrument_track*/, key)`),
so `InstrumentTrack`'s destructor - which deletes only its own
`m_instrument` (`src/tracks/InstrumentTrack.cpp:229`) - would leave the
module-created instance and its sample-rate connection alive for the rest of
the test process. The test therefore holds it in a `std::unique_ptr`
declared after the track and while the `QLibrary` is still loaded, so the
destruction order is instrument -> track -> library on every exit path,
including the early returns of the assertions
(`tests/src/plugins/PluginPortsMigrationTest.cpp:2586-2597`).

### Why the upstream test was not ported verbatim

The upstream branch (commits a58a4bc73, 797dc0b65) put the regression in the
same file, but it included the plugin's private header
(`#include "OpulenZ.h"`, plus `${PROJECT_SOURCE_DIR}/plugins/OpulenZ` on the
test target's include path) and drove the models through the plugin class
members (`instrument->op1_lvl_mdl.setValue(...)`).

That shape was rebuilt and run on this tree to see the merge train's reported
fatal abort: it compiles and it passes here
(`logs/build-9-upstream-shape.log`, EXIT 0; `logs/test-8-upstream-shape.log`,
EXIT 0; and in the whole family, `logs/test-9-upstream-shape-family.log`,
EXIT 8 with only the pre-existing LADSPA fixture failure - see below). The
abort could therefore NOT be reproduced on this tree, so it was not
reproduced-and-fixed; the port removes the fragile part instead: the test no
longer compiles a second copy of the plugin's class layout, which is the only
coupling between the test binary and the module it loads (the module is
statically linked against its own copy of the core, so the test's copy and
the module's copy of the same class must agree byte for byte).

The behaviour under test is identical either way: the same model change, the
same held voice, the same product path
(`restoreState` -> `loadSettings` -> model value -> `dataChanged` ->
`updatePatch()` -> `setVoiceVelocity()` for the sounding voice).

## Evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-326-port.r1/logs`.
Commands ran from the worktree root; CTest ran from `build/tests`.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Baseline build | `cmake --build build --target PluginPortsMigrationTest opulenz -- -j20 > logs/build-1-baseline.log 2>&1; echo EXIT=$?` | EXIT 0 |
| RED before the fix | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-1-red.log 2>&1; echo EXIT=$?` | EXIT 8, 0/1; failed at `after < before * 0.01` (energy before=1031.38 after=1022.79, ratio 0.991669) |
| Build with the fix | `cmake --build build --target opulenz PluginPortsMigrationTest -- -j20 > logs/build-3-fix.log 2>&1; echo EXIT=$?` | EXIT 0 |
| GREEN after the fix | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-2-green.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 |
| Reverse the product patch | `git apply -R logs/fix.patch; echo EXIT=$?` | EXIT 0 |
| Rebuild with the patch reversed | `cmake --build build --target opulenz -- -j20 > logs/build-4-reverse.log 2>&1; echo EXIT=$?` | EXIT 0 |
| RED with the patch reversed | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-3-red-reversed.log 2>&1; echo EXIT=$?` | EXIT 8, 0/1; same assertion |
| Re-apply the product patch | `git apply logs/fix.patch; echo EXIT=$?` | EXIT 0 |
| Rebuild after re-applying | `cmake --build build --target opulenz -- -j20 > logs/build-5-reapply.log 2>&1; echo EXIT=$?` | EXIT 0 |
| GREEN after re-applying | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-4-green-reapplied.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 |
| Affected family, fix reversed | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-6-family-baseline.log 2>&1; echo EXIT=$?` | EXIT 8; 44 passed, 2 failed, 2 skipped - the new regression plus the pre-existing LADSPA fixture failure |
| Affected family, with the fix | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-7-family-final.log 2>&1; echo EXIT=$?` | EXIT 8; 45 passed, 1 failed, 2 skipped - only the pre-existing LADSPA fixture failure |
| Final build + focused test | `cmake --build build --target PluginPortsMigrationTest opulenz -- -j20 > logs/build-10-final-test.log 2>&1; echo EXIT=$?` then `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-10-final-green.log 2>&1; echo EXIT=$?` | EXIT 0 / EXIT 0, 1/1 |
| Final build of the committed tree | `cmake --build build --target PluginPortsMigrationTest opulenz zene -- -j20 > logs/build-12-final-all.log 2>&1; echo EXIT=$?` | EXIT 0 |
| Final focused test of the committed tree | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-12-final-focused.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 |
| Final family run of the committed tree | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-13-final-family.log 2>&1; echo EXIT=$?` | EXIT 8; 45 passed, 1 failed, 2 skipped - only the pre-existing LADSPA fixture failure |
| Focused test, verbose (energy numbers) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest opulenzReappliesVelocityScalingToHeldVoice > logs/test-11-final-green-verbose.log 2>&1; echo EXIT=$?` | EXIT 0; energy before=1031.38 after=0.0193274, ratio 1.87394e-05 |
| `zene` still builds | `cmake --build build --target zene -- -j20 > logs/build-11-zene.log 2>&1; echo EXIT=$?` | EXIT 0 |
| Upstream test shape, rebuilt | `cmake --build build --target PluginPortsMigrationTest -- -j20 > logs/build-9-upstream-shape.log 2>&1; echo EXIT=$?` | EXIT 0 |
| Upstream test shape, focused run | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-8-upstream-shape.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 (the reported abort does not occur on this tree) |
| Upstream test shape, whole family | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-9-upstream-shape-family.log 2>&1; echo EXIT=$?` | EXIT 8; 45 passed, 1 failed, 2 skipped - only the pre-existing LADSPA fixture failure |
| Complexity ratchet (after the final edit) | `bash tests/complexity-gate.sh --check > logs/gate-4-complexity-final.log 2>&1; echo EXIT=$?` | EXIT 1 - red on the train for other files; no item in the files this change touches (see below) |
| File-length ratchet (after the final edit) | `bash tests/file-length-gate.sh --check > logs/gate-5-file-length-final.log 2>&1; echo EXIT=$?` | EXIT 1 - red on the train for other files; no item in the files this change touches (see below) |
| Feature check (all gates, after the final edit) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-326-port > logs/gate-6-feature-check-final.log 2>&1; echo EXIT=$?` | EXIT 0 - RESULT: PASS, `complexity: no new item in your files`, `file-length: no new item in your files` |

### Round 2 (review REWORK: the test leaked the instrument it created)

| Evidence | Exact command | Result |
| --- | --- | --- |
| Build after the ownership fix | `cmake --build build --target zene PluginPortsMigrationTest opulenz -- -j20 > logs/build-16-raii.log 2>&1; echo EXIT=$?` | EXIT 0 |
| Focused test after the ownership fix | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-16-raii-focused.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 |
| Affected family after the ownership fix | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-17-raii-family.log 2>&1; echo EXIT=$?` | EXIT 8; 45 passed, 1 failed, 2 skipped - only the pre-existing LADSPA fixture failure |
| Product patch (base -> HEAD) | `git diff HEAD~2 -- plugins/OpulenZ/OpulenZ.cpp > logs/fix.patch` | the same one-condition product change |
| Reverse the product patch | `git apply -R logs/fix.patch; echo EXIT=$?` | EXIT 0 |
| Rebuild with the patch reversed | `cmake --build build --target opulenz -- -j20 > logs/build-18-raii-red.log 2>&1; echo EXIT=$?` | EXIT 0 |
| RED with the patch reversed (final test) | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-18-raii-red.log 2>&1; echo EXIT=$?` | EXIT 8, 0/1; failed at `after < before * 0.01` (energy before=1031.38 after=1022.79, ratio 0.991669) |
| Re-apply the product patch | `git apply logs/fix.patch; echo EXIT=$?` | EXIT 0 |
| Rebuild after re-applying | `cmake --build build --target opulenz -- -j20 > logs/build-19-raii-green.log 2>&1; echo EXIT=$?` | EXIT 0 |
| GREEN after re-applying (final test) | `cd build/tests && ctest -R '^OpulenZ$' --output-on-failure > logs/test-19-raii-green.log 2>&1; echo EXIT=$?` | EXIT 0, 1/1 |
| GREEN, verbose (final test) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest opulenzReappliesVelocityScalingToHeldVoice > logs/test-20-raii-green-verbose.log 2>&1; echo EXIT=$?` | EXIT 0; energy before=1031.38 after=0.0193274, ratio 1.87394e-05 |
| Complexity ratchet (final) | `bash tests/complexity-gate.sh --check > logs/gate-8-complexity-raii.log 2>&1; echo EXIT=$?` | EXIT 1 - red on the train for other files; no item in the files this change touches |
| File-length ratchet (final) | `bash tests/file-length-gate.sh --check > logs/gate-9-file-length-raii.log 2>&1; echo EXIT=$?` | EXIT 1 - red on the train for other files; no item in the files this change touches |
| Feature check (final) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-326-port > logs/gate-10-feature-check-raii.log 2>&1; echo EXIT=$?` | EXIT 0 - RESULT: PASS, `complexity: no new item in your files`, `file-length: no new item in your files` |


### The pre-existing failure in the affected family

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` fails in this
tree with "SWH Decimator test fixture was not loaded": the LADSPA fixture the
slot needs is not installed on this machine. It fails identically with the
product patch reversed (`logs/test-6-family-baseline.log`) and with the patch
applied (`logs/test-7-family-final.log`), and it is not an OpulenZ test.

### Gate detail

`tests/complexity-gate.sh --check` (EXIT 1, `logs/gate-4-complexity-final.log`)
reports these regressions, none of them in a file this change touches:
`dotCentreAlong` (tests/src/gui/XyPadPairTest.cpp), `parseNotes`
(src/core/DawProjectReadTracks.cpp), `adjust`
(src/core/ControlCommandsMixerTools.cpp), `setStrip`
(src/core/ControlCommandsMixerStrip.cpp), `rotateProjectRevision`
(src/core/ProjectRevisions.cpp), `importedOverrides`
(src/core/ControlCommandsShortcuts.cpp).

`tests/file-length-gate.sh --check` (EXIT 1,
`logs/gate-5-file-length-final.log`) reports these regressions, none of them
in a file this change touches: src/core/SmfInterchangeReader.cpp,
src/core/TakeLane.cpp, tests/src/core/CollabTransportTest.cpp,
tests/src/core/ControlAutomationScriptTest.cpp,
tests/src/core/ControlDeviceCatalogueTest.cpp,
tests/src/core/ControlEditCommandsTest.cpp,
tests/src/core/CustomAccessTest.cpp, tests/src/core/DeviceDspTest.cpp,
tests/src/core/HardwareIoTest.cpp, tests/src/core/NotePlaybackTest.cpp,
tests/src/core/RackMacrosTest.cpp, tests/src/core/RevisionTimelineTest.cpp,
tests/src/core/VideoTrackTest.cpp, tests/src/gui/ArrangerTimelineTest.cpp.

`feature_check.py` (EXIT 0, `logs/gate-6-feature-check-final.log`) is the gate
that compares against the train baseline: it reports
`complexity: no new item in your files` and
`file-length: no new item in your files` for this change, and PASS for
fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology and doc-figures.

## Files committed

- `plugins/OpulenZ/OpulenZ.cpp` - the fix.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` - the regression.
- `tests/CMakeLists.txt` - the focused `OpulenZ` CTest registration.
- `tests/upstream-modifications.txt` - the three inherited files registered.
- `tests/fork-sources.txt` - this report registered as a product-new file.
- `docs/qwen-fixes-2026-10/cluster-new-p9-326-port.md` - this report.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES
with it (exit codes).**

* Before the fix existed at all: `ctest -R '^OpulenZ$'` -> EXIT 8, 0/1, failed
  at `after < before * 0.01` (`logs/test-1-red.log`; energy before=1031.38,
  after=1022.79, ratio 0.991669).
* With the fix: `ctest -R '^OpulenZ$'` -> EXIT 0, 1/1
  (`logs/test-2-green.log`; ratio 1.87394e-05, `logs/test-11-final-green-verbose.log`).
* Reverse patch (`git apply -R logs/fix.patch`, EXIT 0), rebuild
  (`logs/build-4-reverse.log`, EXIT 0), test -> EXIT 8, 0/1, the same
  assertion (`logs/test-3-red-reversed.log`).
* Re-apply (`git apply logs/fix.patch`, EXIT 0), rebuild
  (`logs/build-5-reapply.log`, EXIT 0), test -> EXIT 0, 1/1
  (`logs/test-4-green-reapplied.log`).
* Repeated with the FINAL test (the one that owns the instrument with
  RAII): reverse (`logs/fix.patch`, EXIT 0) + rebuild
  (`logs/build-18-raii-red.log`, EXIT 0) -> EXIT 8, 0/1, the same assertion
  (`logs/test-18-raii-red.log`); re-apply (EXIT 0) + rebuild
  (`logs/build-19-raii-green.log`, EXIT 0) -> EXIT 0, 1/1
  (`logs/test-19-raii-green.log`, ratio 1.87394e-05 in
  `logs/test-20-raii-green-verbose.log`).

**2. The test is deterministic: no wall-clock timing, sleeps or races.**

* The rendered amount is a fixed count of engine periods (8 warmup + 8
  measured), never a deadline: the loop is `for (int period = 0; period <
  periods; ++period)` over `Instrument::play(std::span<SampleFrame>)`, which
  renders exactly `Engine::audioEngine()->framesPerPeriod()` frames (256 at
  44100 Hz in this tree) through the emulator's own `update()`.
* No sleep, no timer, no thread, no wall-clock read anywhere in the test.
* The audio device is stopped by the test's own `initTestCase()`
  (`Engine::audioEngine()->audioDev()->stopProcessing()`), so no other thread
  renders the same emulator while the test renders it.
* The inputs are injected, not observed: the operator levels (63/63 then 0/0)
  and the note (key 69, velocity 127) are set explicitly; the measured
  quantity is the sum of the squared rendered samples, compared with a fixed
  ratio, not a duration.
* The numbers repeat exactly across runs: before=1031.38 in the RED run
  (`logs/test-1-red.log`) and in the GREEN run
  (`logs/test-11-final-green-verbose.log`).

**3. The fix is at the cause and complete: every caller and sibling of what
changed was checked and stays consistent.**

* Cause: the constant-false comparison `OPL2_VOICE_FREE == 0`
  (`plugins/OpulenZ/OpulenZ.cpp:578` before the change; the constant is 128 at
  `plugins/OpulenZ/OpulenZ.h:49`).
* Every place that reads or writes the flag was checked and agrees with the
  new test: the initialisation `voiceNote[i] = OPL2_VOICE_FREE`
  (`OpulenZ.cpp:154`), the note-on that stores the bare key
  (`OpulenZ.cpp:312`), the note-off that sets the bit (`OpulenZ.cpp:323`),
  the pitch-bend handler that reads the flag bit alone
  (`OpulenZ.cpp:347-348`), and the note-off / key-pressure comparisons that
  use the whole value (`OpulenZ.cpp:319`, `OpulenZ.cpp:333`). A sounding
  voice has the bit clear, a free or released voice has it set - the new
  condition asks exactly that, in the same encoding as its siblings.
* The callers of `updatePatch()` were checked: the knob connections
  (`OpulenZ.cpp:178-216`), the constructor (`OpulenZ.cpp:160`) and
  `reloadEmulator()` (`OpulenZ.cpp:243`, which resets every `voiceNote` to
  `OPL2_VOICE_FREE` before calling it, so the loop correctly skips all
  voices there).
* The value the loop reads flows into `velocities[voiceNote[voice]]`;
  `velocities` is `int velocities[128]` (`OpulenZ.h:133`) and for a sounding
  voice `voiceNote[voice]` is the bare key (0..127), so the index is in
  range. A free voice (128) and a released voice (key|128) are skipped
  before that read.
* `setVoiceVelocity()` is documented as callable only under the mutex
  (`OpulenZ.cpp:246`) and `updatePatch()` holds `emulatorMutex` for the whole
  update (`OpulenZ.cpp:539`), so the newly enabled call satisfies that
  precondition. That lock is the sibling cluster new-p9-327's change, which
  this tree already carries; the upstream fix's second commit
  (797dc0b65) added the same lock on its branch.
* The other path into the same behaviour - `MidiKeyPressure` re-scales a
  voice on aftertouch (`OpulenZ.cpp:333`) - is unchanged, and it is the
  sibling that made the missing re-scale visible only until the next MIDI
  event.

**4. No collateral damage: old projects and saved data still load as before,
and a limit or check added here does not hit other callers.**

* No file format, project element or saved value changed: the fix is one
  condition inside `updatePatch()`. The plugin's `saveSettings()` /
  `loadSettings()` are untouched, so a project saved before or after the
  change loads exactly as before.
* The only behaviour that changes is the one the finding names: a voice that
  is sounding when an operator level changes is re-scaled. A free or
  released voice is skipped, exactly as before, and no limit, clamp or
  check was added to any shared path, so no other caller can trip it.
* The behaviour-preservation harness that compares the migrated plugin
  against the verbatim pre-migration reference source
  (`tests/reference/OpulenZ/OpulenZ.cpp`, which still carries the old
  condition) is unaffected: the family run is 45 passed / 1 failed / 2
  skipped with the fix (`logs/test-13-final-family.log`) and 44 passed /
  2 failed / 2 skipped with it reversed (`logs/test-6-family-baseline.log`) -
  the one failure in both runs is the pre-existing LADSPA fixture failure,
  and the comparison never changes a level while a note is held.
* The audio thread is not touched: `playImpl()` (`OpulenZ.cpp:394`) is
  unchanged, and the new emulator writes happen in `updatePatch()`, on the
  thread that changed the model - no new allocation, lock, wait or log on
  the render path. No new control command was added.
* The test owns what it creates: the module-created instrument is held in a
  `std::unique_ptr` declared after the track and while the `QLibrary` is
  alive, so the instance, its emulator and its sample-rate connection are
  released before the track and the module go away, on every exit path
  including the assertions' early returns
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2586-2597`). Nothing the
  test creates outlives the slot any more.

**5. The verdict is FIXED, not ALREADY-FIXED and not a FALSE-POSITIVE.**

The condition was present in this tree before the change
(`plugins/OpulenZ/OpulenZ.cpp:578`:
`if(voiceNote[voice] && OPL2_VOICE_FREE == 0)`), the constant it compares is
128 (`plugins/OpulenZ/OpulenZ.h:49`), and the loop body never ran: the
RED run proves it (the held voice kept 99.17% of its energy after the level
change, `logs/test-1-red.log`). The mutex hunk of the upstream fix's second
commit was already on this line through cluster new-p9-327, so only the
condition had to be re-applied here.

