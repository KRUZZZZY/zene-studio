FIXED

new-p9-454: FIXED

The SF2 note path no longer allocates on the audio thread. `playNoteImpl()`
takes one of the instrument's preallocated `Sf2PluginDataSlot`s
(`plugins/Sf2Player/Sf2Player.h:195`, `Sf2Player.cpp:643`) instead of
`new Sf2PluginData`, and hands the note to the render stage through the
bounded, preallocated `MidiHandoffQueue<NoteEventCapacity, Sf2NoteEvent>`
(`Sf2Player.h:377`, `Sf2Player.cpp:680-691`). The old `QVector<NotePlayHandle*>
m_playingNotes` and its `append()`/mutex are gone; there is no `m_playingNotes`
member left in the tree. A refused first event releases the claimed slot
(`Sf2Player.cpp:743-750`) so a full queue cannot leak it. `deleteNotePluginData`
queues a pitch-only note-off and returns the slot
(`Sf2Player.cpp:1201-1227`).

Regression: `Sf2PlayerRealtimeTest::notePathAllocatesNothing` loads the
product's own `sf2player` module and counts allocations with the fork's
thread-local allocation probe while it calls `Instrument::playNote()`.
It fails on the old code and passes with the fix:

- RED: `git apply -R build/qwen-logs/fix.patch` EXIT=0; `cmake --build build --target sf2player Sf2PlayerRealtimeTest -j8` EXIT=0; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure` EXIT=8. Assertion: `the SF2 note path allocated 1 time(s) on the audio thread`.
- GREEN: `git apply build/qwen-logs/fix.patch` EXIT=0; same build EXIT=0; same ctest EXIT=0 (1/1 passed). Verbose run: `./Sf2PlayerRealtimeTest -v1` EXIT=0, `Totals: 4 passed, 0 failed, 0 skipped`.

new-p9-453: FIXED

The render thread no longer takes `m_playingNotesMutex`, `m_notesRunningMutex`
or `m_synthMutex`; all three members are removed. The render thread owns the
active `fluid_synth_t` and adopts a fully built replacement from the UI through
`Sf2PointerQueue<Sf2SynthSwap, 8>` (`Sf2Player.h:77-140`, `Sf2Player.cpp:847`).
The old synth travels back on the retire queue and the UI deletes it after the
render thread has stopped using it (`Sf2Player.cpp:368`, `343`). Reload/font
changes build the candidate off the audio thread (`reloadSynth`, `openFile`),
and gain/reverb/chorus/patch/bank/tuning are published as atomics
(`Sf2Player.h:309-324`) and applied by the render thread in `applyControls()`
(`Sf2Player.cpp:914`). `noteOn`/`noteOff` (`Sf2Player.cpp:778`, `833`) and
`renderFrames` (`Sf2Player.cpp:1158`) use the render-owned synth with no lock.
`deleteNotePluginData` does not call into the synth either. The only remaining
mutex, `m_synthHandoffMutex`, serialises UI-side producers of the hand-off
queues and is never taken by an audio path (`Sf2Player.h:350`).

Regression: the fork's RT-safety sweep, with the SF2 audio functions now
declared in `tests/rt-safety-scope.txt:63-74`, proves there is no allocation,
container growth or lock left in them.

- RED (old product): `git apply -R build/qwen-logs/fix.patch` EXIT=0; `python3 tests/rt-safety-sweep.py --tree $PWD --scope $PWD/build/qwen-logs/rt-scope-old.txt --allowlist tests/rt-safety-allowlist.txt --check` EXIT=1. It reported `playNoteImpl:alloc-new`, `playNoteImpl:grow-container`, `playNoteImpl:lock-call`, `playImpl:lock-call`, `renderFrames:lock-guard`, `noteOn:lock-call`, `noteOff:lock-call`, `deleteNotePluginData:lock-call` (allocation 1, locking 12, growth 3).
- GREEN (fix restored): `git apply build/qwen-logs/fix.patch` EXIT=0; same sweep scope with an empty allowlist (`build/qwen-logs/rt-allow-empty.txt`) EXIT=0; the repository's own `python3 tests/rt-safety-sweep.py --check` EXIT=0.
- The runtime smoke `Sf2PlayerRealtimeTest::renderSurvivesAPendingReload` publishes two replacement synths and renders after each: `./Sf2PlayerRealtimeTest -v1` EXIT=0 (`PASS renderSurvivesAPendingReload`).

Affected-family run: `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(Sf2PlayerRealtimeTest|RtSafetySweep|RtSafetySelfTest)$' --output-on-failure` EXIT=0, 3/3 tests passed. The reference `Sf2Player` test and the SF2 cases of `PluginPortsMigrationTest` are not built in this configuration (`PART_C_SF2_FILE-NOTFOUND`; the module is only added when a soundfont exists), so they execute zero tests and cannot be used as evidence here.

