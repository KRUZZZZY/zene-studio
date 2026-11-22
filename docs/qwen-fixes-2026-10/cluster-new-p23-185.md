FALSE-POSITIVE

# Cluster new-p23-185 — OpulenZ leaves theEmulator/frameCount/renderbuffer uninitialized

## Verdict

The finding is false on this branch. The constructor does initialize all three
members, and it does so before the only object that can call `playImpl()` is
registered with the audio engine. No product change and no regression test were
made, because there is no defect for a test to prove.

## File:line evidence

| Claim | Evidence |
| --- | --- |
| `theEmulator`, `frameCount` and `renderbuffer` are the private members named by the finding | `plugins/OpulenZ/OpulenZ.h:125` (`Copl *theEmulator;`), `:127` (`f_cnt_t frameCount;`), `:128` (`short *renderbuffer;`) |
| The constructor creates and initializes the emulator | `plugins/OpulenZ/OpulenZ.cpp:146` `theEmulator = new CTemuopl(Engine::audioEngine()->outputSampleRate(), true, false);`, followed by `init()` and a register write at `:147-149` |
| The same constructor allocates and sizes the render buffer | `plugins/OpulenZ/OpulenZ.cpp:165` `frameCount = Engine::audioEngine()->framesPerPeriod();` and `:166` `renderbuffer = new short[frameCount];` |
| Void-pointer and integer members have exactly these assignments; nothing else writes them | `rg -n 'theEmulator = new CTemuopl\|frameCount = \|renderbuffer = new short'` reports `:146`, `:165`, `:166` in the constructor and `:237` (`reloadEmulator`, emulator only). `frameCount` is never assigned outside the constructor. |
| `playImpl()` can only run after those assignments | The constructor adds its `InstrumentPlayHandle` at `plugins/OpulenZ/OpulenZ.cpp:221-222`, after `:146` and `:165-166`. `playImpl()` itself is at `:396` and reads the three members at `:401` and `:407`. |
| `reloadEmulator()` does not depend on uninitialized members | `plugins/OpulenZ/OpulenZ.cpp:234-246` deletes and recreates only `theEmulator`; it neither reads `frameCount` nor `renderbuffer` and does not change either. The finding's suggested "call `reloadEmulator()` in the constructor" would therefore duplicate the constructor's own `:146-149`. |
| `updatePatch()` runs before `frameCount`/`renderbuffer` are assigned, but does not touch either | `plugins/OpulenZ/OpulenZ.cpp:162` calls `updatePatch()`; its body (`:552-607`) reads/writes emulator registers and `voiceNote`/`velocities` only. No buffer access occurs before `:166`. |

The same initialization block is already present in the branch base,
`050/m0-train-35`, so this is not a case of another lane having fixed it: the
defect described by the finding has never existed in this code. Evidence:
`logs/evidence-1.log`, produced by the `git show 050/m0-train-35:...` command
below.

## Runtime evidence that the members are initialized

`PluginPortsMigrationTest::opulenzRendersTheProvidedSpanSize`
(`tests/src/plugins/PluginPortsMigrationTest.cpp:5954`) constructs a real
OpulenZ instance through `lmms_plugin_main` at `:5971` and immediately calls
`instrument->play(...)` at `:5979` and `:5987`. If `theEmulator`,
`frameCount` or `renderbuffer` were uninitialized, that first call would
dereference garbage. The test passes. The registered CTest is
`tests/CMakeLists.txt:4717`.

## Test matrix

| Path | Coverage | Result |
| --- | --- | --- |
| First playback after construction | Existing `opulenzRendersTheProvidedSpanSize` constructs the plugin and calls `play()` immediately; it exercises the three members before any note or reload. | PASS (`test-1.log`) |
| Normal short and multi-period spans | Same test renders a half-period span and a two-period span and checks the written frame count. | PASS (`test-1.log`) |
| Failure / exhaustion | Not applicable to this finding: the report is a false positive and there is no new allocation or limit to exhaust. The existing zero-chunk guard test `OpulenZZeroFrameCount` still passes. | PASS (`test-1.log`) |
| Reconfiguration (`reloadEmulator`, sample-rate change) | The slot replaces only `theEmulator` under `emulatorMutex` at `OpulenZ.cpp:234-246`; `frameCount` is never touched and the allocated buffer stays valid. No uninitialized read exists on this path. | Code path checked |
| Audio-thread constraints | No code changed; the existing `playImpl` lock/update path is untouched. | N/A |
| Supported platforms | Built and tested only in this worktree's configured Linux/Qt build; no other platform build was available here. No platform-specific code is involved. | Linux PASS |

