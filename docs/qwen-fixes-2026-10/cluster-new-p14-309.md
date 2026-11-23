ALREADY-FIXED

## Finding

- Cluster: `new-p14-309`
- File: `plugins/Sf2Player/Sf2Player.cpp` (historical line 459)
- Claim: `openFile()` releases `m_synthMutex` and then calls `updatePatch()`, which calls
  `fluid_synth_program_select()` without the mutex, so a SoundFont load or patch change during
  playback can race with `renderFrames()`.

## Verdict evidence

The bug was real in the code the finding describes, and it is already fixed on this branch by the
SF2 realtime rewrite (`370df75734`, "realtime note tracking and lock-free synth hand-off", and
`d73caeacaf`, "a render path that cannot wait, a reload that keeps the font"; both are ancestors of
HEAD). There is no `m_synthMutex` left (`rg -n m_synthMutex plugins/Sf2Player` finds nothing), and
`updatePatch()` no longer calls FluidSynth at all.

Current product code:

- [`plugins/Sf2Player/Sf2Player.cpp:650`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:650)
  `Sf2Instrument::updatePatch()` only publishes the requested numbers:
  `m_requestedBank.store(...)` and `m_requestedPatch.store(...)`. It does not touch the playing
  synth, so the UI thread never calls `fluid_synth_program_select()`.
- [`plugins/Sf2Player/Sf2Player.cpp:611`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:611)
  `openFile()` ends with that `updatePatch()` call; there is no `m_synthMutex` release and no
  program select on the UI thread.
- [`plugins/Sf2Player/Sf2Player.cpp:1188`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:1188)
  `Sf2Instrument::applyControls()` runs on the render thread and applies the published bank/patch
  there: `fluid_synth_program_select( m_synth, ... )` is at
  [`plugins/Sf2Player/Sf2Player.cpp:1276`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:1276).
  The synth runs with FluidSynth's thread-safe API off, so the render path takes no mutex.
- [`plugins/Sf2Player/Sf2Player.cpp:1120`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:1120)
  `serviceSynthHandoff()` adopts the UI-built synth from a bounded lock-free queue at the start of
  the render period; the UI side publishes through
  [`plugins/Sf2Player/Sf2Player.cpp:466`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.cpp:466)
  `publishSynth()`. The UI-side hand-off mutex is documented in
  [`plugins/Sf2Player/Sf2Player.h:402`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/plugins/Sf2Player/Sf2Player.h:402)
  as never taken by the audio thread.
- `createPatchDialogSynth()` (`plugins/Sf2Player/Sf2Player.cpp:855`) still calls
  `fluid_synth_program_select()`, but on a synth of the dialog's own, never on the render synth,
  so it is not a sibling of the race.

Historical proof that this was the reported bug, taken from the parent of the fix commit:

```
$ git show 370df75734^:plugins/Sf2Player/Sf2Player.cpp | sed -n '436,458p'
	updatePatch();          // <- openFile, after m_synthMutex.unlock()
}

void Sf2Instrument::updatePatch()
{
	if( m_bankNum.value() >= 0 && m_patchNum.value() >= 0 )
	{
		fluid_synth_program_select( m_synth, m_channel, m_fontId,
				m_bankNum.value(), m_patchNum.value() );   // <- no mutex
	}
}
```

Existing regression coverage: `Sf2PlayerRealtimeTest::renderPathAppliesTheState`
([`tests/src/plugins/Sf2PlayerRealtimeTest.cpp:230`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309/tests/src/plugins/Sf2PlayerRealtimeTest.cpp:230))
asserts that a UI-side player update is published, not applied, and only reaches the synth on the
next render period; the same file also asserts the synth's own thread-safe API is off and the
render path allocates nothing. The full `Sf2PlayerRealtimeTest` suite passes on this branch.

## Commands and results

All output in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-309.r1/logs/`.

```
$ cmake --build build --target Sf2PlayerRealtimeTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?
EXIT=0

$ cd build/tests && ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?
EXIT=0
1/1 Test #234: Sf2PlayerRealtimeTest ............   Passed    1.11 sec

$ git show 370df75734^:plugins/Sf2Player/Sf2Player.cpp ... > .../logs/evidence-history.log 2>&1; echo EXIT=$?
EXIT=0

$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-309 \
    > .../logs/feature-check-1.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

No product code was changed, so there is no product diff to reverse for a red/green run; the
verdict rests on the code above and the existing test, as the procedure allows for
ALREADY-FIXED.

## Self-check

- **Regression test reverted / restored:** not applicable: this report changes no product code. The
  existing deterministic regression `Sf2PlayerRealtimeTest::renderPathAppliesTheState` passed on the
  current tree (`ctest` EXIT=0 above). The historical defect is shown by the pre-fix source in
  `evidence-history.log`.
- **Determinism:** the existing test drives the player's own `play()`/`updateGain()` seam directly
  and compares synth state before and after a render period; it uses no sleeps, wall-clock timing
  or thread races.
- **Cause and completeness:** every caller of `updatePatch()` was checked: constructor, `loadFile()`,
  `openFile()` and the two model `dataChanged` connections; all run on the UI thread and only store
  atomics. The only remaining `fluid_synth_program_select()` calls are the render-thread application
  (`Sf2Player.cpp:1276`) and the dialog's private synth (`Sf2Player.cpp:855`). No sibling path
  applies a patch to the render synth from the UI thread.
- **No collateral damage:** no project, save or model format was touched. Patch selection still
  round-trips because `m_bankNum`/`m_patchNum` and `saveSettings()`/`loadSettings()` are unchanged;
  only the moment the synth receives the request moved to the render period, which is the intended
  contract from clusters new-p9-453/454.
- **ALREADY-FIXED citation:** `Sf2Player.cpp:650-655` (UI only publishes), `Sf2Player.cpp:1276`
  (render thread applies), `Sf2Player.cpp:1120-1176` and `Sf2Player.h:402-408` (lock-free hand-off;
  UI-side mutex never taken by audio). Fix commits `370df75734` and `d73caeacaf` are ancestors of
  HEAD.
