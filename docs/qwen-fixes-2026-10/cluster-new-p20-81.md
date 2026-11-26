ALREADY-FIXED

# Cluster new-p20-81: lazy MixerOptions initialization on the audio thread

new-p20-81: ALREADY-FIXED

## Finding
`MixerOptions::instance()` (src/core/MixerOptions.cpp:34-42) builds the object and calls
`ProjectSection::registerSection` on first use. The mixer reads it once per period through
`mixerstrip::applyPanMode` -> `applyPanLaw` (src/core/MixerStrip.cpp:205) and `applyCrossfade`
(src/core/MixerStrip.cpp:173), called from src/core/Mixer.cpp:840-841. If nothing built it first, the
mixer's first period would allocate it and append to `ProjectSection::sections()` on the audio thread.

## Why it is already fixed on this branch
- src/core/Song.cpp:183-186: `Song::Song()` calls `projectsections::registerAll()`. The comment
  there says it builds "the project sections and the singletons the audio thread reads ... on the thread
  that builds the song, in every mode - not on the audio thread, and not only once the control registry
  happens to exist".
- src/core/ProjectSectionRegistry.cpp:55: `registerAll()` calls `MixerOptions::instance();`.
- src/core/Engine.cpp:73 constructs `new Song` inside `Engine::init`. That is before
  `s_audioEngine->startProcessing()` at src/core/Engine.cpp:91, so no audio thread exists yet when
  MixerOptions is built and registered. It holds in every mode, including headless `-r` rendering without
  a control socket (src/core/main.cpp:1459-1479). The finding assumed the eager call in
  `registerMixerToolCommands` (src/core/ControlCommandsMixerTools.cpp:494) was the only one.
