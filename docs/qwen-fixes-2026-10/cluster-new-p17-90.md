BLOCKED

Cluster new-p17-90: "The Mallets 'Bowed' setting has no visible control". The finding is REAL. The fix is written, but it has NOT been built or run: the lane's configured build tree has no target for the new test, and reconfiguring is refused in this lane. RED and GREEN were not observed, so nothing below claims a test result.

## Evidence that the finding is real (HEAD b61e5f965, before this change)

- `plugins/Stk/Mallets/Mallets.cpp:557-558`: the "Bowed" `LedCheckBox` creation is commented out in `setupBandedWGControls`.
- `plugins/Stk/Mallets/Mallets.cpp:599`: its model binding (`m_strikeLED->setModel( &inst->m_strikeModel )`) is commented out in `modelChanged`.
- `plugins/Stk/Mallets/Mallets.h:280`: the `m_strikeLED` member is commented out.
- The setting still changes the sound and is persisted:
  - `plugins/Stk/Mallets/Mallets.cpp:88` declares `m_strikeModel( true, ... )`, so the default is bowed.
  - `:156` and `:191` save and load the `strike` attribute.
  - `:369` passes `m_strikeModel.value() * 128.0` into the banded synth (the `else` branch, presets 10 and up).
  - `:400` adds `m_strikeModel.value() * freq * 2.5f` for preset 10 with the old-version flag.
- Provenance: the comment-out is already present at the fork's base `4e677cb6c6ab` (`plugins/Stk/Mallets/Mallets.cpp:560-561` and `:602`). It is inherited, not introduced by this fork.

## The change (at the cause; three lines restored, nothing else)

- `plugins/Stk/Mallets/Mallets.cpp:557-558`: the checkbox is live code again, `m_strikeLED = new LedCheckBox( tr( "Bowed" ), widget )` placed at `(138, 25)`.
- `plugins/Stk/Mallets/Mallets.cpp:599`: `m_strikeLED->setModel( &inst->m_strikeModel )` is restored in `modelChanged`.
- `plugins/Stk/Mallets/Mallets.h:280`: the `LedCheckBox * m_strikeLED;` declaration is restored (`LedCheckBox.h` is already included there).
- Audio behaviour is unchanged for existing projects: the default is `true`, so the checkbox opens in the state the sound already uses, and loaded `strike` values reach the model exactly as before.
- No new control command, no change to the audio path, no change to the save format.
- Ordering: `m_strikeLED` is created in `setupBandedWGControls`, next to `m_pressureKnob` and `m_velocityKnob`, which `modelChanged()` already dereferences (`Mallets.cpp:431-452`). It adds no new ordering dependency.
- Not changed, on purpose: the Motion and Vibrato knobs stay commented out (`Mallets.cpp:564-566`, `:572-574`). That is a separate question and outside this bug.

## Regression test

- New file `tests/src/plugins/MalletsInstrumentViewTest.cpp`, test `theBowedCheckBoxIsBoundToTheStrikeSetting`:
  - before the fix it fails at `QVERIFY2` ("the BandedWG view has no Bowed checkbox");
  - after the fix it checks four things: the checkbox exists; it defaults checked with `strike="1"` saved; a click writes `strike="0"` and then `"1"`; and a loaded `strike="0"` unchecks it.
