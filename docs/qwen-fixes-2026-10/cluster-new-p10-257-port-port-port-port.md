FIXED - project XML can no longer load a non-finite mixer float (surround downmix, strip, snapshots, channel/send/VCA), cluster new-p10-257-port-port-port-port

# Cluster new-p10-257-port-port-port-port: NaN values in project XML can reach audio processing

**Verdict: FIXED**

REAL on this newest line. The XML float readers of the mixer trusted the file:

- `src/core/MixerSurround.cpp:288` (before the fix) returned `ok ? std::clamp(value, low, high) : fallback`.
  `std::clamp` leaves NaN unchanged, so `<surrounddownmix centre="nan">` became the centre coefficient of
  every matrix, and an overflowing literal (`1e999`) converted to infinity with `ok == false`.
- `src/core/MixerStrip.cpp:359-372` (before) called `setValue(attribute(...).toFloat())` for trim, the three EQ
  bands, EQ mid frequency, pan-right and width - a NaN reached the trim gain and the biquad coefficients.
- `src/core/MixerSnapshots.cpp:132-140` (before) stored snapshot volume, pan and send amount/pan unchecked, so
  recalling a snapshot put them back into the mixer.
- `src/core/Mixer.cpp` (before): channel `volume`/`pan`, regular and sidechain send `amount`, send `pan`, and
  VCA group `vca` went straight into `FloatModel::loadSettings` (`:2845`, `:2852`, `:2912-2913`, `:2958`,
  `:2993` before the fix), including the child-element and automation-clip forms of that loader.

This is the port of the accepted `bugs/qwen-new-p10-257-port-port-port` fix (`0867dc20a`, QA follow-up
`98772c1f3`), re-applied by hand because `src/core/Mixer.cpp` had moved on this line. No merge, cherry-pick or
rebase was used. All four remaining commits of the source cluster are documentation/verdict commits
(`2061c8eb7`, `53f644509`) whose content is this report.

## Evidence (current file:line)

| Evidence | Location |
| --- | --- |
| Downmix reader rejects NaN and overflow before clamping / matrix construction | `src/core/MixerSurround.cpp:284-291` |
| Strip float attributes use a finite fallback (trim, EQ low/mid/midfreq/high, pan-right, width) | `src/core/MixerStrip.cpp:72-77`, load at `:366-380` |
| Snapshot channel and send floats use a finite fallback | `src/core/MixerSnapshots.cpp:129-134`, `:139-151` |
| Mixer channel volume/pan, regular/sidechain send amount and send pan, VCA gain skip a non-finite model value | `src/core/Mixer.cpp:150-210`, callers `:2913`, `:2920`, `:2980-2984`, `:3029`, `:3064` |
| Automation bend is part of the non-finite point check (the QA follow-up) | `src/core/Mixer.cpp:168-173` |
| Conversion is checked regardless of `ok`, so `1e999` is caught | `src/core/Mixer.cpp:154-159` |
| `std::clamp` leaves NaN unchanged (the matrix hole) | `src/core/MixerSurround.cpp:289-290` |
| The raw loader it guards | `src/core/AutomatableModel.cpp:287-345`; `src/core/AutomationNode.cpp:134-138` |

Old projects and finite values load exactly as before: only a parsed NaN/±inf/overflow is replaced by the
same default the reader already used for a missing attribute (`0`, `1`, `1000`, the -3 dB downmix default).
The change is XML load code on the project-open path, not the audio thread; it adds no allocation, lock,
wait, log, undo step or control command.

## Regression tests

Four suites, seven new test slots - all deterministic, injecting literal XML attributes through `QDomDocument`:

