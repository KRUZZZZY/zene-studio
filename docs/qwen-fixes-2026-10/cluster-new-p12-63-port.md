FIXED

# Cluster new-p12-63-port — Default recording paths reuse the same take file

new-p12-63-port: FIXED

## Verdict

**REAL**, and fixed here for the one recording default that still reused its take file on this
branch.

The finding names "default recording paths". On this branch there are three default take paths,
and two of them were already repaired before this lane started:

- `record.arm_track` with no `file` → `takePathArg()` reserves a free name
  (`src/core/ControlRecordingSupport.cpp:320-345`, allocator at `:296-318`, bound
  `include/ControlRecordingSupport.h:93`).
- `track.set_arm` with no `file` → `armTrackArm()` reserves the same way and releases on a failed
  arm (`src/core/ControlCommandsArrangementState.cpp:210-245`).
  Both came from `7ca94243880` (`[new-p17-24] reserve default take names ...`), an ancestor of
  HEAD, with their own tests in `tests/src/core/ControlRecordingRoutesTest.cpp`
  (`secondDefaultArmDoesNotTruncateFirstTake`, `trackSetArmDefaultSecondTakeIsDistinct`, …), which
  I ran again in the family run below.

The third default was still broken: `record.retro_capture_to_take` with no `file` wrote the fixed
`zene-retro-take.wav` beside the recovery file, and `writeRetroAudioTake()` replaces its destination
on `commit()`. The second default retro take therefore silently destroyed the audio the first one
wrote — no error, no warning, `ok: true`.

## Evidence (file:line)

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The default retro path was one fixed name, reused by every take | `src/core/ControlCommandsRecordingRetro.cpp:170-172` at HEAD before this commit (`git diff` in `logs/fix.patch`) | `const QString path = file.isEmpty() ? QDir(defaultRecoveryDir()).absoluteFilePath(QStringLiteral("zene-retro-take.wav")) : file;` |
| E2 | Writing that path replaces what was there | `src/core/RetroAudioCapture.cpp:305-307` (`QSaveFile output(filePath)`, `setDirectWriteFallback(false)`) and `:366-371` (`output.commit()`) | the staged sibling is renamed over the destination, so an existing take is replaced rather than refused |
| E3 | The neighbour that refuses collision instead proves the intent | `src/core/ControlCommandsSamplerTake.cpp:314-319` | a landed sampler take refuses an existing destination ("nothing was overwritten and the take stays in hand") — the retro verb had no such guard |
| E4 | The allocator the fix reuses | `src/core/ControlRecordingSupport.cpp:296-318`, declared `include/ControlRecordingSupport.h:96-106` | exclusive creation (`QIODevice::NewOnly`) per candidate, `<base>.wav`, `<base>-1.wav`, …, skipping any candidate whose WAV or journal exists; `TakeNameProbeBound = 1000` makes exhaustion deterministic |
| E5 | The fix | `src/core/ControlCommandsRecordingRetro.cpp:170-185`, `:195-200` | a default take reserves the next free name (`zene-retro-take.wav` first, so the documented default still holds for the first take), refuses typed on exhaustion, and releases the reservation when the write fails so a retry gets the name back |
| E6 | The other two defaults were already fixed before this lane | `git log -1 --format='%H %s' -- src/core/ControlRecordingSupport.cpp` → `7ca94243880…`; `git merge-base --is-ancestor 7ca94243880 HEAD` → 0 | route/track defaults reserve: `ControlRecordingSupport.cpp:335`, `ControlCommandsArrangementState.cpp:214` |

Callers and siblings audited (`writeRetroAudioTake` has three):

- `record.retro_capture_to_take` — `ControlCommandsRecordingRetro.cpp:195`: fixed here.
- `record.sampler_capture_land` (sampler take) — `ControlCommandsSamplerTake.cpp:325`: its default
  is `TakeRecorder::defaultTakePath()` (`src/core/TakeRecorder.cpp:202-212`), a timestamped name
  with a collision loop, and it refuses an existing destination. Unchanged, correct.
- `session.audio_record_land` — `ControlCommandsSessionAudioRecord.cpp:265`: same
  `defaultTakePath()`; writes the take the cell names. Unchanged, correct.
