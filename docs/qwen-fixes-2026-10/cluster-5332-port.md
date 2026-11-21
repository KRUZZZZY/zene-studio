# Cluster 5332-port — chain:removeEffect is not undoable

Verdict: **ALREADY-FIXED** on `bugs/qwen-5332-port` (based on `4eb8d6e8a`).
The product change of the original fix (`82c1fce2e`) is already in this tree,
in a stronger form. This commit adds only a regression test.

## Finding

`src/core/ScriptDawEdit.cpp:201` (historical): `applyRemoveEffect` removed the
effect and scheduled its deletion with no journalled inverse, so the user's undo
after `chain:removeEffect(0)` did not restore it.

## Current code (evidence)

- `src/core/ScriptDawEdit.cpp:231` — `if (!control::journalEffectRemoval(chain, effect, index))`:
  the removal is recorded before the device comes off.
- `src/core/ScriptDawEdit.cpp:233-237` — a device whose state cannot be captured is
  not removed; the script is told (`logRefusal`, owner decision 2026-10-08).
- `src/core/ScriptDawEdit.cpp:238-241` — `control::detachEffect` (one checkpoint: the
  structural step), then `effect->deleteLater()`.
- `src/core/ControlStructuralSupport.cpp:515` — `journalEffectRemoval`, the same
  helper `plugin.unload` uses (`src/core/ControlCommandsPlugin.cpp:357`).

## What was ported

The original commit `82c1fce2e` made the same core change (journal before the
removal) and added a test. That change is present here, and this tree also refuses
an uncapturable device. I did not port the original test as written: its
`undoDepth() == depth + 2` expectation conflicts with the current
`detachEffect` comment (`ScriptDawEdit.cpp:238-239`, one checkpoint). That is a
reading of the code, not a run of the original test.

New regression: `tests/src/core/ScriptDawBindingTest.cpp`, slot
`aScriptsRemovalOfAProbeDeviceIsJournalledOrRefused`. It runs on a probe device
(`UndoProbeEffect`), so it needs no plugin module. It checks that with capture
refused the device stays, the script is told, and no undo step is pushed; and that
with capture working the device is removed and the removal is exactly one undo step.

## Why not the existing removal-undo tests

`tests/src/core/ScriptDawRemovalUndoTest.cpp` already has slots for this path
(`aScriptsRemovedEffectIsUndoneByItsOwnStep`, `aScriptsRemovalThatCannotBeUndoneIsRefused`,
and others). In this targeted build they fail before the removal: `loadableDevice()`
returns catalogue entry `dev-0`, a LADSPA device that `controlDeviceCatalogue`
marks `loadable` (`src/core/ControlDeviceCatalogue.cpp:128`) but whose host module
`ladspaeffect` is not built here, so the load is refused
(`src/core/ControlDeviceHosted.cpp:87`; log `test-1-removalundo-green-current.log`:
`loaded 0` for the first slot). All 11 removal slots call `loadableDevice()` and all 11
fail, identical at HEAD and after this change. I traced the cause in the first slot's log only,
not for every slot. I did not change those slots.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5332-port.r1/logs`.
All builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-5332` (the default ccache temp dir
under `/run/user` is blocked by the write-jail; `build-1-zene-removalundo.log` EXIT=2 is that failure).

