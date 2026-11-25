FIXED

# Cluster new-p10-198 - an invalid LADSPA effect key can divide by zero

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p10-198`
(branch `bugs/qwen-new-p10-198`). Logs:
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-198.r1/logs/`.

## Decision

The finding has two halves. The first (divide by zero) is **ALREADY-FIXED on this
branch tip**; the second (a plugin wider than the stereo bus makes zero processors)
was **REAL** and is fixed here. The verdict line is `FIXED` because the second half
changes product code.

### Half 1 - zero input channels: already fixed, with proof

`plugins/LadspaEffect/LadspaEffect.cpp:343-359` (this branch tip) rejects a plugin
whose `inputChannels` is zero *before* the division:

```
343  const int effect_channels = description->inputChannels;
344  if( effect_channels == 0 )
...
357          setOkay( false );
358          return;
359  }
```

`git log --oneline -- plugins/LadspaEffect/LadspaEffect.cpp` on this branch shows the
commit that added it: `32335cfa8b Fix LADSPA audio-path mutex and zero-input effect
(clusters 4179, new-p15-94)`. Its regression
`PluginPortsMigrationTest::ladspaEffectRejectsZeroInputPlugin` still passes in the
runs below (`test-2-green.log`, `test-9-green-final.log`), and the RED evidence for
that guard is recorded in `docs/qwen-fixes-2026-10/cluster-4179.md` (SIGFPE exit 136
before the guard). Descriptor *type* needs no separate check:
`src/core/LadspaManager.cpp:180-202` derives `type` from the two channel counts, so
`LadspaPluginType::Source` is exactly `inputChannels == 0 && outputChannels > 0` -
the existing guard already covers it, and a second test of the same fact would be
dead code.

### Half 2 - more inputs than the bus: real, reproduced, fixed

Before this change, `pluginInstantiation()` divided unconditionally
(`plugins/LadspaEffect/LadspaEffect.cpp:384` on the tip, the same line the finding
calls "~235" at its own revision):

```
m_processors = DEFAULT_CHANNELS / effect_channels;
```

`DEFAULT_CHANNELS` is `ch_cnt_t{2}` (`include/lmms_constants.h:38`), so a plugin with
four audio inputs yields `2 / 4 == 0` processors. Nothing else caught it: the layout
check that follows (`LadspaEffect.cpp:410-436`) compares
`processors * mappedInputs > DEFAULT_CHANNELS`, which is `0 > 2` - false - for every
port layout, so the effect was accepted. I reproduced that on the unfixed tree: the
accepted effect reports `processorCount() == 0` and `isOkay() == true` and allocates
its controls (`test-1-red.log`, `test-3-reverted-red.log`):

```
FAIL!  : PluginPortsMigrationTest::ladspaEffectRejectsMoreInputsThanTheEffectBus()
 'effect->processorCount() != 0 || !effect->isOkay()' returned FALSE.
 (a four-input LADSPA plugin cannot be split across the two-channel effect bus: the
 wrapper made zero processors and still accepted the effect, which then processes nothing)
   Loc: [../../tests/src/plugins/PluginPortsMigrationTest.cpp(2284)]
```

The consequence is a silently inert effect: `processImpl`'s copy, run and merge loops
all iterate `processorCount()`, so the audio passes through untouched while the FX
slot shows a plugin with no knobs and no diagnostic.

Fix, at the cause and before any port buffer exists
(`plugins/LadspaEffect/LadspaEffect.cpp:360-383`):

```
360  // The processor count splits the effect's channels across the plugin's audio
...
365  if( effect_channels > static_cast<int>( DEFAULT_CHANNELS ) )
366  {
367      const QString reason = QStringLiteral( "LADSPA plugin %1 has %2 audio "
368              "inputs and cannot be split across the %3-channel effect bus" )
...
381      setOkay( false );
382      return;
383  }
```

It mirrors the two guards already in the same function: same warning path
(`lmms::isUnattendedRun()` -> `qWarning()`, otherwise one `QMessageBox`), same
`setOkay( false )`. A refused effect becomes a `DummyEffect` with its saved XML
preserved (`src/core/EffectChain.cpp:234-246`), which is exactly what the zero-input
and output-overflow guards already do.

### Callers and siblings checked

- `pluginInstantiation()` has two callers: the constructor
  (`LadspaEffect.cpp:118`) and `changeSampleRate()` (`LadspaEffect.cpp:159`).
  `changeSampleRate()` returns early when `m_controls == nullptr`
  (`LadspaEffect.cpp:137-140`), so a refused effect is never re-instantiated.
- `LadspaSubPluginFeatures.cpp:145` only offers plugins with
  `inputChannels <= DEFAULT_CHANNELS` in the browser, so no effect the user can add
  from the browser is newly rejected; only a hand-edited or foreign project key can
  reach the new guard.
- No other site divides by `inputChannels` (`plugins/LadspaEffect/` has exactly the
  three `inputChannels` uses listed by a search of the directory: the two guards and
  the division).