| Test | File | Was RED as |
| --- | --- | --- |
| `nonFiniteDownmixSettingsAreIgnored` | `tests/src/core/ProjectStateTest.cpp` | `downmixOptions().centre` NaN, expected 0.7071068 |
| `nonFiniteSnapshotFloatsAreReplaced` | `tests/src/core/ProjectStateTest.cpp` | `channels.front().volume` NaN, expected 1 |
| `nonFiniteStripSettingsAreReplaced` | `tests/src/core/MixerStripTest.cpp` | `m_trimModel.value()` NaN, expected 0 |
| `nonFiniteMixerSettingsAreIgnored` | `tests/src/core/MixerStripTest.cpp` | `m_volumeModel.value()` 2 from `1e999`, expected 1 |
| `nonFiniteMixerSettingsWithMismatchedNodeNameAreIgnored` | `tests/src/core/MixerStripTest.cpp` | `m_volumeModel.value()` NaN from a mismatched `<volume nodename="another-model">` child |
| `nonFiniteMixerAutomationBendIsIgnored` | `tests/src/core/MixerStripTest.cpp` | `valueAt(TimePos(50))` not finite |
| `nonFiniteVcaGainDoesNotReachMemberChannel` | `tests/src/core/VcaGroupTest.cpp` | `vcaModel()->value()` not finite |

The downmix test lives in `ProjectStateTest.cpp` rather than `SurroundTest.cpp`: `SurroundTest.cpp` is
already at 499 code lines against the Gate 7 limit of 500, so adding to it would have created a new
file-length regression. `ProjectStateTest.cpp` is the file that already owns the snapshot/project-element
regressions, and it asserts the same `surround::` surface.

## Commands and EXIT codes

The original fix attempt recorded build/test logs under `build/qwen-logs/`. During QA, the mandated log
folder was writable (`test -w .../logs` -> EXIT=0); QA logs are under the numbered files in that folder.

| Step | Command | EXIT |
| --- | --- | ---: |
| RED build | `cmake --build build --target SurroundTest MixerStripTest ProjectStateTest VcaGroupTest -j8` (`build-1-red.log`) | 0 |
| RED test | `cd build/tests && ctest -R '^SurroundTest$' --output-on-failure` (`test-1-red-SurroundTest.log`) | 8 |
| RED test | `ctest -R '^MixerStripTest$'` (`test-1-red-MixerStripTest.log`) | 8 |
| RED test | `ctest -R '^ProjectStateTest$'` (`test-1-red-ProjectStateTest.log`) | 8 |
| RED test | `ctest -R '^VcaGroupTest$'` (`test-1-red-VcaGroupTest.log`) | 8 |
| GREEN build | `cmake --build build --target zene SurroundTest MixerStripTest ProjectStateTest VcaGroupTest MixerWindowsTest -j8` (`build-5-final.log`) | 0 |
| GREEN family test | `cd build/tests && ctest -R '^(SurroundTest|MixerStripTest|ProjectStateTest|VcaGroupTest|MixerWindowsTest)$' --output-on-failure` (`test-4-family-green.log`) | 0 (5/5) |
| Save patch | `git diff -- src/core/Mixer.cpp src/core/MixerSnapshots.cpp src/core/MixerStrip.cpp src/core/MixerSurround.cpp > build/qwen-logs/fix.patch` | 0 |
| Reverse patch | `git apply -R build/qwen-logs/fix.patch` | 0 |
| Reverted build | `cmake --build build --target zene SurroundTest MixerStripTest ProjectStateTest VcaGroupTest -j8` (`build-6-reverted.log`) | 0 |
| Reverted test | `ctest -R '^(SurroundTest|MixerStripTest|ProjectStateTest|VcaGroupTest)$' --output-on-failure` (`test-6-reverted.log`) | 8 (3 of 4 failed, 6 new slots) |
| Restore patch | `git apply build/qwen-logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene SurroundTest MixerStripTest ProjectStateTest VcaGroupTest -j8` (`build-7-restored.log`) | 0 |
| Restored test | `ctest -R '^(SurroundTest|MixerStripTest|ProjectStateTest|VcaGroupTest)$' --output-on-failure` (`test-7-restored.log`) | 0 (4/4) |
| QA build | `cmake --build build --target zene MixerStripTest -j12` (`<LOG_FOLDER>/qa-review-03-qa-fix-build.log`) | 0 |
| QA family tests | `cd build/tests && ctest -R '^(SurroundTest|MixerStripTest|ProjectStateTest|VcaGroupTest|MixerWindowsTest)$' --output-on-failure` (`<LOG_FOLDER>/qa-review-04-qa-fix-family-ctest.log`) | 0 (5/5) |
| Reverse QA guard | `git apply -R build/qwen-logs/reviewer/qa-fix.patch` | 0 |
| Reversed QA build | `cmake --build build --target zene MixerStripTest -j12` (`<LOG_FOLDER>/qa-review-05-reversed-qa-fix-build.log`) | 0 |
| Reversed QA test | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure` (`<LOG_FOLDER>/qa-review-06-reversed-qa-fix-ctest.log`) | 8 (`nonFiniteMixerSettingsWithMismatchedNodeNameAreIgnored` fails) |
| Restore QA guard | `git apply build/qwen-logs/reviewer/qa-fix.patch` | 0 |
| Restored QA build/test | `cmake --build build --target zene MixerStripTest -j12`; `ctest -R '^MixerStripTest$'` (`<LOG_FOLDER>/qa-review-07-restored-qa-fix-build.log`, `qa-review-08-restored-qa-fix-ctest.log`) | 0 / 0 |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-257-port-port-port-port` (`<LOG_FOLDER>/qa-review-12-feature-check-final.log`) | 0 |

