FIXED

# Cluster new-p23-180: MultiTrackRecorder default route mapping ignores input capacity

## Verdict

**FIXED** - the finding's *direction* is real, but its stated symptom is not. The
finding says route 1 on a 2-route / 1-channel recorder gets channel 1 and cannot be
armed. That does not happen: `TrackRecorder::setInputChannel` already maps a
non-selectable request to 0 (`src/core/audio/TrackRecorder.cpp:93-96`), so that case
was on channel 0 all along. The real defect is that a route past the input width
gets channel **0** instead of the **last selectable channel** the constructor comment
promises (and that the control-command sibling implements). Example: 3 routes over
2 channels - route 2 read channel 0, the contract says channel 1. The regression
test below reproduces exactly that.

## Evidence (file:line, pre-fix tree at `37eaf07557`)

- `src/core/audio/MultiTrackRecorder.cpp:48-50` - the comment states the contract:
  route k starts on channel k, or on the last selectable channel when the range is
  narrower.
- `src/core/audio/MultiTrackRecorder.cpp:51-54` (pre-fix) - the loop called
  `setInputChannel(i)` unconditionally.
- `src/core/audio/TrackRecorder.cpp:93-96` - `setInputChannel` stores
  `inputChannelSelectable(channel) ? channel : 0`. This is why an out-of-range route
  fell to 0 rather than to the last channel.
- `src/core/audio/TrackRecorder.cpp:133-136` - `inputChannelSelectable` is
  `0 <= channel < inputChannelCapacity()`.
- `src/core/ControlCommandsRecordingRoutes.cpp:110-115` (sibling) - the default when
  no `input_channel` is given is `std::min(route, channels - 1)`, i.e. the contract.
- `src/core/ControlCommandsArrangementState.cpp:156` - the default is user-visible:
  the arrangement state reports `target.inputChannel()` as `input_channel`.

## Fix

`src/core/audio/MultiTrackRecorder.cpp:51-56` (post-fix):

```cpp
const int lastChannel = std::max(0, this->inputChannelCapacity() - 1);
for (int i = 0; i < routes; ++i)
{
    m_tracks[static_cast<std::size_t>(i)]->setInputChannel(std::min(i, lastChannel));
}
```

`this->` is required: the constructor parameter `inputChannelCapacity` shadows the
member function (first attempt failed to compile, see log `build-3.log`). The value
read is the capacity the tracks actually hold (`TrackRecorder` clamps it to >= 1, so
`lastChannel >= 0` always; the `std::max` is belt-and-braces). No new branch, no
audio-thread code (constructor), no command added, no header change.

## Regression test

`tests/src/core/MultiTrackRecorderTest.cpp:147-174`,
slot `DefaultRouteMappingStartsOnTheLastSelectableChannel`. Matrix:

- normal path: `MultiTrackRecorder narrow(3, 2)` -> routes read 0, 1, 1
  (the failing assertion is the route-2 one, line 156).
- narrow edge: `MultiTrackRecorder mono(2, 1)` -> both routes read 0 (the finding's
  example; already passing before the fix, kept as a guard).
- arming: every default channel is selectable and arms via `armTrack` (exhaustion /
  reconfiguration are not touched by this change; `shrinkingInputCapacity...` and
  `RecordingInputPathTest` cover those and still pass).
- audio-thread: not applicable (constructor). Deterministic: no timing, no races.
- platform: plain Qt test, no platform-specific code.

