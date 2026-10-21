FIXED

Cluster 5616 - "Renaming a preset to its current name records a no-op undo step".
Round 2 (this commit): the product fix from round 1 (commit 7317dec00) is kept and
the REGRESSION is reworked so it no longer depends on a loadable instrument or
effect - the round-1 review found the regression could QSKIP and leave the undo
bug untested.

## Verdict and evidence

REAL, and fixed on this branch.

- Finding site: src/core/ControlCommandsChainEdit.cpp:233-243 (the handler of
  chain.rename). Before the fix the handler read
      if (toPath != from && !QFile::rename(from, toPath)) { ...refuse... }
  so for a rename to the CURRENT name the rename was skipped but the handler
  still fell through to addUndoStep(...) at src/core/ControlCommandsChainEdit.cpp:253.
  The undo stack therefore grew a step whose inverse changed nothing, and the
  first real control.undo after such a rename consumed that empty step instead
  of the real edit.
- Fix (commit 7317dec00, kept here): the handler returns a no-change success
  result - the same fields the real rename reports (name, renamed_to, path,
  device_count, dir) and NO __transaction - BEFORE the addUndoStep call when
  toPath == from. No undo step and no redo step is recorded for a rename that
  changes nothing, so undo stays one honest step.
- Sibling checked: mixer.rename_channel already no-ops on an unchanged name
  without recording a step; chain.apply / chain.save / chain.remove are not
  renames and are untouched. chain.rename's other two exits (occupied target
  and a failed QFile::rename) already refused without recording a step.
- No new control command was added, no file format changed, no data written by
  an older build is read differently: the store is a plain .zcp file per preset
  and the no-op path only ever renamed a file onto itself.

## Round-2 change (the review's blocking point)

tests/src/core/ControlChainPresetTest.cpp:renamingToTheCurrentNameRecordsNoUndoStep
no longer needs a plugin:
- The new helper writePresetFile() (tests/src/core/ControlChainPresetTest.cpp,
  anonymous namespace) writes a VALID preset file into the store with
  controlChainPresetDocument() and a hand-built <zenepluginstate> device
  document - the same helpers the file's own document round-trip check uses.
  chain.rename only READS the preset and moves its file; it never loads a
  device, so the check is now independent of which devices a build ships.
- Both QSKIP branches and the plugin-dependent setup (addInstrumentTrack /
  firstLoadableEffect / loadEffects) are gone from this check: the regression
  can no longer skip.
- Preserved: the real rename assertion, the same-name rename assertion and the
  single-undo assertion (one control.undo after the no-op rename must undo the
  REAL rename: the original name is back and the renamed one is gone).

## Commands and exit codes (all run in this worktree)

| step | command | exit |
|------|---------|------|
| RED (fix reverted) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5616.r2/logs/fix.patch` | 0 |
| RED build | `cmake --build build --target ControlChainPresetTest -j 8` | 0 |
| RED test | `cd build/tests && QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECK_RESULTS=set:1 ./ControlChainPresetTest renamingToTheCurrentNameRecordsNoUndoStep` | 1 (FAIL! 'storeNames().contains(kPreset)' returned FALSE) |
| GREEN (fix restored) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5616.r2/logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target ControlChainPresetTest -j 8` | 0 |
| GREEN test | `cd build/tests && QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECK_RESULTS=set:1 ./ControlChainPresetTest renamingToTheCurrentNameRecordsNoUndoStep` | 0 (3 passed, 0 failed, 0 skipped) |
| product build | `cmake --build build --target zene -j 8` | 0 |
| family | `cd build/tests && ctest -R "ControlChainPreset" --output-on-failure` | 0 (2/2 passed) |
| gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (RESULT: PASS) |