- Registered in `tests/CMakeLists.txt`: an `LMMS_TESTS` entry gated on `LMMS_HAVE_STK`, and a `MalletsInstrumentViewTest` block that compiles `plugins/Stk/Mallets/Mallets.cpp`/`.h`, sets `PLUGIN_NAME=malletsstk`, passes `MALLETS_TEST_STK_DIR` from `STK_RAWWAVE_ROOT`, links `unofficial::libstk::libstk`, and sets `QT_QPA_PLATFORM=offscreen`. This follows the `PatmanLoadTest` pattern.
- The test points `ConfigManager` at the STK rawwave directory first. Without it the view opens a modal "missing files" dialog (`Mallets.cpp:464-470`), which would block an offscreen test.
- The existing `^Mallets$` ctest runs `tests/reference/Stk/Mallets/`, which is the frozen Part C reference copy (`git log` of that path: `cfdb7a558`). It compares samples and cannot see a missing control, so it was not used and was not edited.
- Registration: `tests/all-sources.txt` (new entry, sorted before `MissingDevicesNoEntryModule.cpp`), and `tests/upstream-modifications.txt` (reasoned lines for `Mallets.cpp`, `Mallets.h` and `tests/CMakeLists.txt`).

## Commands run, with EXIT codes

| step | command | EXIT | log (LOG FOLDER) |
| --- | --- | --- | --- |
| RED build, attempt 1 | `cmake --build build --target MalletsInstrumentViewTest -j4` | 2 | `build-red-1.log`: `No rule to make target 'MalletsInstrumentViewTest'`; the jobserver fifo was also refused |
| RED build, attempt 2 | `env -u MAKEFLAGS -u MFLAGS cmake --build build --target MalletsInstrumentViewTest -j4` | 2 | `build-red-2.log`: same "No rule" error |
| reconfigure | `env -u MAKEFLAGS -u MFLAGS cmake -S . -B build` | 97 | `configure-1.log`: `bunny-guard: configuring is the parent's job; the lane build tree is already configured.` |
| feature gate, as given | `python3 .../feature_check.py <worktree>` | 1 | the script tried to create `<worktree>-feature-check`, outside the write-jail (`PermissionError`); no gate result |
| feature gate, log dir moved | copy of `feature_check.py` in `/tmp/fc-new-p17-90/`: same checks, `ROOT` pinned to the qwen dir, logs written to `feature-check-1/` in the LOG FOLDER | 1 | `gate-1.log` and `feature-check-1/*.log` (see below) |
| RED (step 3) | not run: target not in the configured build tree | — | — |
| GREEN (step 4) | not run | — | — |
| affected family `^Mallets$`, new test count | not run | — | — |
| step 4b (revert, RED, re-apply, GREEN) | not run | — | — |

Gate result from the copy, before the commit (`gate-1.log`): `fork-sources` FAIL and `all-sources-reproduce` FAIL, both on the same line. The manifests are derived from committed history (`git diff --diff-filter=A 4e677cb6c6ab HEAD`), so the uncommitted test entry is "in the manifest but not derived". Everything else passed: `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item in my files), `file-length` (no new item in my files). The gate was re-run after the commit; see the Self-check section.

## What the parent must do before this can be verified

1. From the worktree, reconfigure the lane tree: `cmake -S . -B build`. The lane cannot do this (guard, exit 97).
2. `cmake --build build --target MalletsInstrumentViewTest -j4`, logged. Expect the build to compile `Mallets.cpp` into the test.
3. RED per procedure 4b on the committed fix: `git show <fix-commit> -- plugins/Stk/Mallets > fix.patch && git apply -R fix.patch`, rebuild, `ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` from `build/tests`. Expect the "no Bowed checkbox" failure. Then `git apply fix.patch`, rebuild, run again. Expect a pass.
4. Run `^Mallets$` as well, to confirm the Part C reference still passes.

Open risks I could not check by reading alone:

- `LedCheckBox::setModel` must set the checked state from the model when it is bound. I assumed this; the test's first `isChecked()` assertion checks it.
- The embedded `artwork` pixmap is not compiled into the test (no `EMBEDDED_RESOURCES`). If `PluginPixmapLoader` needs it, the view should still build, but the background may be blank. This does not affect the checkbox assertions.
- Any `cmake --build` on this tree after the `tests/CMakeLists.txt` edit may try to regenerate and hit the same guard. I inferred this from file times and did not run it.

## Self-check

(Filled in after `submit_check.py`; see the section below.)
