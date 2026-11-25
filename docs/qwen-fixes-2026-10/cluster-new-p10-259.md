FIXED

# new-p10-259: the hardware output bounds check added `start + count` and could wrap

## Verdict: REAL, fixed at `src/core/MixerSurroundMix.cpp:435-444`

The finding named `src/core/MixerSurroundMix.cpp:286-290`; the code has moved to `:435-444` and read:

```cpp
bool deviceCarries(int start, int count)
{
	const AudioDevice* device = Engine::audioEngine()->audioDev();
	return start >= 0 && device != nullptr && start + count <= static_cast<int>(device->outputChannels())
		&& start + count <= HardwareOutputBus::MaxChannels;
}
```

`deviceCarries` is the predicate behind every surround route decision:

- `masterPlaysWide()` -> `MixerSurroundMix.cpp:187` (the master's side channels onto the hardware),
- `directOut()` -> `MixerSurroundMix.cpp:427` (a surround channel's direct out),
- `hardwareJson()` -> `src/core/ControlCommandsMixerSurround.cpp:87` (`mixer.surround_get`'s `hardware`
  status).

Both `start` and `count` are signed `int` and `count` is positive at every call site
(`surround::channelCount(...)` is 1..64), so with `start = INT_MAX - 1` and `count = 2` the expression
`start + count` is signed overflow: undefined behaviour that in practice wraps to `INT_MIN`, which
satisfies `<= outputChannels()` and `<= HardwareOutputBus::MaxChannels` while `start >= 0` holds. The
check then reports that the device carries the route. This is exactly what the test below observed on the
unfixed code.

How a `start` that large reaches the predicate: `audioio::Destination::start` is an `int`
(`include/AudioIoSetup.h:84`). The command path bounds a route (`audioio::parseChannels`,
`src/core/AudioIoSetup.cpp:185-192`), but `audioio::parseDestination` (`:338-342`) also accepts a stored
**bus** unchanged (`*out = Destination{bus.start, bus.width}`), and `sideFromStorage`
(`src/core/AudioIoSetup.cpp:124-136`) copies `start` out of the stored I/O setup JSON
(`ControlHardwareIoSupport::readDestination`, `src/core/ControlHardwareIoSupport.cpp:116-130`, keeps the
setup's buses and only widens the channel count). A hand-edited config entry therefore reaches
`ChannelHardwareIo::output` and then `deviceCarries` with an unbounded `start`.

Blast radius, stated honestly: `HardwareOutputBus::add` (`src/core/HardwareOutputBus.cpp:60-70`) and
`addPlane` (`:81-84`) already reject a channel outside the bus with the overflow-safe form
`start > MaxChannels - sides`, so no out-of-bounds write happens through the bus - the old comment there
says as much ("Overflow-safe: start + sides could overflow for a start near INT_MAX"). What the defect
actually produced is undefined behaviour in the predicate plus a wrong answer: the sides were treated as
carried and then silently dropped instead of folded into the pair, and `mixer.surround_get` reported
`status: ok` (and `start + count`, itself overflowing, in `outputs`) for a route that is off the device.

## Fix

```cpp
bool deviceCarries(int start, int count)
{
	// The route is start .. start + count - 1, but the sum is never formed: a start near INT_MAX wraps
	// to a negative number and passes every upper bound (cluster new-p10-259). A count of zero or less
	// is no route at all, and no caller asks for one.
	const AudioDevice* device = Engine::audioEngine()->audioDev();
	if (device == nullptr || count <= 0) { return false; }
	const int limit = std::min(static_cast<int>(device->outputChannels()), HardwareOutputBus::MaxChannels);
	return start >= 0 && start <= limit - count;
}
```

`limit` is the device's own count clamped to the bus ceiling, `limit - count` cannot overflow (`limit`
is at most 128, `count` at least 1), and a `count` above `limit` makes `limit - count` negative, so a
non-negative `start` never satisfies it. Realtime: three integer comparisons on the render thread, no
allocation, no lock, no logging. No control command was added, so the ControlCommandsSnapshot canary is
untouched (`ControlCommandsSnapshot` ran in the family run below and passed).

### Siblings and callers checked

| Place | State |
| --- | --- |
| `src/core/MixerSurroundMix.cpp:187`, `:427` (the two product callers) | pass `channelCount(...) >= 1`; same code, now overflow-safe |
| `src/core/ControlCommandsMixerSurround.cpp:87` | passes `channelCount(...) >= 1`; same code |
| `src/core/HardwareOutputBus.cpp:63-70` (`add`) | already overflow-safe (`start > MaxChannels - sides`); still the guard that drops a route whole |
| `src/core/HardwareOutputBus.cpp:81-84` (`addPlane`) | range-checks `channel` before indexing; unchanged |
| `src/core/AudioIoSetup.cpp:185-192` (`parseChannels`) | bounds a numeric route to 1..128, so a parsed project route cannot overflow here; unchanged |
| `src/core/AudioIoSetup.cpp:338-342` (`parseDestination`, stored buses) | the unbounded way in, reported under Other findings |

`count <= 0` now answers false. No caller passes a non-positive count (the three sites above all use
`surround::channelCount()`, which is 1..64), the predicate is `LMMS_EXPORT` but no other file calls it
(`git`-visible use: the three sites plus the declaration at `include/MixerSurround.h:287`), and an empty
route is not a route to route. For every positive count the answer is identical to the old code for
every start that does not overflow, so project load, save and rendering of existing projects do not
change. The header declaration is untouched, so nothing needed recompiling for a signature change.

## Regression test

`tests/src/core/WideDiscreteBusTest.cpp:260` - `theDeviceBoundsCheckDoesNotOverflow()`. The file already
runs a 128-output `WideOutputs` device that never starts, which is the widest route this predicate
decides on. (The slot was first written in `SurroundTest.cpp`; that pushed the file to 516 counted lines
and tripped the file-length ratchet, whose log is `gate-8-file-length.log`. It was moved here, where the
device is the bus's own ceiling.)

Matrix covered by the slot: the normal first pair `(0, 2)` and last pair `(126, 2)` true; a mono last
channel `(127, 1)` true; one past the device `(127, 2)` and a pair that runs off it `(126, 3)` false; no
route `(-1, 2)` false; the overflow cases `(INT_MAX - 1, 2)` and `(INT_MAX, 2)` false; degenerate counts
`(0, 0)` and `(0, -2)` false. No timing, no sleeps, no threads: the predicate reads only the installed
device and its arguments. Audio-thread constraints are the code path's own: the test drives
`deviceCarries` directly, and the family run exercises the render paths that call it.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-259.r6/logs`.

RED (product change reverted, test present):

```
git diff -- src/core/MixerSurroundMix.cpp > LOG/fix.patch        EXIT=0   (fix.patch, 20 lines)
git apply -R LOG/fix.patch                                       EXIT=0
cmake --build build --target zene WideDiscreteBusTest -j4 > LOG/build-7-red-wide.log 2>&1   EXIT=0
cd build/tests && ctest -R '^WideDiscreteBusTest$' --output-on-failure > LOG/test-8-red-wide.log 2>&1   EXIT=8
```

`test-8-red-wide.log`:

```
FAIL!  : WideDiscreteBusTest::theDeviceBoundsCheckDoesNotOverflow() '!surround::deviceCarries(Max - 1, 2)' returned FALSE. ()
Totals: 8 passed, 1 failed, 0 skipped, 0 blacklisted, 517ms
0% tests passed, 1 tests failed out of 1
```

GREEN (fix re-applied):

```
git apply LOG/fix.patch                                          EXIT=0
cmake --build build --target zene WideDiscreteBusTest -j4 > LOG/build-8-green-wide.log 2>&1   EXIT=0
cd build/tests && ctest -R '^WideDiscreteBusTest$' --output-on-failure > LOG/test-9-green-wide.log 2>&1   EXIT=0
```

`test-9-green-wide.log` ends `100% tests passed out of 1`, `Total Test time (real) = 0.72 sec`.

Family, final tree (the same files as committed):

```
cmake --build build --target zene SurroundTest WideDiscreteBusTest SurroundExportTest HardwareIoTest \
  SurroundDevicePathTest MixingRoutingTest WidePinRoutingTest SurroundPannerTest -j4 > LOG/build-9-final-family.log 2>&1   EXIT=0
cd build/tests && ctest -R '^SurroundTest$|^WideDiscreteBusTest$|^SurroundExportTest$|^HardwareIoTest$|^SurroundDevicePathTest$|^MixingRoutingTest$|^WidePinRoutingTest$|^SurroundPannerTest$|^ControlCommandsSnapshot$' \
  --output-on-failure -j2 > LOG/test-10-final-family.log 2>&1   EXIT=0
```

`test-10-final-family.log`: `100% tests passed out of 9` (9/9 passed, including `ControlCommandsSnapshot`).

An earlier two-step version of this cycle (test in `SurroundTest.cpp`) is in the same folder:
`build-3-reverted.log` / `test-4-reverted-red.log` (EXIT=8, same wrap failure) and
`build-4-reapplied.log` / `test-5-reapplied-green.log` (EXIT=0).

## Gates

`python3 tests/... ` individually, from the worktree (logs `gate-2..gate-15`):

| gate | exit | log |
| --- | --- | --- |
| fork-sources | 0 | `gate-10-fork-sources.log` |
| all-sources-reproduce | 0 | `gate-11-all-sources.log` |
| no-upstream-regression | 0 | `gate-12-no-upstream.log` |
| check-namespace | 0 | `gate-14-namespace.log` |
| no-tautology | 0 | `gate-13-no-tautology.log` |
| doc-figures | 0 | `gate-15-doc-figures.log` |
| file-length (`--check`) | 0 | `gate-9-file-length.log` - `PASS (check mode: no regressions; baseline not written)` |
| complexity (`--check`) | 2 | `gate-7-complexity.log` - `lizard is not installed — run: pip install lizard` |

`feature_check.py` itself cannot run in this session. It needs a baseline listing for the base commit
(`base = merge-base(HEAD, FIX-BASE) = 9913d36d53`, i.e. HEAD), and it gets one by
`git -C ~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach <base>`, which the
lane's git shim refuses (log `gate-1.log`):

```
bunny-guard: 'git -C .../gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4'
  is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
Feature_check.py: CalledProcessError: ... returned non-zero exit status 97.
```

I did not work around the shim. The cache file it would write
(`gate-baselines/9913d36d5397025d1b7092097f50f6d50c652dc4.json`) is on a read-only mount as well
(`OSError: [Errno 30] Read-only file system`), so the baseline step is unavailable either way. The two
ratchet rows are run directly from the table above instead. Complexity cannot be measured here because
`lizard` is absent and `pip install --user lizard` is refused by PEP 668 (`pip-lizard.log`); the fix adds
no function, and `deviceCarries` keeps a flat body of two branches (old: one return with three `&&`) -
far under the CCN 10 target - but I did not measure it, so I do not claim it.

## Self-check

- *Is the claim backed by a run I saw?* Yes: `test-8-red-wide.log` (EXIT=8, the named FAIL line) and
  `test-9-green-wide.log` (EXIT=0), plus the 9-test family run `test-10-final-family.log` (EXIT=0).
- *Does the test fail without the fix for the intended reason?* It fails on `deviceCarries(Max - 1, 2)`
  returning true, i.e. the wrap. The same slot also pins `(Max, 2)` and the degenerate counts.
- *Collateral:* no header changed; the three call sites all pass `channelCount() >= 1`; the answer for
  every non-overflowing input is unchanged, so saved projects and their saved routes load as before.
  No command added, no undo step added, nothing allocating or locking on the render path.
- *Two files under `src/core/Song*`/`*Record*`/`*Undo*`/`*Transport*`/`Take*`/`AudioEngine*`?* No; the
  product change is `src/core/MixerSurroundMix.cpp` only, so no `state-sequence` artifact is due.
- *`src/gui` change?* No.
- *Registration:* `src/core/MixerSurroundMix.cpp` and `tests/src/core/WideDiscreteBusTest.cpp` are both
  already listed in `tests/fork-sources.txt`; that gate exits 0.
- *`git status` clean after the commit?* Verified below in the evidence block: the only paths touched are
  the two product/test files and this report.

## Other findings

1. `tests/src/core/ControlRenderSurround` (ctest #950) fails on this branch for a reason unrelated to
   this cluster: its CLI refusal checks expect `--channels takes` / `--channels surround is for` in the
   usage text. It fails identically with this product change reverted
   (`test-4b-reverted-render.log`, EXIT=8), so it is pre-existing on `050/m0-train-52`, not caused here.
2. `src/core/ControlCommandsMixerSurround.cpp:87-90` (`hardwareJson`) has the same shape of addition:
   `out.start + (wide ? count : out.width)` for the `outputs` label. With the fix `wide` is false for an
   extreme start, but `out.start + out.width` can still overflow in the label. It is a display string,
   not a routing decision, and changing it would move control-command output for inputs this bug fix
   does not own; left alone and recorded here.
3. `src/core/AudioIoSetup.cpp:338-342` (`parseDestination`'s bus match) and `:124-136`
   (`sideFromStorage`) pass a stored bus `start` through without a range check - the reachable path into
   `deviceCarries` described above. The numeric path (`parseChannels`, `:185-192`) is bounded. A range
   check here would change how a hand-edited setup loads, which is a separate decision from this fix.
