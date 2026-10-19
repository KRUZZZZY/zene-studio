# Cluster new-p8-269-port: explicit output destinations beyond the configured output count

Verdict: **FIXED**

`audioio::parseDestination` now checks the parsed destination's end channel against the side it is
read on (`outputs.channels`), so a number that names no channel of that side is refused with an
error. The port had to adapt the shape of the accepted fix (`2cadc3aa4`) to this line: the original
bound the *command* path by the interface's requested output count, which is what the merge train
rejected. On this line a route the running device may not carry is legal and is answered with a
status, so the command keeps the full range and the parser carries the bound.

## The finding and what this line does with it

The finding (`src/core/AudioIoSetup.cpp:171` on the old line) is true of the code: `parseChannels`
bounded a number by `MaxChannels` only, and `parseDestination` accepted every number it parsed, so
a destination beyond the side's channel count was stored.

The user-visible half of the finding - "routing a track to "3" or "127" with two outputs configured
succeeds" - is this line's **documented contract**, not a gap:

- `include/ControlHardwareIoSupport.h:74-78` - the reply's status says whether the device carries
  the route: "ok", "beyond_device" (the device opened fewer channels) or "stereo_backend" (this
  backend opens a stereo pair only; the route plays nowhere).
- `src/core/ControlCommandsHardwareIo.cpp:262-268` - the same contract in the command's own
  description ("The reply's status says whether the device carries it").
- `src/core/ControlHardwareIoSupport.cpp:40-45` - `outputStatus` implements it.
- `tests/src/core/HardwareIoTest.cpp:484` - a route to 3/4 on a stereo-only device is accepted and
  answered "stereo_backend" ("routed nowhere, and said so").
- `tests/control-hardware-io.py:61-63` - a pair the device did not open is accepted and answered
  "beyond_device".
- `tests/src/core/HardwareIoTest.cpp:171` - **the finding's own scenario is required to succeed on
  this line**: routing a channel to output "7" with the default two-output configuration is a
  passing assertion. Refusing it is what the original fix did, and it is what broke the train.

## Why the original fix's shape could not be re-applied

The original fix's product diff (`git show 2cadc3aa4 -- src/core/...`) was applied to this tree
unchanged and measured (`logs/build-2.log`, `logs/test-3.log`): it fails three families here.

| Test | Result with the original product diff |
|---|---|
| `HardwareIoTest` | **Failed** - routes to "3/4" and "5/6", cue outputs, insert sends "3/4" and "9/10" are refused |
| `SurroundTest` | **Failed** - "7/8 refused" on the eight-output device |
| `ControlHardwareIo` | **Failed** - "a pair the device did not open says so" and "one undo puts the previous route back" |

