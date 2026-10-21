IMPLEMENTED

# Q050-166 - Mono and discrete bus formats

Branch `feat/050-Q050-166-oc`, cut from `050/m0-train-9` (`2da2ed356`). Member `R-mixing-47`, area L09.

## What changed

- `include/MixerSurround.h`, `src/core/MixerSurround.cpp`: `Format::Mono` (one `M` role) and
  `Format::Discrete2` .. `Format::Discrete8` (role-less, index-addressed). The count is part of the
  canonical name (`mono`, `discrete-2` ... `discrete-8`), so saving and reloading cannot lose it. The
  loader also accepts `discrete5`, `discrete 5` and `discrete_5` for hand-written projects. The range is
  2..8 - the existing eight-channel side-buffer capacity; Q050-155 owns the wider graph.
- Discrete channels are role-less: `roleOf()` is `Role::None`, `channelOf()` is -1, `speakerNames()` is
  empty and `channelLabels()` reports `1`..`N`. The downmix matrix (`discreteMatrix()`) maps channel `i`
  to channel `i`, dropping channels the receiver does not have - no speaker lookup, reordering or fold.
- `src/core/ControlCommandsMixer.cpp`: `mixer.get_state` reports `format`, `channels`, `speakers`,
  `channel_roles` and `discrete` for a non-stereo channel. A stereo channel keeps exactly the old shape,
  so existing projects and the `ControlFeedbackCommands` mixer baseline are untouched. No new command id.
- `src/core/ControlCommandsMixerSurround.cpp`: `mixer.surround_get` carries the same count/roles fields;
  `mixer.channel_format`'s schema advertises the new names and its description states the index routing.
- `src/core/MixerSurroundMix.cpp`: mono sums its pair for the master/direct out; the panner is restricted
  to named speaker layouts; discrete layouts use the existing side-plane path by index.
- `src/gui/SurroundPanner.cpp`: a discrete layout is drawn as an index strip, never as invented speaker
  positions. The GUI entry point is the existing `Surround > Format` menu in `MixerHardwareMenu.cpp`,
  built from the command schema; each entry runs `mixer.channel_format` through `ControlRegistry`, the
  same command the agent surface uses.
- `src/core/SurroundExport.cpp`: mono/discrete formats cannot index the speaker table out of bounds.
- `src/core/audio/AudioFileWave.cpp`: WAV channel-map generation now takes the selected master format
  (`formatForChannels(channels, masterFormat)`), so discrete-3/-4/-6/-8 no longer inherit LCR/quad/5.1/
  7.1 masks. libsndfile writes a count default for 4/6/8, so after the samples flush the writer closes
  the file and clears WAVEX's `dwChannelMask` to 0; named exports keep their masks.
- `data/manual/index.html`: Mono and Discrete layouts, index routing and the absence of speaker positions.
- `docs/050-features/Q050-166.md`: the feature record.
- Tests: `tests/src/core/SurroundTest.cpp` (two new slots, plus helpers) and
  `tests/src/gui/MixerWindowsTest.cpp` (the existing menu slot now triggers Mono and Discrete 5).
- Snapshot regenerated live: 833 commands / 76 groups, no new ids. `README.md` already says 833/76.

## Acceptance

| Criterion | Test and evidence | Exit |
|---|---|---|
| `mixer.channel_format` accepts mono and discrete channel-count layouts and `mixer.get_state` reports their speaker/channel roles | `SurroundTest::monoAndDiscreteFormatsReadBack` invokes the registered command for `mono` and every `discrete-2`..`discrete-8`; asserts `mixer.get_state` gives `format`, `channels`, empty `speakers` + `channel_roles` `1..N` for discrete and `["M"]` for mono, `mixer.surround_get` agrees, one `control.undo` returns stereo, an invalid count is `invalid_args`. `MixerWindowsTest::theSurroundMenuSetsTheFormatAndOpensThePanner` triggers the actual `Surround > Format > mono` and `discrete-5` menu actions and reads the same values back, then undoes. Direct `./build/tests/SurroundTest` -> `Totals: 15 passed, 0 failed`, exit 0; `./build/tests/MixerWindowsTest` -> `Totals: 11 passed, 0 failed`, exit 0. | 0 |
| A project round-trip preserves each layout and a render routes every discrete channel to its matching output | `SurroundTest::monoAndDiscreteLayoutsSurviveASaveAndLoad`: each of mono + discrete-2..8 is written by `Mixer::saveSettings`, the mixer is cleared, loaded back, and the format, count, name and roles are re-read (8/8 layouts). `SurroundTest::discreteChannelsReachTheirOwnOutputs`: for every N 2..8, distinct constants on the channel pair and planes 0..N-1 reach outputs 0..N-1 exactly and no other output (checked every frame), and the matrix is index-identity (`m[0][0]`, `m[4][4]` set, `m[4][2]` zero; discrete-8 -> mono does not sum channels 0 and 1). `SurroundExportTest::aDiscreteMasterWritesItsChannelCountWithoutSpeakerMask` (round-1 rework): every discrete master 3..8 - the counts that overlap LCR, quad, 5.1 and 7.1 included - writes a WAVE_FORMAT_EXTENSIBLE file with its own channel count and mask 0. Direct `SurroundTest` exit 0 (15 passed); direct `SurroundExportTest` exit 0 (10 passed). | 0 |