- `record.recovery_restore` — takes the journal's own `take` path, never invents a name.

## RED / GREEN proof (logs in `runs/new-p12-63-port.r9/logs`)

`fix.patch` is the product diff (`git diff -- src/core/ControlCommandsRecordingRetro.cpp`), not the
test. Test file: `tests/src/core/RetroTakeDefaultPathTest.cpp` (new; its own file because
`ControlRecordingRoutesTest.cpp` is at the file-length ratchet's 500-code-line ceiling).

| Step | Command | EXIT | Result |
|---|---|---|---|
| diff | `git diff -- src/core/ControlCommandsRecordingRetro.cpp > logs/fix.patch` | 0 | 40-line product diff |
| reverse | `git apply -R logs/fix.patch` | 0 | `logs/step-4b-red.txt`: only the test files remain modified |
| build | `cmake --build build --target zene RetroTakeDefaultPathTest ControlRecordingRoutesTest -j4 > logs/build-9-4b-red-new.log 2>&1` | 0 | `Built target ControlRecordingRoutesTest` |
| RED | `cd build/tests && QT_QPA_PLATFORM=offscreen ./RetroTakeDefaultPathTest > logs/test-10-4b-red.log 2>&1` | **2** | `FAIL! … 'secondPath != firstPath' returned FALSE. (the second default retro take reused …/zene-retro-take.wav)`; also `defaultRetroTakeExhaustionRefusesWithoutWriting` FAIL; `Totals: 4 passed, 2 failed` |
| apply | `git apply logs/fix.patch` | 0 | `logs/step-4b-apply.txt` |
| build | `cmake --build build --target zene RetroTakeDefaultPathTest ControlRecordingRoutesTest -j4 > logs/build-11-4b-green.log 2>&1` | 0 | rebuilt |
| GREEN | `cd build/tests && ctest -R '^RetroTakeDefaultPathTest$\|^ControlRecordingRoutesTest$\|^RetroAudioCaptureTest$' --output-on-failure > logs/test-12-4b-green.log 2>&1` | **0** | `100% tests passed out of 3` (`RetroTakeDefaultPathTest`, `ControlRecordingRoutesTest`, `RetroAudioCaptureTest`) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature_check-2.log 2>&1` | **0** | every row `PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

An earlier RED/GREEN pair (`logs/test-1-red.log` EXIT 8, `logs/test-2-green.log` EXIT 0) ran the
same cases inside `ControlRecordingRoutesTest.cpp`; that placement failed the file-length gate
(`logs/feature_check-1.log`: `NEW: new file over 500 lines: tests/src/core/ControlRecordingRoutesTest.cpp (523)`),
so the cases moved to their own file. Both files are reverted/staged respectively, and the numbers
above are the ones from the layout that ships.

## Test matrix

| Path | Case | Test | Result |
|---|---|---|---|
| normal | first default retro take → `<recovery dir>/zene-retro-take.wav`, 8 frames written | `defaultRetroTakeDoesNotReuseTheFirstTakesFile` | PASS (`test-12-4b-green.log`) |
| normal | second default retro take → a distinct file; the first take's bytes unchanged | same test | FAIL before the fix (EXIT 2), PASS after |
| normal | an explicit absolute `file` is written where asked and reserves nothing; the next default take still gets the base name | `explicitRetroTakeFileLeavesTheDefaultNameFree` | PASS |
| failure | the write's final move refused (`retroaudiotesting::failNextTakeMove()`): typed refusal, no file left in the directory, the next take gets the same name | `refusedRetroTakeWriteLeavesNoReservation` | PASS |
| exhaustion | all `control::TakeNameProbeBound` (1000) candidates occupied → typed `Refused`, the occupied file untouched; freeing one makes the take possible | `defaultRetroTakeExhaustionRefusesWithoutWriting` | FAIL before the fix (it wrote over the occupied file), PASS after |
| reconfiguration | each case points the recovery directory at its own `QTemporaryDir` (`ConfigManager::setWorkingDir`, restored by `qScopeGuard`), so the default follows the instance's directory | all four | PASS |
| audio thread | the change is inside a command handler on the control thread; `RetroAudioCapture::push()`/`takeWindow()` are untouched, so nothing new allocates, locks, waits or logs on the audio path | `RecordingRealtimeTest` (13.16 s, PASS in `test-4-family.log`) | PASS |
| undo | the command was and stays `mutating = false` (`ControlCommandsRecordingRetro.cpp:309` before and after); no undo step is added or removed | `ControlRecordingRoutesTest` undo cases | PASS |
| platforms | only `QFile`/`QDir`/`QFileInfo`/`QString` are used through `reserveDefaultTake`; no POSIX-only path test, no Qt-6-only API | gates `check-namespace`, `no-upstream-regression` | PASS |

Family run (`logs/test-4-family.log`, EXIT 0): `ControlRecordingRoutesTest`, `MultiTrackRecorderTest`,
`RetroAudioCaptureTest`, `RecordingRealtimeTest` — `100% tests passed out of 4`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `./RetroTakeDefaultPathTest` → EXIT **2** (`test-10-4b-red.log`, two cases fail on the
  reported defect). Restored: `ctest -R '^RetroTakeDefaultPathTest$|…'` → EXIT **0**
  (`test-12-4b-green.log`). Both runs are of the same binary rebuilt from the same sources; the only
  difference is `logs/fix.patch`.
- **The test is deterministic.** No sleeps, no wall clock, no races: the retained window is produced
  by driving `RetroAudioCapture::push()` with 8 frames of a known value at a fixed 48 000 Hz, the
  refused write uses the existing one-shot `retroaudiotesting::failNextTakeMove()` seam, and every
  case owns its `QTemporaryDir`. The candidate space is bounded by `control::TakeNameProbeBound`, so
  exhaustion is a deterministic refusal rather than a scan.
- **At the cause and complete.** The cause is the fixed default name in `retroToTake`; the fix
  allocates through the same control-thread allocator the other two defaults use, refuses typed on
  exhaustion, and releases the reservation when the write fails. Callers and siblings are listed
  above; the two `TakeRecorder::defaultTakePath()` callers were checked and need no change
  (timestamped name plus a collision loop, and the sampler take refuses an existing file).
- **No collateral damage.** An explicit `file` behaves exactly as before (same validation, no
  reservation, no release). The first default take still lands on the historical
  `zene-retro-take.wav`, so the command's description ("defaults to zene-retro-take.wav beside this
  instance's recovery file") stays true and the command snapshot does not move — no command, no id,
  no schema changed. Old projects and saved data are untouched: the change writes a new file where
  it used to overwrite one. The 1000-candidate bound is the existing one, not a new limit, so no
  other caller is newly rejected.
- **Sibling commands in the touched file** (`record.*`): `record.retro_capture_arm` and
  `record.retro_capture_status` are mode/status reads with no path; `record.retro_capture_to_take`
  is this fix; `record.arm_track`/`record.disarm_track`/`record.disarm_all` already reserve through
  `takePathArg`/`releaseDefaultTake` and are covered by their own tests; `record.get_state`,
  `record.remaining`, `record.input_*` name no take file. `track.set_arm` (the other path into the
  same allocator) was checked and already reserves. No sibling was left inconsistent.
- **Realtime.** `retroToTake` runs on the UI/control thread like every handler; the audio-thread
  entries (`RetroAudioCapture::push`, the recorder taps) are not touched by this diff, and
  `RecordingRealtimeTest` passes.

## Other findings

- `docs/CAPABILITIES-0.3.0.md` and `tools/mcp-zene-control/zene_control/commands_snapshot.json` carry
  the retro description "defaults to zene-retro-take.wav beside this instance's recovery file". It is
  still true for the first take in an empty directory but does not say later takes get `-1`, `-2`.
  Left as generated text on purpose: regenerating the snapshot for wording would move the
  ControlCommandsSnapshot canary for a bug fix. Worth a separate docs job.
- `#include <QDir>` in `src/core/ControlCommandsRecordingRetro.cpp:54` is now unused (the default
  path no longer formats a directory). Left in place to keep the product diff minimal.
- `tests/src/core/ControlRecordingRoutesTest.cpp` is measured by the whole-tree file-length ratchet,
  which lists only files over 500 code lines: adding these four cases there took it to 523 and turned
  the gate red (`logs/feature_check-1.log`), so the cases live in their own file instead.