Build: `cmake --build build --target zene sf2player Sf2PlayerRealtimeTest -j8` EXIT=0 (final build after the edit).

Gates: `feature_check.py` as installed cannot create its sibling log directory under this sandbox (`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-454-feature-check`: Permission denied). Running the same script with only its log path redirected produced: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in these files), file-length PASS (no new item in these files), RESULT PASS, EXIT=0 (`build/qwen-logs/feature-check-local.log`). The same gates were also run individually: `bash tests/fork-sources-gate.sh` EXIT=0, `bash tests/all-sources-reproduce.sh` EXIT=0, `bash tests/no-upstream-regression-gate.sh` EXIT=0, `tests/scripted/check-namespace` EXIT=0, `bash tests/no-tautology-gate.sh` EXIT=0, `python3 tests/doc-figures-gate.py` EXIT=0.

Log-folder note: every attempt to create a file in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-454.r2/logs`
failed with `Permission denied` (for example `touch .../logs/perm-test` EXIT=1),
including through the file-write tool, while the worktree is writable. All logs
for this session are therefore new numbered files under
`build/qwen-logs/` in the worktree (build directory, not committed):
`build-1`..`build-7`, `red-build-454.log`, `red-454-test.log`,
`red-453-sweep.log`, `green-build.log`, `green-454-test.log`,
`green-454-verbose.log`, `green-453-sweep.log`,
`green-453-permanent-sweep.log`, `final-build.log`, `final-test-family.log`,
`feature-check-local.log`, `fix.patch`, `rt-scope-old.txt`,
`rt-allow-empty.txt`.

Files committed: `include/MidiHandoffQueue.h`,
`plugins/Sf2Player/Sf2Player.h`, `plugins/Sf2Player/Sf2Player.cpp`,
`tests/CMakeLists.txt`, `tests/src/plugins/Sf2PlayerRealtimeTest.cpp`,
`tests/all-sources.txt`, `tests/fork-sources.txt`,
`tests/rt-safety-scope.txt`, `tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p9-454.md`.

## Self-check

- Regression RED/GREEN: see the exit codes above. 454 RED is EXIT=8 with the
  allocation assertion; 454 GREEN is EXIT=0. 453 RED is the sweep EXIT=1 with
  the named lock/alloc hits; 453 GREEN is EXIT=0 on the same scope and on the
  repository sweep.
- Determinism: the runtime test uses the thread-local `AllocationProbe`
  (`tlCountAllocations`), not wall-clock timing, sleeps or a race. It loads
  the module once in the test thread and calls `playNote` synchronously. The
  453 regression is a source-level sweep: no timing at all.
- Cause and completeness: the note path was fixed at its two allocation
  sources (the `new` and the growing vector) by replacing the storage, not by
  capping the old one; every audio-path function that touched a mutex
  (`playNoteImpl`, `playImpl`, `renderFrames`, `noteOn`, `noteOff`,
  `deleteNotePluginData`) was converted, and all UI callers (`openFile`,
  `reloadSynth`, `updatePatch`, `updateGain`, reverb/chorus/tuning slots) now
  publish through the same hand-off.
- Collateral: save/load still writes/reads the same `src`, `bank`, `patch` and
  effect attributes; `openFile` still keeps the old font on a failed load
  (candidate built off-thread and only published on success). The 128 note
  slots exceed FluidSynth's default polyphony (64), and the 256-event queue is
  drained every period; a refused event leaves the note silent for that period
  rather than allocating.
- Gates and pre-submit check: `python3 submit_check.py <worktree> new-p9-454`
  reports PASS for commits, clean tree, report, tests and test registration.
  Its only FAIL is the `feature_check.py` gate row, because that installed
  script tries to `mkdir` a sibling log directory
  (`<worktree>-feature-check`) outside this sandbox and gets `PermissionError`.
  The gate logic itself passes: the same script with only its log path
  redirected gave RESULT PASS, EXIT=0, and the individual gate commands are
  EXIT=0 (see above). This is a sandbox-path limitation, not a code or gate
  failure; no gate limit or baseline was changed.
- Test registration: `tests/src/plugins/Sf2PlayerRealtimeTest.cpp` is added to
  `LMMS_TESTS` in `tests/CMakeLists.txt` (the new-test check passes).
- Verdict: both findings are REAL and FIXED; neither is ALREADY-FIXED or
  FALSE-POSITIVE. The 453 QA DISPUTE asked for a real-time hand-off, SF2 in
  the RT-safety scope, RED/GREEN proof and corrected evidence; those are the
  changes above.