| Step | Command (run from the worktree / `build/tests`) | EXIT | Log |
| --- | --- | --- | --- |
| Pre-change baseline, removal-undo file at HEAD | `ctest -R ScriptDawRemovalUndoTest -V` | 8 | `test-1-removalundo-green-current.log` (2 pass, 11 fail) |
| Pre-change length gate | `bash tests/file-length-gate.sh --check` | 1 | `gate-0-length-before.log` |
| **RED** (journalless removal) | `cmake --build build --target zene ScriptDawBindingTest -j2`, then `./ScriptDawBindingTest aScriptsRemovalOfAProbeDeviceIsJournalledOrRefused` | build 0 / run **1** | `build-11-binding-RED-mutation.log`, `test-11-binding-probe-RED.log` — `FAIL! … Actual 0, Expected 1, ScriptDawBindingTest.cpp:483` |
| **GREEN** (fix present, first run in this file) | same build and run | build 0 / run 0 | `build-10-binding-green.log`, `test-10-binding-probe-GREEN.log` |
| **GREEN** (final, restored) | `cmake --build build --target zene ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest -j2`, then `./ScriptDawBindingTest aScriptsRemovalOfAProbeDeviceIsJournalledOrRefused` | build 0 / run 0 | `build-12-final.log`, `test-12-binding-probe-GREEN-final.log` (3 passed: init, slot, cleanup) |
| Affected family, final | `ctest -R ScriptDaw -V` in `build/tests` | 8 | `test-13-scriptdaw-family-final.log` |
| ↳ ScriptDawBindingTest | | pass | 12 passed, 1 skipped (`aScriptsLoadedEffectIsUndoneOnTheChannelItNames`: "this build has no amplifier module", pre-existing), incl. the new slot |
| ↳ ScriptDawEffectsTest | | pass | 7 passed, 0 failed |
| ↳ ScriptDawRemovalUndoTest | | fail | 2 passed, 11 failed — same 11 precondition failures as the pre-change baseline |
| Complexity gate (final) | `bash tests/complexity-gate.sh --check` | **1** | `gate-3-complexity-final.log` — 6 "new function over target" regressions, none in a file this commit touches; the new slot is not listed |
| File-length gate (final) | `bash tests/file-length-gate.sh --check` | **1** | `gate-4-length-final.log` — failure set identical to `gate-0-length-before.log` (19 files over, 14 regressions); `ScriptDawBindingTest.cpp` is at 401 gate lines (331 at HEAD), under 500 |

The mutation is the pre-fix removal from the bug report (`ScriptDawEdit.cpp:231-241` replaced by
`chain->removeEffect(effect); effect->deleteLater();`). Saved as
`logs/mutation-journalless-binding.patch`. I applied it by editing the file, not with
`git apply -R`, because the current tree has no product diff to reverse (the fix is in HEAD).
The file was restored from a backup and verified identical to HEAD (`git diff --quiet` EXIT 0)
before the GREEN run; `build-12` recompiled `ScriptDawEdit.cpp.o` and relinked `zene`.

Earlier RED runs in the removal-undo file (`test-3`, `test-7`, EXIT 1) hit the same
assertion (device removed under refused capture, lines 629 and 634 there); that
file's version was moved to the binding file, so they are superseded.

## Observations (not fixed here)

1. **Catalogue / instantiation mismatch (possible product bug).** `ControlDeviceCatalogue.cpp:128`
   marks a LADSPA entry `loadable` from the scanned LADSPA manager, without checking that
   the `ladspaeffect` host module exists; `ControlDeviceHosted.cpp:87` then refuses it. In a
   targeted build this makes the `loadable` flag promise something the build cannot do.
   Owner decision; I did not change it.
2. **Undo cap leak in a test.** `ScriptDawRemovalUndoTest.cpp` `aScriptsRemovedEffectIsUndoneOnceAtCapOne`
   sets `setMaxUndoStates(1)` and restores it only at the end. When it returns early on the
   precondition failure above, the cap of 1 stays for later slots in the process. My first draft
   of the regression in that file failed on exactly this (`undoDepth` 1, expected 2). My slot pins
   its own cap with a scope guard; the leak in the existing slot is untouched.
3. **Gates.** Both ratchet gates exit 1 at this commit, as they did before it; the failing
   entries are in files this commit does not touch. I did not re-anchor or edit any baseline.
4. **Editor diagnostics.** The IDE reports clang errors on the touched test files (e.g.
   "No matching constructor for initialization of 'UndoProbeEffect'"). The repo's GCC build of the
   same files has no warning or error (`build-12-final.log`: 0 matching lines); the IDE's
   compile flags differ.