Adjacent regression run: `ctest -R '^(SurroundTest|MixerWindowsTest|SurroundExportTest|ReversibilityContractTest|ControlCommandsSnapshot|AgentSurfaceReportTest)$'`
-> `100% tests passed out of 6`, exit 0 (`ctest-final-7.log`). `SurroundExportTest` (named layout
exports plus the discrete-5 header), `ReversibilityContractTest`, `ControlCommandsSnapshot` and
`AgentSurfaceReportTest` all pass.

## Reversion evidence

- **Full non-test revert** (the seven product files restored from `2da2ed356`, tests kept):
  `cmake --build build --target SurroundTest` -> **EXIT=2**, compiling `SurroundTest.cpp` fails because
  `Format::Mono`, `MinDiscreteChannels`, `Format::Discrete6` etc. do not exist in the old header
  (`revert-full-build.log`). This shows the tests are not passing against the base product.
- **Behavioural revert that still compiles**: restore the new header/model/render files, but put
  `src/core/ControlCommandsMixer.cpp` back at `2da2ed356` (so `get_state` does not publish the layout
  fields). `cmake --build build --target SurroundTest` -> **EXIT=0**; running the two new slots ->
  **EXIT=2**, `monoAndDiscreteFormatsReadBack` and `monoAndDiscreteLayoutsSurviveASaveAndLoad` both fail
  with `actual ""`, `expected "mono"` (`revert-getstate-test.log`). Restored current product files:
  rebuild **EXIT=0**, six-test ctest **EXIT=0** (`ctest-restored.log`, `ctest-final-6.log`).
- **Round-1 export rework**: with only `src/core/audio/AudioFileWave.cpp` put back at the submitted
  `HEAD` (so the channel map is inferred from the count again), `cmake --build build --target zene
  SurroundExportTest` -> **EXIT=0** and `ctest -R '^SurroundExportTest$'` -> **EXIT=8**, the new loop's
  `h.mask` actual `7`, expected `0` at discrete-3 (`revert-export-build.log`, `revert-export-test.log`).
  Restored: rebuild **EXIT=0**, `SurroundExportTest` **EXIT=0** (`restore-export-build.log`,
  `restore-export-test.log`).
- The prescribed log directory `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-166.r1/logs`
  is read-only from this sandbox (`touch` -> `Permission denied`), so all run logs quoted here are under
  `/tmp/q050-166-logs/`; the worktree is clean apart from the intended changes.

## Gates

| Gate | Exit | Note |
|---|---|---|
| `bash tests/fork-sources-gate.sh` | 0 | 1853 fork-NEW, 1212 inherited, 48 tooling, 0 stale |
| `bash tests/all-sources-reproduce.sh` | 0 | REPRODUCES |
| `bash tests/no-upstream-regression-gate.sh` | 0 | 1526 changed paths declared |
| `tests/scripted/check-namespace` | 0 | 0 errors |
| `bash tests/no-tautology-gate.sh` | 0 | PASS |
| `python3 tests/doc-figures-gate.py` | 0 | 833 ids / 76 groups, README matches |
| `python3 .../snapshot_live.py $PWD` | 0 | 833 commands, 76 groups, `wasm.*` out |
| `bash tests/complexity-gate.sh --check` | 1 | red on the train; no line names a function/file changed here (the split `buildMatrix` helpers are under target) |
| `bash tests/file-length-gate.sh --check` | 1 | red on the train; `SurroundTest.cpp` is not listed (504 code lines at the first check, then trimmed under 500) |