## Commands and exit codes

All builds ran from the worktree root; CTest ran from `build/tests`. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p23-185.r1/logs`.

| Command | Exit | Log |
| --- | ---: | --- |
| `{ echo ...; sed -n '144,166p' plugins/OpulenZ/OpulenZ.cpp; sed -n '122,135p' plugins/OpulenZ/OpulenZ.h; sed -n '220,223p' plugins/OpulenZ/OpulenZ.cpp; rg -n 'theEmulator = new CTemuopl\|frameCount = \|renderbuffer = new short\|theEmulator->update\|delete \[\] renderbuffer' plugins/OpulenZ/OpulenZ.cpp; git show 050/m0-train-35:plugins/OpulenZ/OpulenZ.cpp \| sed -n '144,166p'; }` (the exact block is in the log) | 0 | `evidence-1.log` |
| `cmake --build build --target PluginPortsMigrationTest -- -j4` | 0 | `build-1.log` |
| `ctest -R '^OpulenZ' --output-on-failure` | 0 | `test-1.log`: 4/4 tests passed (`OpulenZ`, `OpulenZSpanSize`, `OpulenZZeroFrameCount`, `OpulenZHz2fnumRange`) |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p23-185` | 0 | `gate-1.log`: all eight gates PASS |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p23-185 new-p23-185` | 0 | `submit-1.log`: `RESULT: PASS - submit` (before the final report-only commit; all mechanical rows PASS) |

The feature-gate table was:

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |

RED/GREEN reversal is not applicable: the verdict is FALSE-POSITIVE, no product
hunk exists to reverse, and no test was added. Nothing was built or run with a
reverted product change, and no RED or GREEN result is claimed.

## Self-check

- **Regression test fails with product change reverted and passes with it:**
  not applicable. There is no product change in this commit; the finding's
  premise (uninitialized members) is contradicted by the constructor itself.
  The existing runtime test `OpulenZSpanSize` was run and passed, which is the
  observable check that the constructed instance can render immediately.
- **Deterministic regression test:** none added. No timing, sleeps, races or
  test seams were introduced. The existing test uses fixed-size buffers.
- **Cause, callers and siblings:** the three members have one construction
  site (`OpulenZ.cpp:146`, `:165`, `:166`); `frameCount` has no later
  assignment; `theEmulator` is also replaced at `:237` by `reloadEmulator`,
  which keeps the initialized buffer. The only render entry point,
  `playImpl()` (`:396`), is reached through the play handle registered at
  `:221-222`, which is after all three assignments. The finding's proposed
  `reloadEmulator()` call in the constructor would be redundant with
  `:146-149`.
- **Collateral damage:** no product or test file changed, so project loading,
  saved data, undo and command behaviour are untouched. The only change is
  this report.
- **FALSE-POSITIVE evidence:** `plugins/OpulenZ/OpulenZ.cpp:146` (emulator),
  `:165` (`frameCount`), `:166` (`renderbuffer`); member declarations at
  `plugins/OpulenZ/OpulenZ.h:125`, `:127`, `:128`; play-handle registration
  after initialization at `OpulenZ.cpp:221-222`; first-play runtime proof in
  `tests/src/plugins/PluginPortsMigrationTest.cpp:5971` and `:5979` via the
  registered `OpulenZSpanSize` test.
- **Sibling commands:** not applicable. This commit changes only this report;
  it does not touch any `ControlCommands*` or reversibility file. The command
  siblings listed by `submit_check.py` (`app.*`, `arrangement.*`, ...) come
  from train merges already present between the gate base `fa4a5f2e7` and this
  branch, not from this cluster, so none is affected.
- **Completion check:** `submit_check.py ... new-p23-185` exited 0 with
  `RESULT: PASS - submit` (`submit-1.log`). Its `tests added or changed` row is
  satisfied by pre-existing merge content in `base..HEAD`; this cluster itself
  adds no product code and no test, as required for a false-positive verdict.

## Other findings

- Not fixed here (outside this finding): `velocities[128]` is declared at
  `plugins/OpulenZ/OpulenZ.h:133` and is not initialized by the constructor.
  `handleMidiEvent`'s `MidiKeyPressure` branch reads `velocities[key]` at
  `OpulenZ.cpp:332` before checking whether that key ever had a note-on, which
  is an uninitialized read for an aftertouch event on an untouched key. The
  named members of this finding are unaffected; report this separately rather
  than widening this cluster's patch.