The reason is the count the original fix bounded by: the interface's *requested* output count
(2 by default), which is not the number of outputs the engine can carry. A device that granted
eight outputs (SurroundTest's device) or a route the device may not carry but the engine's output
bus can (the command's "beyond_device" answer) are both legitimate here, so the port keeps the
count where it is authoritative - the side the parser is handed - and leaves the callers that
legitimately exceed it on the full range.

## The change

| File | What it does |
|---|---|
| `src/core/AudioIoSetup.cpp:305-316` | `parseDestination`: a parsed number must fit the side it is read on (`out->start + out->width > outputs.channels` -> refused, "outputs are numbered 1..N"). The bus-name branch is untouched. |
| `include/AudioIoSetup.h:109-112` | the declaration's contract, changed with the definition. |
| `src/core/ControlHardwareIoSupport.cpp:116-130` | `readDestination` (the command path: `mixer.hardware_output`, `mixer.cue_master`, `mixer.hardware_insert`): the side keeps the setup's buses but takes the full range - a route the device may not carry is answered with a status here, so the parser must not refuse it. |
| `src/core/HardwareIo.cpp:61-69` | the project load: a project's routing is channel NUMBERS, read over the full range, so a route saved on a machine with eight outputs still loads on one with two. |
| `src/core/HardwareInsert.cpp:49-61` | the same for a project's insert sends and returns. |
| `tests/src/core/HardwareIoTest.cpp:453-463` | the regression (the original fix's assertions). |
| `tests/upstream-modifications.txt` | the inherited file `src/core/HardwareIo.cpp` is changed: its divergence is declared. |

Every caller of `parseDestination` was checked (there are three in the product, plus the test):
the command path, the project load (`HardwareIo.cpp`) and the project load of the insert's pairs
(`HardwareInsert.cpp`). No other path reads a routing target.

## The regression

`HardwareIoTest::destinationsParse` - the original fix's assertions, in the same test: a two-output
side refuses "3", "2/3" and "127", each with an error. It is deterministic: a hand-built
`audioio::Side` and pure string parsing, no clock, no thread, no device.

## Commands, exit codes and results

Build commands ran from the worktree root, CTest commands from `build/tests`. Every log is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-269-port.r1/logs`.

| Log | Command | Exit | Result |
|---|---|---:|---|
| `build-1.log` | `cmake --build build --target zene HardwareIoTest ImportTrackIoTest SurroundTest` | 0 | baseline build |
| `test-1.log` | `ctest -R '^(HardwareIoTest\|ImportTrackIoTest\|SurroundTest)$' --output-on-failure` | 0 | 3/3 passed (baseline) |
| `test-2.log` | `ctest -R '^ControlHardwareIo$' --output-on-failure` | 0 | 1/1 passed (baseline) |
| `build-2.log` | `cmake --build build --target zene HardwareIoTest SurroundTest` (the ORIGINAL fix's product diff applied) | 0 | the experiment build |
| `test-3.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ControlHardwareIo)$' --output-on-failure` (original diff) | 8 | 3 families FAILED - the train's reason, measured here |
| `build-3.log` | `cmake --build build --target HardwareIoTest` (regression test added, no product fix) | 0 | RED build |
| `test-4.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` | 8 | **RED**: `HardwareIoTest::destinationsParse() '!audioio::parseDestination(QStringLiteral("3"), twoOutputs, &d, &error)' returned FALSE` |
| `build-4.log` | `cmake --build build --target zene HardwareIoTest ImportTrackIoTest SurroundTest` (with the fix) | 0 | GREEN build |
| `test-5.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ImportTrackIoTest\|ControlHardwareIo)$' --output-on-failure` | 0 | 4/4 passed |
| `build-5.log` | `cmake --build build --target MixerRoutingBackwardCompatTest MixerAbRegressionTest MixerConcurrencyTest` | 0 | the neighbouring mixer families |
| `test-6.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ImportTrackIoTest\|ControlHardwareIo\|ControlMixingRouting\|MixerRoutingBackwardCompatTest\|MixerAbRegressionTest\|MixerConcurrencyTest\|ControlDevices)$' --output-on-failure` | 8 | 8/9 passed; `ControlDevices` failed (pre-existing, see below) |
| `build-7.log` | `cmake --build build --target zene HardwareIoTest` (product diff reverted) | 0 | the reverse-patch build |
| `test-8.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` (product diff reverted) | 8 | **RED proof**: the same `destinationsParse` assertion failed |
| `test-9.log` | `ctest -R '^ControlDevices$' --output-on-failure` (product diff reverted) | 8 | `ControlDevices` fails without this change too - pre-existing |
| `build-8.log` | `cmake --build build --target zene HardwareIoTest ImportTrackIoTest SurroundTest` (product diff restored) | 0 | the restore build |
| `test-10.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ImportTrackIoTest\|ControlHardwareIo\|ControlMixingRouting\|MixerRoutingBackwardCompatTest\|MixerAbRegressionTest\|MixerConcurrencyTest)$' --output-on-failure` | 0 | **GREEN proof**: 8/8 passed |
| `gate-feature-check-2.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | all gates PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length: "no new item in your files") |
| `gate-complexity.log` | `bash tests/complexity-gate.sh --check` | 1 | red on the train: dead baseline entries in files this change does not touch (`Vst3Host.cpp`, `ControlCommandsSessionFollow.cpp`, ...) - no entry for a file of this change |
| `gate-file-length.log` | `bash tests/file-length-gate.sh --check` | 1 | red on the train: 14 files over the limit that this change does not touch; `tests/src/core/HardwareIoTest.cpp` was already 556 code lines before this change (the limit is 500) and is not in the baseline |
| `gate-feature-check-3.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (with this report) | 0 | all gates PASS |
| `build-9.log` | `cmake --build build --target zene HardwareIoTest ImportTrackIoTest SurroundTest` (committed state) | 0 | the final build |
| `test-11.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ImportTrackIoTest\|ControlHardwareIo\|ControlMixingRouting\|MixerRoutingBackwardCompatTest\|MixerAbRegressionTest\|MixerConcurrencyTest)$' --output-on-failure` (committed state) | 0 | the final run: 8/8 passed |
| `submit-check.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-269-port` | 0 | RESULT: PASS - submit (every mechanical item PASS; the ASK items are answered below) |

The reverse-patch proof used `logs/fix.patch`:

```
git diff -- src include > logs/fix.patch        # the product change only (the test is not in it)
git apply -R logs/fix.patch                     # exit 0  -> build-7.log, test-8.log (exit 8: RED)
git apply    logs/fix.patch                     # exit 0  -> build-8.log, test-10.log (exit 0: GREEN)
```

### The pre-existing failure

`ControlDevices` (a control-surface driver over the socket) fails in this build with and without
this change: its transcript says "the LADSPA host plugin 'ladspaeffect' is not in this build"
(`logs/test-7.log` with the change, `logs/test-9.log` without it - the same four checks fail). It
touches the Device panel and LADSPA effects, not the audio I/O routing this change is about.

## What this port changes and what it does not

- **Changed**: `audioio::parseDestination` refuses a numeric destination beyond the channel count of
  the side it is read on. The bound is the side's, so a caller that knows the interface's count
  gets it enforced.
- **Not changed**: the commands' behaviour. A route beyond the running device is still accepted and
  answered with a status ("beyond_device", "stereo_backend"), and a route beyond the interface's
  requested count is still accepted while the device can carry it - that is this line's contract
  (`include/ControlHardwareIoSupport.h:74-78`) and its own tests demand it
  (`tests/src/core/HardwareIoTest.cpp:171`, `tests/control-hardware-io.py:61-63`).
- **Not changed**: saved data. A project's routing is channel numbers and is read over the full
  range, so an old project loads exactly as before on any interface.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED:
  `ctest -R '^HardwareIoTest$' --output-on-failure` -> exit 8, `HardwareIoTest::destinationsParse()`
  fails at `!audioio::parseDestination(QStringLiteral("3"), twoOutputs, &d, &error)`
  (`logs/test-4.log`, and again after the reverse patch in `logs/test-8.log`). GREEN: the same
  command -> exit 0 (`logs/test-10.log`, 8/8 families passed).
- **The test is deterministic.** It parses strings into a hand-built `audioio::Side`
  (`channels = 2`); no clock, no sleep, no thread, no device, no randomness.
- **The fix is at the cause and complete.** The cause is `parseDestination` accepting a number
  beyond the side's channels (`src/core/AudioIoSetup.cpp:305-316`); all three product callers were
  checked and kept consistent: the command path (`ControlHardwareIoSupport.cpp:116-130`), the
  project load (`HardwareIo.cpp:61-69`) and the insert's pairs (`HardwareInsert.cpp:49-61`). The
  sibling path into the same function - the bus-name branch - is unchanged and still resolves a bus
  of the side. The declaration in `include/AudioIoSetup.h:109-112` was changed with the definition.
- **No collateral damage.** Old projects and saved data load exactly as before (the project load
  reads over the full range); the check does not hit the callers that legitimately exceed a side's
  count (the command path answers them with a status, the project load carries another machine's
  channel numbers); behaviour outside the bug is unchanged - the command's replies, the undo steps
  and the saved format are untouched.
- **Realtime, undo and commands.** Nothing on the audio thread was touched: the change is in the
  interface/control path (parsing a routing target), not in the mixer, the render path or the
  device feed. No allocation, no lock, no logging was added. No control command was added, removed
  or changed, so the command surface and its canary are untouched (the diff adds no command id).
- **Qt 5.15 / MSVC / macOS portability.** No new API is used: the change is `QStringLiteral`,
  `QString::arg` and integer arithmetic, all already used in these files. No `windows.h` name, no
  `std::filesystem`, no Qt-6-only call.
- **Registration and gates.** `tests/upstream-modifications.txt` declares the inherited file
  `src/core/HardwareIo.cpp` this change touches (the file ends with a newline); no new file was
  added, so `tests/fork-sources.txt` needed no entry. `feature_check.py` exits 0 with every gate
  PASS. The two ratchet scripts are red on the train for items in files this change does not touch
  (their logs are listed above); no limit was re-anchored.
- **The ALREADY-FIXED / FALSE-POSITIVE item does not apply** - the verdict is FIXED. The contract
  it would have asked for is cited above anyway: `include/ControlHardwareIoSupport.h:74-78` and
  `src/core/ControlCommandsHardwareIo.cpp:262-268` (the command's own description) are where this
  line states that a route the device may not carry is accepted and answered with a status.

## Files in the commit

- `src/core/AudioIoSetup.cpp`
- `include/AudioIoSetup.h`
- `src/core/ControlHardwareIoSupport.cpp`
- `src/core/HardwareIo.cpp`
- `src/core/HardwareInsert.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-269-port.md`
