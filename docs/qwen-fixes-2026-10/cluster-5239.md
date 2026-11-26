FIXED

# Cluster 5239 - a renderer's dither/SRC publication outlived the wrong renderer

## The verdict, and the code it rests on

REAL. `ProjectRenderer` published its two render-scope choices into the
process-wide `ExportRenderSettings` and, until this fix, remembered a private
"previous value" snapshot that its destructor put back:

- the constructor published both values - `src/core/ProjectRenderer.cpp:157`
  (`m_previousDither(ExportRenderSettings::ditherMode())`),
  `:158` (`m_previousSrcQuality(...)`), `:160`/`:161` (the two setters);
- the destructor put *that renderer's* snapshot back - `src/core/ProjectRenderer.cpp:221`
  and `:222`;
- the values are process-wide atomics - `src/core/ExportRenderSettings.cpp:35`,
  `:36`, read by the render path through
  `AudioResampler::modeForCurrentSrcQuality()` (`src/core/AudioResampler.cpp:645`)
  and `Sample::PlaybackState`'s default argument (`include/Sample.h:51`).

A per-object snapshot of shared state is only correct when renderers die in the
reverse order of their construction. With two alive at once - which the public
class permits, and which a nested export does - the OLDER renderer's destruction
put the pre-render selection back **under** the newer one while it still had to
render. That is the defect the finding describes, and it is reachable in one
process, deterministically, without any two renders running at the same time:

```
renderer A built  -> A saves Linear,       publishes SincBest   (in force: SincBest)
renderer B built  -> B saves SincBest,     publishes SincBest   (in force: SincBest)
A destroyed       -> A restores Linear     (in force: Linear)   <-- B's choice lost
B renders                                   -> at Linear, not SincBest
```

It also leaks: the last renderer restores whatever *its* snapshot held, so the
process-wide selection after the pair is not the one that was in force before
them.

Scope, stated honestly: two renders *running* at the same time are not supported
by this engine at all (`ProjectRenderer::startProcessing()` installs its file
device as the engine's - `src/core/ProjectRenderer.cpp:268`, and `Song`/the
`AudioEngine` are process-wide), so the finding's "A renders while B renders"
half is not reachable. The half that IS reachable - overlapping LIFETIMES, and a
destructor restoring under a live renderer - is what the fix below makes correct;
no caller today holds two renderers (`RenderManager::renderNextTrack` resets the
old one before `render()` builds the next, `src/core/RenderManager.cpp:70`/`:105`),
which is why this never showed up as a field bug, but the class contract in
`include/ExportRenderSettings.h` promised it and the test proves it was broken.