- Introduced by commit d4aa916d4c ("0.5.0 W3: review W3-batchA fixes, project sections built at Song
  construction ..."): `git log -S'MixerOptions::instance();' -- src/core/ProjectSectionRegistry.cpp`
  and `git log -S'projectsections::registerAll();' -- src/core/Song.cpp` both return it.
- After construction, `instance()` on the audio thread is only the function-local static's
  initialised-guard check plus a pointer return: no allocation, lock or registration.

Siblings: the other lazily built sections that the audio thread reads are AutomationLatency
(Song.cpp:576, TrackContainer.cpp:472/517/566), AutomationSwitches (Song.cpp:816/877/1628) and MidiPlayback
(Song.cpp:651, InstrumentTrack.cpp:932/1034, InstrumentTrackNotes.cpp:178/229/272). `registerAll()` builds
each of them as well (ProjectSectionRegistry.cpp:53, 54, 66). No product change was needed.

## Regression guard added (test only, no product change)
tests/src/core/ProjectSectionTest.cpp, `mixerOptionsIsRegisteredBeforeTheAudioEngineStarts()`. Before
`Engine::init(true)`, `initTestCase()` connects directly to `Engine::initProgress`. On the message
"Launching audio engine threads", which Engine.cpp:90 emits right before `startProcessing()`, it records
whether a section named `mixer-options` is already in `ProjectSection::sections()`. That point comes
before any audio thread exists, so the test does not depend on timing or a race. It never calls
`MixerOptions::instance()` itself.

To show the guard catches the bug, I deleted `MixerOptions::instance();` from
ProjectSectionRegistry.cpp:55, ran the test, and then restored the line. `git diff` of that file is empty
afterwards.

Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-81.r1/logs):
| step | command | EXIT |
|---|---|---|
| build, current code | `cmake --build build --target ProjectSectionTest -j4 > logs/01-build-red.log 2>&1` | 0 |
| test, current code | `cd build/tests && ctest -R ProjectSectionTest --output-on-failure > logs/02-test-red.log 2>&1` | 0 ("100% tests passed out of 1") |
| build, line 55 removed | `cmake --build build --target ProjectSectionTest -j4 > logs/03-build-guard-red.log 2>&1` | 0 |
| test, line 55 removed | `cd build/tests && ctest -R '^ProjectSectionTest$' --output-on-failure > logs/04-test-guard-red.log 2>&1` | 8 |
| build, line restored | `cmake --build build --target ProjectSectionTest -j4 > logs/05-build-guard-green.log 2>&1` | 0 |
| test, line restored | `cd build/tests && ctest -R '^ProjectSectionTest$' --output-on-failure > logs/06-test-guard-green.log 2>&1` | 0 ("100% tests passed out of 1") |
| zene, restored source | `cmake --build build --target zene -j4 > logs/07-build-zene.log 2>&1` | 0 |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/08-feature-check.log 2>&1` | 1 (refused, see below) |

Failure with line 55 removed (04-test-guard-red.log, lines 8-9):
```
FAIL!  : ProjectSectionTest::mixerOptionsIsRegisteredBeforeTheAudioEngineStarts() 'm_mixerOptionsRegisteredAtLaunch' returned FALSE. (MixerOptions must be created on the main thread before startProcessing, not lazily by the mixer's first period)
   Loc: [../../tests/src/core/ProjectSectionTest.cpp(114)]
```

## Gates
feature_check.py did not run. The lane guard refused its baseline step (08-feature-check.log):
```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
subprocess.CalledProcessError: ... returned non-zero exit status 97.
```
The brief says a refused command is not to be worked around, so no gate table exists for this run. The
change touches one existing, already-registered test file (tests/fork-sources.txt:2932,
tests/CMakeLists.txt:1062) and adds no product file and no new function in product code.

Delegation: Sonnet wrote the guard test and ran the first build and test, which passed on the current
code and exposed the existing `registerAll()` call. Haiku searched the audio paths of the sibling
singletons. A second Haiku ran the guard RED/GREEN cycle with line 55 removed and then restored, and
rebuilt zene. I made the verdict, reviewed the diff and wrote this report.

## Self-check
- **The test fails without the product line and passes with it.** No product change was made, because the
  fix already exists. The guard was checked against the existing fix instead. With
  `MixerOptions::instance();` removed from ProjectSectionRegistry.cpp:55, ctest exited 8
  (04-test-guard-red.log, the FAIL! line quoted above). With the line restored, ctest exited 0
  (06-test-guard-green.log). On HEAD as committed, ctest exited 0 (02-test-red.log).
- **Deterministic.** There are no sleeps or timing. The seam is the `Engine::initProgress` signal with a
  `Qt::DirectConnection` on the same thread, at the message "Launching audio engine threads"
  (src/core/Engine.cpp:90). That signal fires before `startProcessing()` (Engine.cpp:91), so no audio
  thread exists that could register MixerOptions first. The test never calls `MixerOptions::instance()`.
- **At the cause and complete.** The cause, construction on first use by the audio path, is already
  removed: `Song::Song()` builds every section through `registerAll()` (Song.cpp:186). I checked the
  siblings the audio thread reads (AutomationLatency, AutomationSwitches, MidiPlayback; call sites listed
  above). All of them are in `registerAll()` (ProjectSectionRegistry.cpp:53, 54, 66). The other
  `MixerOptions::instance()` callers are UI-thread commands (ControlCommandsMixerTools.cpp:87/95/494,
  ControlCommandsMixerMeters.cpp:135-154, ControlCommandsProjectInfo.cpp:75/86).
- **No collateral damage.** No product code changed: the diff is the test file and this report. Saved
  projects and the load path are untouched. No limit or check was added.
- **ALREADY-FIXED evidence.** src/core/ProjectSectionRegistry.cpp:55 (`MixerOptions::instance();`),
  src/core/Song.cpp:186 (`projectsections::registerAll();` in `Song::Song()`), src/core/Engine.cpp:73
  (`new Song`) before Engine.cpp:91 (`startProcessing()`). Fixed in commit d4aa916d4c.
- **Gates.** submit_check reports `FAIL gates (feature_check.py)` because the lane guard refuses that
  script's `git checkout` in its baseline tree (exit status 97, 08-feature-check.log). The brief forbids
  working around a refused command, so this item stays open. That step is BLOCKED by the environment, not
  by the change.

## Other findings
- Gate tooling: feature_check.py runs `git checkout` in its baseline tree, and the lane guard refuses
  that, so the gates cannot run in this lane.