RED assertions observed in `test-6-reverted.log` (the reversed-fix run):

- `VcaGroupTest::nonFiniteVcaGainDoesNotReachMemberChannel` - `'std::isfinite(loaded->vcaModel()->value())' returned FALSE`.
- `MixerStripTest::nonFiniteStripSettingsAreReplaced` - compared floats are not the same.
- `MixerStripTest::nonFiniteMixerSettingsAreIgnored` - compared floats are not the same (`1e999` -> 2, expected 1).
- `MixerStripTest::nonFiniteMixerSettingsWithMismatchedNodeNameAreIgnored` - `'std::isfinite(channel(1)->m_volumeModel.value())' returned FALSE` when only the QA guard hunk was reversed.
- `MixerStripTest::nonFiniteMixerAutomationBendIsIgnored` - `'std::isfinite(loadedClip->valueAt(TimePos(50)))' returned FALSE`.
- `ProjectStateTest::nonFiniteSnapshotFloatsAreReplaced` - compared floats are not the same.
- `ProjectStateTest::nonFiniteDownmixSettingsAreIgnored` - compared floats are not the same.

## Gates

The exact requested gate command was run during this review after the manifest re-derivation commit:
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-257-port-port-port-port` -> EXIT=0.

**At base `dbb15dded`, both failures were pre-existing on the train, not caused by this bug fix.** The
base `tests/all-sources.txt` carried 1148 vendored `plugins/SfzPlayer/sfizz/external/` entries that its
own header recipe now excludes (`build/qwen-logs/gate-3-base-manifest.log`):

```
=== base commit dbb15dded: tests/all-sources.txt vs its own header recipe ===
manifest entries the recipe does not derive: 1148
entries the recipe derives the manifest is missing: 0
```

The identical failure reproduced on the untouched main checkout at the same base commit with a clean tree
(`build/qwen-logs/gate-5-base-checkout.log`):

```
fork-sources-gate EXIT=1
all-sources-reproduce EXIT=1
FAIL: all-sources.txt does not match the recipe in its own header.
```

### The manifest failure, and what I did about it

The two failures were pre-existing on the train, not caused by this fix: `tests/all-sources.txt` carried
1148 vendored sfizz paths that its own header recipe excludes on purpose, so both gates that consume the
manifest were red at the base commit with a clean tree. The gate's header prescribes exactly one fix -
re-derive the entry list with the recipe, never hand-edit it - and I verified the delta was only that
class before applying it:

```
=== all base-only entries are under the header's excluded vendor tree ===
0 entries outside plugins/SfzPlayer/sfizz/
=== none of them are in fork-sources.txt ===
0
distinct prefixes dropped: plugins/SfzPlayer/sfizz/external, plugins/SfzPlayer/sfizz/src
re-derivation delta: 0 additions, 1148 deletions
```

`bash tests/all-sources-reproduce.sh` then reported `REPRODUCES` (EXIT=0) and
`bash tests/fork-sources-gate.sh` returned EXIT=0 (`1968 fork-NEW, 1214 inherited, 48 tooling, 0 stale`).
This is a train-state repair committed separately from the bug fix so the two are not confused; it changes
no product code, no test and no gate limit, and it removes only paths the manifest's own recipe says do not
belong in the whole-tree scope.

Final gate table after the repair (`build/qwen-logs/gate-6.log`):

```
feature-check zene-qwen-new-p10-257-port-port-port-port (base dbb15dded, 11 changed files; ...)
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