- Layouts the wrapper already accepted keep their behaviour: 1-in/1-out
  (`2 / 1 = 2` processors), 2-in/2-out (`2 / 2 = 1`), 2-in/1-out. The sibling tests
  `ladspaEffectRejectsZeroInputPlugin`, `ladspaEffectRejectsOutputChannelOverflow`
  and `ladspaEffectProcessLockTakesNoExclusiveMutex` pass unchanged
  (`test-9-green-final.log`).

### Realtime, undo, commands

The guard runs in `pluginInstantiation()` only (GUI / project-load thread, before
handles and buffers exist). `processImpl`, `processLock` and `processUnlock` are
untouched: nothing is added to the audio thread - no allocation, free, lock, wait or
log. No undo step, no saved-data format change, no control command (the
ControlCommandsSnapshot canary is unchanged; this plugin has no command handler).

## Test matrix

| Path | Covered by |
|---|---|
| Normal path (1-in, 2-in layouts still load and process) | `ladspaEffectRejectsOutputChannelOverflow`'s accepted layouts, `ladspaEffectProcessLockTakesNoExclusiveMutex` (real 1-in effect accepted), `LadspaChannelCountsTest`, `LadspaGlobalLinkTest` |
| Failure path (4-in layout must be refused) | new `ladspaEffectRejectsMoreInputsThanTheEffectBus` |
| Exhaustion / zero processors | the same test: requires `processorCount() != 0` *or* a refusal, then requires `!isOkay()` and `controls() == nullptr` |
| Reconfiguration path (`changeSampleRate`) | untouched; `changeSampleRate` bails on `m_controls == nullptr`, and `ladspaEffectSurvivesFailedInstantiationsDuringSampleRateChange` / `ladspaControlDialogSurvivesFailedInstantiationsDuringSampleRateChange` still pass in the full binary run |
| Audio-thread constraints | no change to `processImpl`/`processLock`; existing realtime tests unaffected |
| Supported platforms | fixture is a plain CMake MODULE with the existing MSVC `/EXPORT:ladspa_descriptor` handling (`tests/CMakeLists.txt:9537-9551`); no new target, no new source file, no POSIX-only code |

## Regression test

- `tests/src/plugins/PluginPortsMigrationTest.cpp:1065` (slot declaration) and
  `:2246-2292` (body), `ladspaEffectRejectsMoreInputsThanTheEffectBus()`. It loads
  the channel-count fixture, constructs the real migrated `LadspaEffect` through its
  `lmms_plugin_main` for the key `ladspaeffect:quad_in_stereo_out`, and requires the
  effect to be refused with no controls. No thread, clock, sleep or race is involved:
  the effect is constructed on the test thread and the suite already runs with
  `QT_QPA_PLATFORM=offscreen`.
- `tests/src/plugins/LadspaChannelCountFixture.cpp:117-189` adds the
  `quad_in_stereo_out` descriptor (four audio inputs, two audio outputs, hard-RT
  capable, id `170024`), exported as `ladspa_descriptor(3)`
  (`LadspaChannelCountFixture.cpp:198`). Indices 0-2 are unchanged, and
  `LadspaChannelCountsTest` (which reads descriptors 0 and 1) still passes.

## Commands and EXIT codes

All commands ran in the worktree; builds used `-j4`.

