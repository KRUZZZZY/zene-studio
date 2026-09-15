# The loudness surface: making the BS.1770-4 meter reachable

**Lane:** `030/meter-surface` (**worktree** `zene-030/wmeter`) · **Base:** `release/0.3.0` @
`f611c888b` · **Board task:** 654 · **Feature row:** 24 of `docs/FEATURE-LIST-0.3.0.md`
("LUFS / loudness metering", section 5 · audit Table B #3, "in the tree but not drivable
through the socket").
**One-line verdict:** the merged BS.1770-4 meter now has an agent surface — a **passive tap on
the live master** and a **file measurement of any rendered audio**, both reporting LUFS-I,
LUFS-M, LUFS-S, the loudest short-term window and true peak — and the render path's own
`.loudness.txt` report is reachable from the socket for the first time. **No DSP was forked:**
every number comes out of the merged `LufsMeter`.

---

## 1. What was wrong, in the row's own words

`docs/FEATURE-LIST-0.3.0.md` row 24 recorded the feature as **partial** with this reason:

> engine and tests are in the tree (`LufsMeterTest`, `LoudnessReportTest`, `MasteringTest`);
> there is no `lufs.` or `meter.` group and `export.get_settings` does not expose it

and the audit counted it in its sixteen "in the tree but not drivable through the socket"
features (Table B #3). `docs/LUFS-METER.md` and `docs/LUFS-WIRING.md` had already recorded the
two halves of the story: the measurement core landed inert (`post-alpha/lufs-meter`), and a
second lane wired it into the render path (`post-alpha/lufs-wire`) — which also stated, plainly,
that **the live path was not attempted**: *"Nothing in the live audio thread constructs or feeds
a meter … the bus tap, the `std::atomic` enable flag, the polling widget and its own
allocation/`-inf` assertions are not written"*.

That is the failure mode this lane exists to close: a meter nothing can reach is not a feature.

## 2. What was built

| File | Status | What it is |
|---|---|---|
| `include/MasterLoudnessTap.h` (201 lines) | new | `lmms::MasterLoudnessTap`: the passive live tap. Owns one `LufsMeter`, publishes a lock-free snapshot (`enabled`, rate, channels, blocks/frames fed, the five values) |
| `src/core/MasterLoudnessTap.cpp` (183 lines) | new | `feed()` (audio thread, reads only), `setEnabled()`/`reset()` (control thread, bounded quiesce), `snapshot()` (any thread) |
| `src/core/ControlCommandsMeter.cpp` (338 lines) | new | the `meter.*` group: `meter.get_state`, `meter.arm`, `meter.measure_file`, the live JSON reader and the registration |
| `src/core/ControlCommandsMeterFile.cpp` (257 lines) | new | `meter.measure_file`'s document reader (libsndfile, bounded chunks, interleaved ≤2ch / planar 3–6ch) and the group's shared vocabulary (`include/ControlMeterSupport.h`): the null-for-sentinel rule, the five key names, the EBU R 128 target |
| `include/ControlMeterSupport.h` (104 lines) | new | the declarations above, so the live half and the file half publish one vocabulary |
| `src/core/ControlReversibilityTableMeter.cpp` | new | the feature's four A16 rows, joined into `reversibilityRowTable()` |
| `include/AudioEngine.h` / `src/core/AudioEngine.cpp` | wired | the tap is constructed once (engine processing rate, stereo) and fed the period `renderStageMix()` has just mixed |
| `include/LoudnessReport.h` / `src/core/LoudnessReport.cpp` | wired | `addPlanarBlock()`, so a mono or 5.1 file is measured with BS.1770-4's channel weights (the interleaved path would read a mono file 3 LU loud) |
| `include/ExportRenderSettings.h` / `src/core/ExportRenderSettings.cpp` | wired | the process-wide `loudnessReport()` selection |
| `include/OutputSettings.h` | wired | the render's report flag defaults from that selection (the dither / `srcQuality` rule), so an agent's choice reaches the next render |
| `src/core/ControlCommandsExport.cpp` | wired | `export.get_settings` **exposes `loudness_report`**; new `export.set_loudness_report` |
| `src/core/ControlCommandsProject.cpp` | wired | `render.render`'s child gets `--loudness-report` when the selection is on, so a socket-driven render writes the sidecar |
| `src/core/main.cpp` | wired | `--loudness-report` also sets the process-wide value (as `--dither` does) |
| `tests/src/core/MeterTapTest.cpp` | new | the **registered ctest `MeterTapTest`**: the live tap's contract and the three negative controls |
| `tests/control-meter-commands.py` | new | the **registered ctest `ControlMeterCommands`**: the same controls over `--control-socket` |

Registration (the places this tree requires): `src/core/CMakeLists.txt` (four sources),
`tests/CMakeLists.txt` (`LMMS_TESTS` entry + `add_test(ControlMeterCommands …)`),
`tests/fork-sources.txt` (the six new files), `tests/upstream-modifications.txt`
(`include/AudioEngine.h`, `src/core/AudioEngine.cpp`, `include/OutputSettings.h`,
`src/core/main.cpp` — each with its reason appended in the same commit), and the A16 table join
in `src/core/ControlReversibilityTable.cpp`.

## 3. The four ids, and what each one is

| id | A16 | What it does |
|---|---|---|
| `meter.get_state` | `not_mutating` | the LIVE master readout: `live.enabled`, `live.sample_rate`, `live.channels`, `live.blocks_fed`, `live.frames_fed`, `live.seconds_fed`, `live.engine_running`, and `integrated_lufs` / `momentary_lufs` / `short_term_lufs` / `short_term_max_lufs` / `true_peak_dbtp`; plus `target` (the standard it reports against) |
| `meter.arm` | `true_inverse` (recorded action) | arm / disarm the tap. **Arming starts a measurement, every time** - including a re-arm, so two measured sections can never be silently averaged into one number; **disarming keeps the last reading readable**. A caller that wants the running measurement to continue simply does not re-arm it |
| `meter.measure_file` | `not_mutating` | measure a **rendered file** now, from its bytes: the same five values, the EBU R 128 verdict, the target, and the file's facts (rate, channels, frames, duration, bytes, sha256, libsndfile's container code). 1–6 channels; more is refused, typed |
| `export.set_loudness_report` | `true_inverse` (recorded action) | the render path's `.loudness.txt` report for the next render — the second half of row 24's complaint |

**Readings are JSON `null` when the meter has no measurement** (silence, or a window that has not
filled) — never `-70`, never `-inf`, never a plausible number. That follows
`MasteringReport.cpp`'s convention for the same values, so one rule parses across the release.

### The re-arm contract, and how it was found

`meter.arm {enabled: true}` **always** starts a fresh measurement, a re-arm of an already-armed tap included.
The first draft made a re-arm a no-op ("do not discard a running measurement"), and the registered socket
proof caught what that costs: it measures two fixtures in a row (*arm, play tone-23, read, arm, play tone-33,
read*) and the second reading came out **2.59 LU** from the first instead of the fixtures' known **10 LU**
difference - the loud fixture's blocks were still in the integrated value, because the second `arm` had
declined to reset. The per-fixture *momentary* value was correct (-33.00), which is exactly how a silent
averaging bug looks from outside. The contract now restores on every arm (bounded quiesce, in place), the
C++ test asserts the reset (`aLouderSignalReadsProportionallyHigher` re-arms instead of disarming first),
and the socket proof asserts the 10 LU separation LIVE, which is what caught it.

## 4. The tap: why it is honest

```
AudioEngine::renderStageMix()
  mixer->masterMix(m_outputBufferWrite.get());
  MixHelpers::multiply(..., m_masterGain, m_framesPerPeriod);
  if (m_masterLoudness) m_masterLoudness->feed(m_outputBufferWrite.get(), m_framesPerPeriod);   // reads only
  emit nextAudioBuffer(m_outputBufferRead.get());
```

* **Passive.** `feed()` takes `const SampleFrame*` and hands the same pointer to
  `LufsMeter::processBlock()`, which also takes it `const`. There is no copy, no second render
  and no re-read of a file anywhere in this path — the samples that reach the device are the
  samples that were measured. `MeterTapTest` hashes a buffer before and after feeding it through
  an armed tap and asserts it is bit-identical.
* **Realtime-safe.** The meter is constructed **once**, with the tap, at engine construction
  time (engine processing rate, stereo). `feed()` allocates nothing, locks nothing and grows
  nothing; the readings are published into plain atomics. `MeterTapTest` asserts **0
  allocations** over 64 fed blocks (`AllocationProbe`, the tree's gold standard).
* **Off by default.** A disarmed tap returns on its first load: one relaxed atomic load per
  audio period, and no metering at all. This is `docs/LUFS-METER.md`'s "the meter is opt-in"
  rule kept intact for the live path.
* **Toggleable while the transport runs.** Arming/disarming/resetting go through a **bounded**
  quiesce (`MasterLoudnessTap::QuietWaitMs`, 250 ms) on the control thread and never replace or
  free the meter object, so a period already being measured always completes against a live
  object. The residual window — a block whose `enabled()` read predates a transition — can add
  at most one period of samples to a just-reset measurement; it cannot corrupt memory (the meter
  writes only its own fixed arrays) and cannot change any audio (the tap reads). The header
  states this; it is not hidden.
* **Two entry points, one meter each.** The live half measures through the tap ('s `LufsMeter`);
  the file half measures through `LoudnessReport` (which owns a `LufsMeter` and is the exact
  object the render path feeds). No DSP lives in the command group.

## 5. The proof, and what it actually asserts

**`MeterTapTest`** (`tests/src/core/MeterTapTest.cpp`, registered in `LMMS_TESTS`) — the live tap
in process:

| slot | assertion |
|---|---|
| `aDisarmedTapMeasuresNothing` | a never-armed tap fed a −23 dBFS sine reports `blocksFed 0`, `framesFed 0` and the sentinel for all five values |
| `silenceReadsTheSentinel` | **control 1** — 5 s of digital silence through an armed tap: every value stays `-inf`, never a number |
| `anArmedTapReadsTheLevelItWasFed` | EBU Tech 3341 case 1 (−23 dBFS, 5 s) reads −23.0 LUFS-I within ±0.1 LU, and the short-term maximum is the loudest window |
| `aLouderSignalReadsProportionallyHigher` | **control 2** — case 1 vs case 2 through one tap: 10 dB more input reads 10 LU more, ±0.1 LU; and re-arming starts from the sentinel |
| `feedingAnArmedTapDoesNotChangeOneByte` | **control 3** — FNV-1a hash of the buffer before/after feeding an armed tap: identical, with a live reading beside it so the passivity is not the passivity of a tap that did nothing |
| `feedingAnArmedTapAllocatesNothing` | 64 armed feeds + 64 snapshots allocate **0** |
| `disarmingKeepsTheLastReadingAndStopsCounting` | the counters freeze and the reading stays readable – and still reads −23.0 |
| `resetStartsANewMeasurementWhileStayingArmed` | `reset()` returns true, stays armed, and zeroes the measurement |
| `theSnapshotReportsTheMetersOwnConfiguration` | rate and channel count come from the meter, not from the caller |

**`ControlMeterCommands`** (`tests/control-meter-commands.py`, registered as a ctest) — the group
over `--control-socket`, on the real binary, with the tree's own fixtures
(`tests/data/loudness/make-fixtures.py` — EBU Tech 3341 case 1 / case 2 / digital silence):

* registration: the three `meter.*` ids and `export.set_loudness_report` are in
  `control.commands_list`, with schemas and the right `mutating` flags, and
  `export.get_settings` carries `loudness_report`;
* a never-armed tap reports `armed false`, `blocks_fed 0` and **null** readings;
* arming records a reversible action checkpoint, starts a **fresh** measurement, and
  `blocks_fed` then **grows on its own** — the engine's audio thread is feeding the tap, which is
  what makes this a live tap and not a fixture;
* **control 1 (silence → null):** `tone-23.wav` measures −23.00 and `tone-33.wav` −33.00 LUFS-I
  (±0.1 LU), while `silent.wav` reports null readings, `measured false` and verdict
  `NOT MEASURED`;
* **control 2 (proportional):** the two fixtures are 10 dB apart and read **10 LU apart**, from a
  file AND from the **live** tap with the two fixture projects playing through the engine (where a
  constant-reading tap cannot produce the difference);
* **control 3 (byte-identical):** the sha256 `meter.measure_file` reports equals the file's
  sha256 computed outside the instance before the call (measuring a file does not touch it), and
  two renders of one project have **identical PCM frames** with the live tap armed and disarmed;
* typed refusals: missing / relative / absent path (`invalid_args`), an empty file and an
  8-channel file (`refused`); and `control.undo` returns the armed flag to its previous state.

## 6. What is NOT done, stated rather than implied

* **No meter in the interface.** There is no loudness widget, no readout, no meter bridge, no
  loudness column and no R128 history graph; `grep -rniI 'lufs\|loudness' src/gui/` finds only the
  export dialog's pre-existing report checkbox and its result label. The socket is the only way to
  watch a level in this release. (One line in `docs/KNOWN-LIMITATIONS.md` and
  `docs/RELEASE-NOTES-v0.3.0-alpha.md`.)
* **Master mix only.** The tap measures the summed master output. There is no per-track, per-bus
  or per-chain loudness, and no LUFS-M history/curve — the meter's own momentary value is the last
  400 ms, not a graph.
* **No LRA / loudness range**, and no EBU Tech 3342 statistics: the surface publishes what
  `LufsMeter` measures and nothing it does not.
* **Stereo in the application.** The tap is built with `DEFAULT_CHANNELS`. `meter.measure_file`
  accepts 1–6 channels (mono and stereo exactly, 5.1 with BS.1770-4's channel weights via
  `LoudnessReport::addPlanarBlock`), refuses a file with more than 6 channels, and does not do
  7.1 / Atmos layouts.
* **`meter.measure_file` measures; it does not make audio.** It reads a file this instance can
  open — it does not fetch, decode an arbitrary container, or render a project (`render.render`
  and `mastering.run` own rendering).
* **No reference cross-check of the LIVE tap** against a second BS.1770-4 implementation.
  `docs/LUFS-WIRING.md` §4.1 did that for the rendered file (`bs1770_reference.py`); the live
  readings here are checked against the EBU fixture levels and against the file path's own
  measurement of the same material, which is a consistency check, not an independent one.
* **The build/test state of this lane is in `LANE-STATE-METER-SURFACE.md`**, including anything
  that had not been run when the lane closed. Nothing here claims a green run that was not
  observed.

## 7. Verified state of this lane (what was run, with exit codes)

```
$ cmake --build build -j2 --target zene MeterTapTest audiofileprocessor ; echo EXIT=$?
EXIT=0
$ cd build/tests && ./MeterTapTest > /tmp/meter-tap2.log 2>&1; echo EXIT=$?
EXIT=0        # Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted  (the three negative controls included)
$ cd build/tests && ctest -R ControlMeterCommands ; echo EXIT=$?
EXIT=0        # 1/1 Passed 13.35 sec - 28/28 checks over --control-socket, including both LIVE controls
$ python3 tools/mcp-zene-control/snapshot_commands.py --socket <own instance>; echo EXIT=$?
EXIT=0        # 231 commands, the four new ids among them
$ cd build/tests && ctest -R ControlCommandsSnapshot ; echo EXIT=$?
EXIT=0        # the MCP tooling spine now carries meter.arm / meter.get_state / meter.measure_file /
              # export.set_loudness_report
```

**Not run in this lane:** the rest of the ctest suite, `tests/run-all-gates.sh` and the static gates. The
lane's worktree was also the site of one unrelated, self-inflicted build interruption
(`AutomationModesTest`, `undefined reference to 'main'`, from a stale object after this lane killed its own
build) — its `.o` was removed so it rebuilds; the parent's full build will confirm. `LANE-STATE-METER-SURFACE.md`
§4 lists exactly what is still open.

## 8. How to reproduce

```
# build + tests (the CI reproduction script, adapted to this box: Qt6 dev files, no Qt5)
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wmeter
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 2

# the live tap's own test
cd build/tests && ./MeterTapTest > /tmp/meter-tap.log 2>&1; echo EXIT=$?

# the group over the socket (starts the real binary and generates the EBU fixtures)
cd build/tests && ctest -R ControlMeterCommands --output-on-failure

# one command by hand, against a running instance
python3 - <<'PY'
import json, socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect("/path/to/zene.sock")
for cmd in ({"id":1,"cmd":"meter.get_state"},
            {"id":2,"cmd":"meter.arm","args":{"enabled":True}},
            {"id":3,"cmd":"meter.measure_file","args":{"path":"/abs/tone-23.wav"}}):
    s.sendall((json.dumps(cmd) + "\n").encode())
    print(s.recv(65536).decode())
PY
```