`submit_check.py` and `feature_check.py` themselves cannot create their sibling log directory from this
sandbox (`/home/.../zene-feat-q050-166-feature-check` -> `Permission denied`). Copies of the same two
scripts with only the log/cache locations redirected to `/tmp` were run against this worktree:
`feature_check.py` -> `RESULT: PASS` (all eight gates; exit 0), `submit_check.py` -> `RESULT: PASS -
submit` (exit 0, `feature-check-modified.log`, `submit-check-modified.log`).

`ControlFeedbackCommands` is the one neighbouring transcript that did not pass in this container: it
compares a saved project against a fixed pre-change hash and its fixture could only load `amplifier`
after that module was built. Its default-mode `mixer.get_state` comparison passes, and its save hash
failure is independent of this diff (nothing here changes any save path for stereo projects). It is
reported rather than hidden.

## Self-check

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run:
exit code?), then PASSES restored.**
Full revert -> build EXIT=2 (missing new API). Compile-preserving revert of `mixer.get_state` readback
-> build EXIT=0, two targeted slots EXIT=2 with the quoted mismatch. Restored -> build EXIT=0, all six
tests EXIT=0. Details and log names under "Reversion evidence".

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code
per criterion).**
Criterion 1: `SurroundTest::monoAndDiscreteFormatsReadBack` and
`MixerWindowsTest::theSurroundMenuSetsTheFormatAndOpensThePanner`; direct runs exit 0 (`Totals: 15
passed` / `11 passed`). Criterion 2: `SurroundTest::monoAndDiscreteLayoutsSurviveASaveAndLoad`,
`SurroundTest::discreteChannelsReachTheirOwnOutputs` and
`SurroundExportTest::aDiscreteMasterWritesItsChannelCountWithoutSpeakerMask`; `SurroundTest` exit 0,
`SurroundExportTest` exit 0, six-test ctest exit 0.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
`mixer.channel_format` keeps its existing single `addUndoStep(...)` per change and its `true_inverse`
row; the new tests set a format then one `control.undo` returns stereo after every mono and every
discrete edit. `mixer.get_state` and `mixer.surround_get` stay `not_mutating`; no command id or class
changed. `ReversibilityContractTest` exit 0.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal (your diff touches audio-path code - name the functions and say why each is safe).**
`surround::buildMatrix`, `discreteMatrix`, `namedIntoMono` and the layout parsing run on the control
thread (format changes/project load) and only fill fixed `std::array` matrices. The render-path changes
are `surround::masterOutput` (sums existing `out` frames for Mono), `surround::directOut` (sums existing
`stereo` frames for Mono; the discrete branch is the pre-existing `placeSides` loop over the already
allocated side planes) and the `panTakesSend` predicate (reads the format enum). No branch calls
allocate/free, takes a lock, waits, performs I/O or emits a Qt signal. `SurroundTest::theMasterMixAllocatesNothing` still passes with the surround paths.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
`aStereoProjectIsUnchanged` still passes: a project that never leaves stereo saves no `<surround>`,
renders bit-identically, and `mixer.get_state` keeps its old shape for stereo channels (therefore the
`ControlFeedbackCommands` stereo `get_state` baseline still matches). A missing `<surround>` loads
stereo (`ChannelSurround::load` -> reset); the new round-trip test covers all eight new layouts.
Named-layout `SurroundExportTest` passes unchanged.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro
names, no std::filesystem outside _WIN32.**
The diff uses only `QString`, `QStringList`, `QJsonArray`, `std::array`, `std::min`, `std::sqrt` and
`std::clamp`, all available in the supported Qt/C++20 set; no new Qt version guard is needed. No
`std::filesystem`, no `windows.h` identifiers (`small`, `near`, `far`, `interface`, `min`, `max`), and
the only new UI code is ordinary `QPainter` drawing. `MixerSurround.h`'s `qstrlen` detour was replaced
by a fixed string index before the build.

**? Sibling commands / surface.** No new id was introduced; `mixer.channel_format`,
`mixer.surround_get` and `mixer.get_state` are the existing registered trio. The schema-backed menu and
the agent both reach the same `ControlRegistry` command. `AgentSurfaceReportTest` and
`ControlCommandsSnapshot` pass on the regenerated live snapshot (833/76).

IMPLEMENTED