`no-upstream-regression` passes with this branch's two new ledger rows (one for `src/core/Mixer.cpp`, one
for `tests/src/core/VcaGroupTest.cpp`, which is inherited rather than fork-NEW).

## Registration

No new files, so no new-files registration was needed. The changed product files are already registered:
`src/core/Mixer.cpp` in `tests/upstream-modifications.txt`; `src/core/MixerSnapshots.cpp`,
`src/core/MixerStrip.cpp` and `src/core/MixerSurround.cpp` in `tests/fork-sources.txt`. The three changed
test files are already in `tests/CMakeLists.txt` and `tests/all-sources.txt`. No gate baseline or limit was
touched. The two new ledger rows are appended at the end of `tests/upstream-modifications.txt`, which still
ends with a newline.

## Files committed

- `src/core/Mixer.cpp`
- `src/core/MixerSnapshots.cpp`
- `src/core/MixerStrip.cpp`
- `src/core/MixerSurround.cpp`
- `tests/src/core/MixerStripTest.cpp`
- `tests/src/core/ProjectStateTest.cpp`
- `tests/src/core/VcaGroupTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (manifest re-derivation, separate commit; see the gate section)
- `docs/qwen-fixes-2026-10/cluster-new-p10-257-port-port-port-port.md`

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverse-apply
  EXIT=0, rebuild EXIT=0, `ctest` EXIT=8 with all six new slots failing by name; restore-apply EXIT=0,
  rebuild EXIT=0, `ctest` EXIT=0 (4/4, and the 5-test family run 5/5). Exact commands and codes above.
- **The test is deterministic.** Each test builds a `QDomDocument` in memory with literal attributes
  (`nan`, `inf`, `-inf`, `1e999`) and compares the stored model/state value against a finite default. No
  clocks, sleeps, threads, files or races; no audio device is started (the suites already stop it).
- **The fix is at the cause and complete.** Each reader family has one guarded path: the surround
  `attribute()`, the strip `load()`, the snapshot `channelFrom()`, and the mixer's
  `loadFiniteMixerSetting()`. Every caller and sibling of those readers was checked: channel volume and
  pan, regular send amount and pan, sidechain send amount, VCA group fader, and the child-element and
  automation-clip forms of `AutomatableModel::loadSettings` (including the automation `bend` attribute
  that the QA follow-up added). Hardware IO's cuemix floats
  (`src/core/HardwareIo.cpp:302`) and `MixerOptions`' crossfader (`src/core/MixerOptions.cpp:83`) already
  carry their own finite guards and were left unchanged.
- **No collateral damage.** Missing attributes, finite values and int/bool attributes read exactly as
  before; the fallbacks are the values the readers already used for a missing attribute, so every old
  project loads identically. No new limit was added and no caller can legitimately need a NaN. Reversing
  the submitted product change failed its six new regression slots; reversing the QA guard alone failed
  the mismatched-nodename regression.
- **Realtime, undo, commands.** Load-time only: these are XML readers on the project-open path, not
  `process`/`render`/per-period code. No allocation, lock, wait or log was added to an audio path, no undo
  step changed shape, and no control command was added (so the commands snapshot canary does not move).
- **Gates.** The exact `feature_check.py` command exits 0 with all eight rows PASS (`<LOG_FOLDER>/qa-review-12-feature-check-final.log`).
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so the last ASK does not apply; the evidence
  that the bug was real on this line is the six failing assertions in the reversed run above, plus the
  pre-fix source lines quoted at the top of this report.