## Commands and exit codes

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-180.r1/logs/`.

| Step | Command (run in `build/`, `build/tests`) | EXIT | Log |
| --- | --- | --- | --- |
| build (env failure) | `cmake --build . --target MultiTrackRecorderTest -j4` | 2 | `build-1.log` - ccache could not create temp file under the shared `dsh` dir (`Permission denied`); not a code error |
| build | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p23 cmake --build . --target MultiTrackRecorderTest -j4` | 0 | `build-2.log` (pre-fix, RED build) |
| RED | `ctest -R '^MultiTrackRecorderTest$' --output-on-failure` | **8** | `test-red-2.log` - 10 passed, 1 failed; `FAIL! DefaultRouteMappingStartsOnTheLastSelectableChannel`, `Actual (narrow.track(2).inputChannel()): 0`, `Expected (1): 1`, `Loc: [MultiTrackRecorderTest.cpp(156)]` |
| build (fix, first try) | `cmake --build . --target MultiTrackRecorderTest -j4` | 2 | `build-3.log` - compile error: `inputChannelCapacity` cannot be used as a function (parameter shadowing) |
| build (fix) | same, after `this->` | 0 | `build-4.log` |
| GREEN | `ctest -R '^MultiTrackRecorderTest$' --output-on-failure` | **0** | `test-green-2.log` (100% passed) |
| step 4b: revert | `git diff -- src/core/audio/MultiTrackRecorder.cpp > fix.patch` ; `git apply -R fix.patch` | 0 ; 0 | `fix.patch` |
| step 4b: build no-fix | `cmake --build . --target MultiTrackRecorderTest -j4` | 0 | `build-5-nofix.log` |
| step 4b: RED | `ctest -R '^MultiTrackRecorderTest$' --output-on-failure` | **8** | `test-4b-nofix.log` - same assertion at line 156 |
| step 4b: restore | `git apply fix.patch` | 0 | - |
| step 4b: build fix | `cmake --build . --target MultiTrackRecorderTest -j4` | 0 | `build-6-fix.log` |
| step 4b: GREEN | `ctest -R '^MultiTrackRecorderTest$' --output-on-failure` | **0** | `test-4b-fix.log`; `-V` run in `test-4b-fix-verbose.log` (Test #161 Passed) |
| family build | `cmake --build . --target ControlRecordingRoutesTest InputChannelMeterTest RecordingInputPathTest -j4` | 0 | `build-7-family.log` |
| family | `ctest -R '^(MultiTrackRecorderTest\|ControlRecordingRoutesTest\|InputChannelMeterTest\|RecordingInputPathTest)$' -V` | **0** | `test-family-1.log` |

Affected-family test count: **4 test binaries, 28 test functions executed, all passed**
(MultiTrackRecorderTest 11, RecordingInputPathTest 8, ControlRecordingRoutesTest 3,
InputChannelMeterTest 6; `test-family-1.log`). Zero-tests check: nonzero in every run.

Note on an earlier GREEN run: `test-green-1.log` (EXIT 8) ran the stale binary because
the fixed build had failed (`build-3.log`). It is superseded by `test-green-2.log`.

### Gates (step 5)

`python3 .../feature_check.py <worktree>` - **EXIT 1, did not run its checks.** It
creates its log folder at `projects/zene-studio-program/zene-qwen-new-p23-180-feature-check`,
a sibling of the worktree, and gets `PermissionError` (outside my write-jail). Log:
`gate-1.log`. I did not edit the shared script.

Manual runs of the same commands the script lists (from its `SIMPLE` and `RATCHET`
tables), run in the worktree:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-manual-1.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-manual-2.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-manual-3.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-manual-4.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-manual-5.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-manual-6.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-manual-7-complexity.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-manual-8-file-length.log` |

The two ratchets are red on the train by design. Their `REGRESSION:` lines name only
files this change does **not** touch (`WasmSandbox.cpp`, `XyPadPairTest.cpp`,
`DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`,
`ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`,
`Vst3Host.cpp`, `TakeLane.cpp`, `ArrangeToolsTest.cpp`). Neither
`MultiTrackRecorder.cpp` nor `MultiTrackRecorderTest.cpp` appears. **Not verified:**
the script's baseline comparison (against `FIX-BASE`), since its cache lives outside
my jail. The fix adds no branch and no new function, so I expect no new item in my
files, but that is a reading, not a gate result.

Registration (step 5): no new files. `src/core/audio/MultiTrackRecorder.cpp` and
`tests/src/core/MultiTrackRecorderTest.cpp` are already listed in
`tests/fork-sources.txt:1959` and `:2507`. `tests/upstream-modifications.txt` needs no
change (both are product-authored, not upstream LMMS files).

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored?** Yes.
  Reverted via `git apply -R fix.patch`: `ctest` EXIT 8, assertion
  `MultiTrackRecorderTest.cpp(156)` (`test-4b-nofix.log`). Restored via
  `git apply fix.patch`: EXIT 0 (`test-4b-fix.log`).
- **Acceptance criteria proven by a test I ran?** The finding's criterion is the
  default mapping; it is proven by `DefaultRouteMappingStartsOnTheLastSelectableChannel`
  (EXIT 0 after fix, EXIT 8 before). Its narrow-edge assertions (`mono(2,1)`) pass
  both before and after, as a guard against regressing the case the finding named.
- **Each edit is one undo step; reversibility class?** Not applicable: the change is
  constructor-time channel defaults, no command, no undo-recorded edit. No
  reversibility class to report.
- **Nothing added runs on the audio thread?** Yes, nothing. The edited code is in the
  constructor (`MultiTrackRecorder.cpp:36-56`), which runs off the audio thread
  (comment at line 38-39). `std::min`/`std::max` and one `inputChannelCapacity()` read
  are not on any audio path. The only new call is `setInputChannel` on the same
  objects, as before.
- **Old projects still load and render as before?** Yes, by construction: the default
  mapping only differs for routes at or past the input width, and it is not saved in
  projects (it is the constructor's initial value; armed routes always carry an
  explicit channel through `armTrack`). Existing tests `RecordingInputPathTest`,
  `ControlRecordingRoutesTest`, `InputChannelMeterTest`, `MultiTrackRecorderTest` pass.
- **Qt 5.15 / MSVC / macOS portability?** No Qt API added; `std::min`/`std::max`
  with `int` operands, `<algorithm>` already included (`MultiTrackRecorder.cpp:28`).
  Test uses `QStringLiteral`, `QTemporaryDir`, `QVERIFY2`, all already used in the file.
- **Gate result:** `feature_check.py` could not run (write-jail, see above); manual
  equivalents listed. Not a clean gate pass in the script's own form.