The dither half of the finding does not reach the bytes: `AudioFileWave` reads
`getOutputSettings().ditherMode()` (the render's own copy) - `src/core/audio/AudioFileWave.cpp:207`,
`:268` - so a lost dither publication changed only what `export.get_settings`
reported and what the next `OutputSettings` inherited, not a rendered file. The
SRC half does reach the bytes, via the playback states described above.

## The fix (at the cause)

The snapshot is gone; a render now owns a **publication** of its selection and
retires it, and the selection in force is always the newest **live** one:

- `include/ExportRenderSettings.h:109-144` - `RenderSelection` (token +
  whether the SRC transaction applied), `publishRenderSelection()`,
  `retireRenderSelection()`; the class contract comment now says "newest LIVE
  publication wins" instead of "restores the previous values".
- `src/core/ExportRenderSettings.cpp:55-92` - the registry (a mutex-guarded
  vector of live publications, the pre-first-publication value, and a monotonic
  token counter). Control threads only; the audio/render path keeps reading the
  existing relaxed atomics and never takes that lock.
- `src/core/ExportRenderSettings.cpp:188-236` - `publish` saves the pre-render
  value only for the FIRST live publication and applies the new one; `retire`
  erases its own token and re-applies the newest publication still alive, or the
  pre-render value when none is left. `reset()` (line 175) also drops live
  publications, so a reset cannot be undone by a later retirement.
- `include/ProjectRenderer.h:170` - one `ExportRenderSettings::RenderSelection`
  member replaces `m_previousDither` / `m_previousSrcQuality`;
  `src/core/ProjectRenderer.cpp:158-159` publishes in the constructor (same two
  values, same moment as before) and `:223` retires in the destructor. Every exit
  route - finish, abort, refused SRC quality, no file device - still retires,
  because the publication is a member and the destructor always runs.

Behaviour with one renderer is byte-identical: the same values are applied at the
same moment, and the same pre-render values come back when it is destroyed
(`aNestedRenderersDestructionRestoresTheSelection` is a guard on exactly that,
and the export/dither/resampler family still passes).

## Test matrix

| Path | Test | Result |
|---|---|---|
| Normal: one renderer publishes, the setting is in force, the pre-render setting returns on destruction | `ExportWavDitherTest::aNestedRenderersDestructionRestoresTheSelection` (inner destroyed first) | PASS before and after (guard) |
| Failure the finding names: older renderer destroyed while a newer one is alive | `ExportWavDitherTest::aLiveRenderersSelectionSurvivesAnotherRenderersDestruction` | FAIL before (exit 8), PASS after (exit 0) |
| Exhaustion: the last retirement puts back the pre-first selection, nothing leaks | second test above, and the final assertions of both | PASS |
| Reconfiguration: a second renderer publishing the same non-default pair reconfigures the engine and its selection survives the older one's death | the same regression test, run with an engine present (the SRC transaction goes through `AudioEngine::onSrcQualityChanged`, `src/core/AudioEngine.cpp:401`) | PASS |
| Audio thread | publication/retirement happens only in the constructor/destructor (control thread); the period path reads the unchanged atomics | no lock, allocation or log added to any audio/render function |
| Platform | only `<mutex>`, `<vector>`, `<algorithm>`, `<cstdint>` - no Qt-6-only API, no POSIX call | builds with the tree's `-Werror` |

## Commands and EXIT codes (all logs in `~/.config/kads/zene/state/runs/5239.r1/logs/`)

RED (test first, product unchanged):

```
cmake --build build --target ExportWavDitherTest -j4 > logs/build-1.log 2>&1        EXIT=0
cd build/tests && ctest -R ExportWavDitherTest --output-on-failure > logs/test-1-red.log 2>&1
                                                                                    EXIT=8
```

`logs/test-1-red.log`: `FAIL!  : ExportWavDitherTest::aLiveRenderersSelectionSurvivesAnotherRenderersDestruction()
... Actual (ExportRenderSettings::srcQuality()): 0  Expected (SrcQuality::SincBest): 3
Loc: [../../tests/src/core/ExportWavDitherTest.cpp(530)]`, `Totals: 12 passed, 1 failed`.

GREEN:

```
cmake --build build --target zene ExportWavDitherTest -j4 > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ctest -R ExportWavDitherTest --output-on-failure > logs/test-2-green.log 2>&1
                                                                                    EXIT=0
cd build/tests && ./ExportWavDitherTest aLiveRenderersSelectionSurvivesAnotherRenderersDestruction \
      aNestedRenderersDestructionRestoresTheSelection > logs/test-3-functions.log 2>&1  EXIT=0
      # PASS aLiveRenderersSelectionSurvivesAnotherRenderersDestruction()
      # PASS aNestedRenderersDestructionRestoresTheSelection()
      # Totals: 4 passed, 0 failed
```

4b - the test catches the bug (product hunks only):

```
git diff -- include/ExportRenderSettings.h src/core/ExportRenderSettings.cpp \
   include/ProjectRenderer.h src/core/ProjectRenderer.cpp > logs/fix.patch          DIFF_EXIT=0
git apply -R logs/fix.patch                                                       REVERSE_APPLY_EXIT=0
cmake --build build --target zene ExportWavDitherTest -j4 > logs/build-4-unfix.log 2>&1  EXIT=0
cd build/tests && ctest -R ExportWavDitherTest --output-on-failure > logs/test-5-unfix-red.log 2>&1
                                                                                    EXIT=8
git apply logs/fix.patch                                                          REAPPLY_EXIT=0
cmake --build build --target zene ExportWavDitherTest -j4 > logs/build-5-refix.log 2>&1  EXIT=0
cd build/tests && ctest -R ExportWavDitherTest --output-on-failure > logs/test-6-refix-green.log 2>&1
                                                                                    EXIT=0
```

`logs/test-5-unfix-red.log` carries the same `FAIL!` at line 530 with `Actual 0` /
`Expected 3`; `logs/test-6-refix-green.log` is `100% tests passed out of 1`.

The `ctest -R` family (every C++ test that touches the changed code):

```
cmake --build build --target AudioResamplerRatioTest AudioResamplerPoolMemoryTest ExportDitherTest \
  ExportNoiseShaperTest ExportWavWriteErrorTest SurroundExportTest StemExportTest ExportChannelsTest \
  -j4 > logs/build-3.log 2>&1                                                       EXIT=0
cd build/tests && ctest -R "^(AudioResamplerRatioTest|AudioResamplerPoolMemoryTest|ExportDitherTest|\
ExportNoiseShaperTest|ExportWavDitherTest|ExportWavWriteErrorTest|SurroundExportTest|StemExportTest|\
ExportChannelsTest)$" --output-on-failure > logs/test-4-family.log 2>&1              EXIT=0   (9/9)
```

Re-run against the final, re-fixed binaries: `logs/test-7-final-family.log`, EXIT=0, 9/9.

Gates:

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1
                                                                                    EXIT=0
```

`logs/gates-1.log`: `PASS fork-sources`, `PASS all-sources-reproduce`,
`PASS no-upstream-regression`, `PASS check-namespace`, `PASS no-tautology`,
`PASS doc-figures`, `PASS complexity`, `PASS file-length`, `RESULT: PASS`.

Registration: `include/ProjectRenderer.h` and `src/core/ProjectRenderer.cpp` are
inherited files - both already in `tests/upstream-modifications.txt`, and this
change appends a cluster-5239 reason for each. `ExportRenderSettings.{h,cpp}` are
fork-NEW, already in `tests/fork-sources.txt`; no new file was added.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with
it (exit codes).** Reverted: `git apply -R logs/fix.patch` (EXIT=0), rebuild
(EXIT=0), `ctest -R ExportWavDitherTest` -> **EXIT=8**, `FAIL!` at
`ExportWavDitherTest.cpp:530`, `Actual 0 / Expected 3`
(`logs/test-5-unfix-red.log`). Restored: `git apply logs/fix.patch` (EXIT=0),
rebuild (EXIT=0), same command -> **EXIT=0**, `100% tests passed out of 1`
(`logs/test-6-refix-green.log`). The first RED run before the fix was written
was the same failure (`logs/test-1-red.log`, EXIT=8).

**2. The test is deterministic: no timing, sleeps or races.** It uses no thread,
no sleep and no clock: it builds two `ProjectRenderer` objects in one thread
(each constructor opens a Wave file in a `QTemporaryDir`; no render is started),
destroys them in a chosen order and reads `ExportRenderSettings::srcQuality()`,
`AudioResampler::modeForCurrentSrcQuality()` and `ditherMode()` between the
steps. The seam is the object lifetime itself - the same seam the product uses
(`RenderManager` builds/destroys renderers). Nothing is sampled or waited for.

**3. The fix is at the cause and complete; callers and siblings checked.**
Siblings checked: (a) `ControlCommandsExport`'s `export.set_dither` /
`export.set_src_quality` (`src/core/ControlCommandsExport.cpp:151-190`,
`:242`) set the selection for the NEXT render and carry their own undo value -
they do not publish, and they behave exactly as before; (b) `OutputSettings`'
defaults still read the same two getters (`include/OutputSettings.h:82-83`);
(c) the engine's transactional SRC path is untouched and still the only way a
quality change is committed (`src/core/AudioEngine.cpp:279-302`, `:403`);
(d) `reset()` clears live publications so a reset cannot be reverted by a later
retirement. Grep for `m_previousDither` / `m_previousSrcQuality` returns nothing.

**4. No collateral damage.** With one renderer the values are published and
restored at exactly the same moments as before, so old projects, CLI renders,
stems, surround exports and the control surface see no change; the
`aNestedRenderersDestructionRestoresTheSelection` guard pins the old
(nested) behaviour, and the 9-test family passes. No new limit or check rejects
a caller: the registry has no capacity bound (a `std::vector`, control thread
only), and a token that was already retired or dropped by `reset()` is ignored
rather than treated as an error.

**5. Verdict evidence.** REAL - `src/core/ProjectRenderer.cpp:157-161` and
`:221-222` before the fix (see `logs/fix.patch`, which is that exact diff), with
the contract they broke stated in `include/ExportRenderSettings.h:55-58` before
the fix and the process-wide reads at `src/core/AudioResampler.cpp:645` and
`include/Sample.h:51`.

Undo and commands: no control command was added or changed, and no undo path is
touched (the report's diff has no `ControlCommands*`, `Song*`, `*Undo*` or
`*Record*` file). Realtime: nothing added to the constructor/destructor path runs
per period; the period path still reads two relaxed atomics.

## Other findings

- The finding's "A renders with B's dither" cannot happen: the dither a render
  writes comes from the render's own `OutputSettings` copy
  (`src/core/AudioFileWave.cpp:207`, `:268`). Only the SRC quality is read back
  from the process-wide holder during a render.
- Two renders running simultaneously are not supported by this engine
  (`ProjectRenderer::startProcessing()` installs its own device in the singleton
  `AudioEngine`). That is a design fact, not a defect fixed here; the fix makes
  lifetime overlap correct, and a caller that truly needs two renders at once
  needs a second process (which is what `project.render`, `bounce.*`,
  `freeze.tracks` and `mastering.run` already do).
- `ProjectRenderer::~ProjectRenderer()` discards the result of the SRC
  restoration (as the old destructor discarded its own). A failed restore
  increments `ExportRenderSettings::srcQualityPropagationFailures()`, which is
  the existing telemetry for it; changing that is outside this cluster.