| Step | Command | EXIT |
|---|---|---:|
| Build RED (test+fixture only) | `cmake --build build --target PluginPortsMigrationTest -- -j4 > .../logs/build-1-red.log 2>&1` | 0 |
| RED run | `cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsMoreInputsThanTheEffectBus > .../logs/test-1-red.log 2>&1` | 1 |
| Build GREEN | `cmake --build build --target ladspaeffect PluginPortsMigrationTest -- -j4 > .../logs/build-2-green.log 2>&1` | 0 |
| GREEN run (new + three sibling LADSPA tests) | `cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsMoreInputsThanTheEffectBus ladspaEffectRejectsZeroInputPlugin ladspaEffectRejectsOutputChannelOverflow ladspaEffectProcessLockTakesNoExclusiveMutex > .../logs/test-2-green.log 2>&1` | 0 |
| 4b: write fix.patch | `git diff -- plugins/LadspaEffect/LadspaEffect.cpp > .../logs/fix.patch` | 0 |
| 4b: reverse it | `git apply -R .../logs/fix.patch` | 0 |
| 4b: rebuild reverted | `cmake --build build --target ladspaeffect PluginPortsMigrationTest -- -j4 > .../logs/build-3-reverted.log 2>&1` | 0 |
| 4b: test must FAIL | `cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsMoreInputsThanTheEffectBus > .../logs/test-3-reverted-red.log 2>&1` | 1 |
| 4b: restore | `git apply .../logs/fix.patch` | 0 |
| 4b: rebuild restored | `cmake --build build --target ladspaeffect PluginPortsMigrationTest zene -- -j4 > .../logs/build-4-restored.log 2>&1` | 0 |
| 4b: test must PASS | `cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsMoreInputsThanTheEffectBus > .../logs/test-4-green-restored.log 2>&1` | 0 |
| Family binaries | `cmake --build build --target LadspaChannelCountsTest LadspaManagerTest LadspaGlobalLinkTest -- -j4 > .../logs/build-5-family-targets.log 2>&1` | 0 |
| LADSPA family | `cd build/tests && ctest -R '^(LadspaManagerTest\|LadspaChannelCountsTest\|LadspaGlobalLinkTest)$' --output-on-failure > .../logs/test-10-ladspa-family.log 2>&1` | 0 |
| Whole test binary (collateral check) | `ctest -R '^(LadspaManagerTest\|LadspaChannelCountsTest\|LadspaGlobalLinkTest\|PluginPortsMigrationTest)$' --output-on-failure > .../logs/test-6-family.log 2>&1` | 8 (4 pre-existing failures, see below) |
| Attribution: build base tree | `cmake --build build --target ladspaeffect ladspa_channel_count_fixture PluginPortsMigrationTest -- -j4 > .../logs/build-6-base.log 2>&1` (all three files restored from `HEAD` first) | 0 |
| Attribution: run the 4 failures at base | `cd build/tests && ./PluginPortsMigrationTest nonRealtimeLadspaEffectsAreInvalid malletsVoiceLimitIsEnforcedPerInstrument malletsVoiceLimitSurvivesDeviceChange slice8PluginsAreLiveAndExact > .../logs/test-8-base-failing.log 2>&1` | 4 |
| Same four with this change | `cd build/tests && ./PluginPortsMigrationTest <same four> > .../logs/test-7-failing-individually.log 2>&1` | 4 |
| Final rebuild | `cmake --build build --target ladspaeffect ladspa_channel_count_fixture PluginPortsMigrationTest zene -- -j4 > .../logs/build-7-final.log 2>&1` | 0 |
| Final green | `cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsMoreInputsThanTheEffectBus ladspaEffectRejectsZeroInputPlugin ladspaEffectRejectsOutputChannelOverflow ladspaEffectProcessLockTakesNoExclusiveMutex > .../logs/test-9-green-final.log 2>&1` | 0 |

`test-5-family.log` (EXIT 8) is the earlier family attempt before the other three
test binaries were built: ctest reported `LadspaChannelCountsTest ... ***Not Run`.
`test-10-ladspa-family.log` is the rerun with the binaries present: `100% tests
passed out of 3`.

Registration: `tests/upstream-modifications.txt` gains one line each for
`plugins/LadspaEffect/LadspaEffect.cpp`,
`tests/src/plugins/LadspaChannelCountFixture.cpp` and
`tests/src/plugins/PluginPortsMigrationTest.cpp`, naming cluster new-p10-198. No new
product file, so `tests/fork-sources.txt` is untouched; both test files were already
in `tests/all-sources.txt`.

## Self-check

- *Verdict backed by evidence?* Yes: `ALREADY-FIXED` half cites
  `LadspaEffect.cpp:343-359` plus the commit that added it and the still-passing
  `ladspaEffectRejectsZeroInputPlugin`; the `REAL` half cites the pre-fix division,
  the guard that multiplies by zero, and the RED failure text above.
- *Does the test fail without the fix?* Yes - step 4b ran it twice that way
  (`test-1-red.log` before the fix, `test-3-reverted-red.log` with `fix.patch`
  reversed), EXIT 1 both times, and it passes with the fix (`test-2-green.log`,
  `test-4-green-restored.log`, `test-9-green-final.log`), EXIT 0.
- *Fixed at the cause, callers and siblings consistent?* Yes - the guard sits before
  the division and before any buffer; both callers covered, browser filter unchanged,
  the three existing layouts behave as before.
- *Collateral damage?* The four failures in the whole `PluginPortsMigrationTest`
  binary (`nonRealtimeLadspaEffectsAreInvalid`, `malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange`, `slice8PluginsAreLiveAndExact`) fail
  identically on the base tree (`test-8-base-failing.log`, EXIT 4) and with this
  change (`test-7-failing-individually.log`, EXIT 4). `LadspaChannelCountsTest`,
  `LadspaManagerTest` and `LadspaGlobalLinkTest` pass.
- *Realtime / undo / commands?* No audio-thread code touched; no undo step, no
  control command, no saved-data format change.
- *Registration?* The three ledger lines above; `tests/upstream-modifications.txt`
  still ends with a newline.

## Other findings

- Four tests in `PluginPortsMigrationTest` already fail on this branch tip without
  this change (environment fixtures): `nonRealtimeLadspaEffectsAreInvalid`
  (`decimator_1202.so` from the SWH LADSPA set is not on `LADSPA_PATH` here),
  `malletsVoiceLimitIsEnforcedPerInstrument` and `malletsVoiceLimitSurvivesDeviceChange`
  (Mallets renders silence), and `slice8PluginsAreLiveAndExact` (`malletsstk` render
  is null). Evidence: `test-8-base-failing.log` (base tree, EXIT 4). Not fixed here.
