# cluster new-p11-82 — Delay time labeled "samples" but valued in seconds

**Verdict: FIXED**

## Finding

`plugins/Delay/DelayControls.cpp:36` constructs the delay-time model with display
name `tr("Delay samples")`, but the value is in **seconds**: the effect multiplies
it by the output sample rate, and the dialog presents it with `" s"` units. The
displayed name is what the device card and `plugin.param_get`/`param_set` read and
write parameters by, so the unit shown to the user is wrong.

## Evidence (file:line)

| What | Where |
|---|---|
| Buggy label | `plugins/Delay/DelayControls.cpp:36` — `m_delayTimeModel(0.5f, 0.01f, 5.f, 0.0001f, 5000.f, this, tr("Delay samples"))` |
| Value is seconds: multiplied by sample rate | `plugins/Delay/DelayEffect.cpp:121` — `m_currentLength = static_cast<int>(*lengthPtr * Engine::audioEngine()->outputSampleRate());` |
| Dialog shows seconds units | `plugins/Delay/DelayControlsDialog.cpp:51` — `setHintText(tr("Delay time:"), " s")` |
| Display name is the user-visible parameter name | `src/core/ControlDeviceSupport.cpp:335` — `controlParameterJson` inserts `model->displayName()` as `"name"`; enumerated at `ControlDeviceSupport.cpp:307` |
| Fix applied | `plugins/Delay/DelayControls.cpp:36` — label renamed to `tr("Delay time (s)")` |

The save-key (`"DelayTimeSamples"`) and rendering are untouched; this is a
display-name-only, behaviour-preserving rename.

## Regression test

New test `tests/src/plugins/DelayControlsTest.cpp` (registered in `tests/CMakeLists.txt`,
manifested in `tests/all-sources.txt`). It instantiates a `DelayEffect`, enumerates its
parameters through the same `controlEffectParameters()` the device card and
`plugin.param_get` use, identifies the delay-time model by its unique tempo-sync
`scale() == 5000.f` (LFO knobs use 20000/2000 — independent of the name), and asserts
the displayed name is `"Delay time (s)"`.

## Commands run (EXIT codes)

RED — test fails on unfixed code (wrong predicate first, then corrected):

```
ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-1.log 2>&1   # EXIT=8  (RED, unfixed)
```

GREEN — test passes with the fix:

```
cmake --build build --target DelayControlsTest > LOGS/build-4.log 2>&1      # EXIT=0
ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-3.log 2>&1   # EXIT=0  (GREEN)
```

4b proof — the test catches the bug via reverse-apply of the product patch:

```
git diff -- plugins/Delay/DelayControls.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch   # APPLY_R_EXIT=0
cmake --build build --target DelayControlsTest > LOGS/build-5.log 2>&1                        # EXIT=0
ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-4.log 2>&1                     # EXIT=8  (FAIL: actual "Delay samples")
git apply LOGS/fix.patch                                                                      # APPLY_EXIT=0
cmake --build build --target DelayControlsTest > LOGS/build-6.log 2>&1                        # EXIT=0
ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-5.log 2>&1                     # EXIT=0  (PASS)
```

Plugin target rebuilt after the product change:

```
cmake --build build --target delay > LOGS/build-7.log 2>&1                  # EXIT=0
```

## Evidence table

| Item | Result |
|---|---|
| RED command | `ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-4.log 2>&1` → **EXIT=8** (reverse-applied fix; assertion `Compared values are not the same`, actual `"Delay samples"`) |
| GREEN command | `ctest -R '^DelayControlsTest$' --output-on-failure > LOGS/test-5.log 2>&1` → **EXIT=0** (100% tests passed, 1/1) |
| Affected-family tests executed | 1 (`DelayControlsTest`); `ctest -R '^Delay'` matched no pre-existing test |
| complexity-gate.sh --check | **EXIT=0** |
| file-length-gate.sh --check | **EXIT=0** |
| Files committed | `plugins/Delay/DelayControls.cpp`, `tests/src/plugins/DelayControlsTest.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `docs/qwen-fixes-2026-10/cluster-new-p11-82.md` |

## Notes

- `tests/reference/Delay/DelayControls.cpp` is frozen migration evidence (ORIGIN.tsv blobs) — not touched.
- `plugins/Flanger/FlangerControls.cpp` carries the same label but is outside this finding's scope — untouched.
- LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-82.r1/logs`