Logs (all under /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5616.r2/logs):
build-red2.log, test-red2.log (RED), build-green2.log, test-green3.log (GREEN),
build-zene.log, build-final.log, test-family3.log (chain family),
test-family-final.log (broad family), test-baseline-prefix.log (the broad
family's failing tests on the PRE-FIX build), feature-check.log, fix.patch.

## Broad family: the failures are pre-existing

`ctest -R "Undo|Reversib|Control"` (test-family-final.log) reports failures that
are NOT caused by this change:
- 59 of the failures are "Not Run": those test binaries are not built in this
  worktree's build tree, so ctest never started them.
- The 9 real failures (ControlSocketIntegration, ControlFeedbackCommands,
  ControlSuccessPaths, ControlRenderSurround, ControlMeterCommands,
  ControlGoldenAudio, ControlSocketPathSafety, ControlAbsentSocket,
  ControlMcpGroupCoverage) fail IDENTICALLY with the product fix REVERTED
  (test-baseline-prefix.log, ctest exit 8, the same 9 names). Their messages
  name the environment, not this change: "the LADSPA host plugin 'ladspaeffect'
  is not in this build", "no import filter in this build takes ... .mid", "no
  MIDI file was written ... the midiexport filter is not loadable in this
  process", and socket-path cases.
- ControlChainPresetTest and ControlChainPresets - the family that covers this
  change - pass (test-family3.log, exit 0).

## Self-check

- Regression FAILS without the fix and PASSES with it: RED run
  `.../ControlChainPresetTest renamingToTheCurrentNameRecordsNoUndoStep` exit 1
  (FAIL! 'storeNames().contains(kPreset)' returned FALSE, log test-red2.log);
  GREEN run of the identical command exit 0, 3 passed, 0 failed, 0 skipped
  (test-green3.log). The fix was reverted with `git apply -R logs/fix.patch` and
  restored with `git apply logs/fix.patch`; both applied cleanly (exit 0) and
  the tree was rebuilt in between (build-red2.log, build-green2.log).
- The regression can no longer skip: it writes its own valid preset file with
  controlChainPresetDocument() and needs no plugin, no instrument track and no
  effect; the QSKIP branches are removed. The RED run shows "2 passed, 1 failed,
  0 skipped".
- Deterministic: no sleeps, no wall-clock, no races. The only timing seam is
  control.set_undo_coalescing with window_ms 0, which pins "one command, one
  undo step" (the seam the product itself offers); the window is restored to
  control::UndoCoalesceWindowMs at the end of the check.
- Fix at the cause and complete: the cause is the no-op path of the chain.rename
  handler itself (src/core/ControlCommandsChainEdit.cpp:233-243). Its two
  siblings in the same handler (occupied target, failed rename) already refuse
  without recording a step, and they still do. No other command renames a chain
  preset (chain.save/apply/remove, chain.list/get_state are untouched), and the
  result fields returned by the no-op path are the same ones the real rename
  reports.
- No collateral damage: no store format, no project file and no saved data
  changed; the no-op path only ever renamed a file onto itself, and it now
  reports success without moving anything. No limit or check was added.
- Realtime/undo: the change is in a control-command handler (the control
  thread), not on the audio thread - nothing new allocates, frees, locks,
  waits or logs there. Undo stays ONE honest step: the no-op rename records
  none, and the real rename still records exactly one.
- No new control command was added for this fix.
- Registration and gates: the regression lives in the existing
  ControlChainPresetTest (registered in tests/CMakeLists.txt); the changed
  inherited files are declared in tests/upstream-modifications.txt with the
  reason, and the file ends with a newline. `git diff --check` is clean
  (the space-before-tab indentation of the round-1 commit is fixed in this
  commit). feature_check.py exits 0 with RESULT: PASS.
- Files committed in this round: src/core/ControlCommandsChainEdit.cpp
  (indentation only - the functional fix is commit 7317dec00),
  tests/src/core/ControlChainPresetTest.cpp (plugin-free regression),
  tests/upstream-modifications.txt (notes), and this report.
