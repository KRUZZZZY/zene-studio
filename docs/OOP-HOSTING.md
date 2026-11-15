# Out-of-process plugin hosting: the opt-in per-plugin toggle

**Status:** landed on `post-alpha/oop-hosting` as commit `0e1cdcef6` (base
`0c23587d2`). **Scope:** ZynAddSubFx only. **Verdict:** the toggle exists, is
persisted, is reachable from the instrument view and from the project file, and
its isolation is proven at the level this box can prove it: the client is a
separate PID, the host survives killing it, notices the exit, and the audio for
that plugin stops. The default path is unchanged inside the renderer's own
float-rounding noise, and exactly unchanged for every project that does not
contain a ZynAddSubFx instrument.

This document is written so that everything in it can be re-run: the fixture
project and the three experiment scripts are in `tests/data/oop-hosting/`
(`README.md` there describes them), and every command below is the command that
produced the output below it.

---

## 1. What happens today (before this change)

### 1.1 The machinery this toggle uses

The remote-plugin machinery was rewritten for the planar audio-port migration in
#589 and is intact:

- `include/RemotePlugin.h:46` — `ProcessWatcher`, the thread that starts the
  client process: `src/core/RemotePlugin.cpp:93` (`process.start(...)` at
  `:96`), and `:123-131` where the watcher invalidates the plugin when the
  client stops answering.
- `src/core/RemotePlugin.cpp:231` — `RemotePlugin::init()` resolves the client
  executable through the `plugins:` search path (or `LMMS_PLUGIN_DIR`,
  `:245-246`) and starts it.
- `src/core/RemotePlugin.cpp:465` — `updateAudioBuffer()` allocates the shared
  audio block (channel-major planar planes, inputs then outputs) and tells the
  client its key (`IdChangeSharedMemoryKey`).
- `src/core/RemotePlugin.cpp:367` — `process()`, the per-period round trip;
  `include/RemotePluginClient.h:340` — the client's `doProcessing()`.
- `include/RemotePluginAudioPorts.h:198` — `ConfigurableAudioPorts`, the
  runtime switch between the remote plugin's shared block and a local buffer
  (`setBufferType()`/`isRemote()`, `:221-226`) that #589 added for the one
  instrument that has both implementations. The toggle in this change drives
  exactly that switch.

### 1.2 What synchronises the audio thread (unchanged by this change)

This matters because the toggle must not add anything to that path, so it is
stated precisely:

- **The transport on this build is a socket, not the shared-memory FIFO.**
  `SYNC_WITH_SHM_FIFO` is defined only when the platform lacks SysV IPC
  (`include/RemotePluginBase.h:38-48`), and this build has both headers
  (`build/lmmsconfig.h:48-49`: `LMMS_HAVE_SYS_IPC_H`, `LMMS_HAVE_SEMAPHORE_H`),
  so the socket half compiles: the server socket is created in the
  `RemotePlugin` constructor (`src/core/RemotePlugin.cpp:149-176`) and the
  client connects in `RemotePluginClient`'s constructor
  (`include/RemotePluginClient.h:189-211`).
- **The audio thread blocks on a full round trip per period.**
  `RemotePlugin::process()` takes the recursive mutex (`include/RemotePlugin.h:164-172`,
  the lock at `src/core/RemotePlugin.cpp:390`) and then does
  `sendMessage(IdStartProcessing)` followed by `waitForMessage(IdProcessingDone)`
  (`src/core/RemotePlugin.cpp:414-426`); the reads and writes underneath are
  blocking `::read()`/`::write()` on the socket
  (`include/RemotePluginBase.h:586-636`). Measured on this box: rendering 118 s
  of audio through one remote ZynAddSubFx instrument takes **5.76 s wall
  clock** (`PERFLOG | Project Render | 4.26user, 6.92system 5.76elapsed`), so
  the round trip costs well under real time here — but it is a blocking wait,
  not a lock-free handshake.
- The shared-memory-FIFO variant of the same class (compiled out here) busy-waits
  with `usleep(5)` between lock attempts
  (`include/RemotePluginBase.h:254-261` and `:281-297`) and takes a
  `SystemSemaphore` for the FIFO management data (`:159-174`,`:233-235`). That
  is the pre-existing design on platforms without SysV IPC.
- **This change adds nothing to any of that**: no new mutex, no blocking call,
  no allocation on the audio path. The added work is a `BoolModel` read on the
  load/UI path and one branch in `initPlugin()`.

### 1.3 Which plugin families use the remote path, and how they are selected

| family | today | selected by |
|---|---|---|
| VST2 (Vestige instruments, VstEffect effects) | **always** a separate process | the plugin implementation: `VstPlugin : public RemotePlugin` (`plugins/VstBase/VstPlugin.h:45`), whose constructor calls `tryLoad()` (`plugins/VstBase/VstPlugin.cpp:174`, `:177`, `:181`) → `init(remoteVstPluginExecutable, ...)` (`:213-215`). Created at `plugins/Vestige/Vestige.cpp:364` and `plugins/VstEffect/VstEffect.cpp:154`. There is no in-process VST2 host in this tree, so there is nothing to toggle here. |
| ZynAddSubFx | the **in-process** synth, unless the user pressed "Show GUI" in this session | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:492` (`m_separateProcessModel.value() || m_hasGUI` since this change; `m_hasGUI` alone before it), with `m_hasGUI` declared at `plugins/ZynAddSubFx/ZynAddSubFx.h:126` and only ever set by the view's Show-GUI button (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:707-717`). **Not saved**, so a reloaded project starts in-process again. |
| VST3, CLAP | in-process | plugin modules loaded into the `lmms` process |
| native (TripleOscillator, Kicker, …), LADSPA, LV2, SF2/GIG | in-process | same |

**Can a user opt in or out today?** Only in one place, and only for Zyn: the
"Show GUI" button switches that instrument to the client process and shows the
client's window — it is per session and lost on reload. VST2 has no choice at
all: the remote process is the only VST2 path.

---

## 2. The toggle (this change)

**Name and default.** `separateprocess`, a per-instrument boolean, default
`false` — i.e. absent means "in-process", which is what every project saved
before this change carries.

**Storage.** A per-instance `BoolModel m_separateProcessModel`
(`plugins/ZynAddSubFx/ZynAddSubFx.h:157`), written by the plugin's own
`saveSettings()` as an attribute of the plugin's element
(`plugins/ZynAddSubFx/ZynAddSubFx.cpp:194`) and read by `loadSettings()`
(`:236`). This is the same convention the neighbouring `forwardmidicc` uses
(`:193`, and the model itself), so no second format was invented: a saved
ZynAddSubFx instrument now looks like

```xml
<instrument name="zynaddsubfx">
  <zynaddsubfx portamento="0" ... forwardmidicc="0" separateprocess="1">
    <ZynAddSubFX-data .../>
  </zynaddsubfx>
</instrument>
```

**Effect.** `initPlugin()` instantiates `RemoteZynAddSubFx` (started by
`RemotePlugin`, `plugins/ZynAddSubFx/ZynAddSubFx.cpp:490-527`) when the setting
or the Zyn GUI asks for it, and `LocalZynAddSubFx` otherwise. When a *stored*
choice differs from what is running, `loadSettings()` re-instantiates before
loading the patch (`:237-240`) — deliberately not through `reloadPlugin()`,
which re-enters `loadSettings()`. The Zyn window is requested only when the
user asked for the GUI (`:521-523`), so a headless render can host the client
without a display.

**Where a user turns it on** (two ways, both real):

1. **In the DAW:** the ZynAddSubFx instrument view has a "Run in a separate
   process" checkbox (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:601`, tooltip
   `:602-604`, wired at `:616`, state mirrored from the model at `:698`,
   handler `separateProcessToggled()` at `:720-736`). Ticking it re-instantiates
   the instrument with the patch carried over.
2. **In the project file / headlessly:** the `separateprocess` attribute above.
   `tests/data/oop-hosting/zyn-separate-process-on.mmp` is a project that only
   differs from its `-off` sibling in that one attribute.

**Diagnostics.** `ZynAddSubFxInstrument::hostingState()`
(`plugins/ZynAddSubFx/ZynAddSubFx.cpp:416-425`) answers `in-process`,
`separate-process` or `separate-process-exited`. It is `Q_INVOKABLE` (declared
`plugins/ZynAddSubFx/ZynAddSubFx.h:100`) so a test host can read it without a
header for the plugin class — which is how the new test asserts the mode.

**Scope, explicitly.** In scope: ZynAddSubFx. Out of scope and still
in-process: VST3, CLAP, native modules, LADSPA, LV2, SF2/GIG — none of them has
a client executable in this tree, so there is nothing to host out of process
until the plugin API grows one. VST2 is *already* fully out of process and its
remote path is not optional, which is why this toggle is not "VST2 first": the
backlog's smallest honest version assumed the VST2 remote path was something a
user could choose; in this tree it is what VST2 *is*, and the family that
actually has two implementations is ZynAddSubFx.

---

## 3. Evidence

### 3.1 Build and tests

The CI `linux-x86_64` command with the one printed deviation (no Qt5
development files on this box):

```console
$ cd <worktree>
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
...
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
local-ci: overall exit=0 (0 = every executed step passed)
```

26 tests, not 25: the new one is in the run —

```
      Start 25: ZynSeparateProcessTest
26/26 Test #25: ZynSeparateProcessTest ...........   Passed    2.18 sec
```

Standalone, the new test's own totals and its two informative lines:

```
PASS   : ZynSeparateProcessTest::defaultIsInProcess()
QINFO  : ZynSeparateProcessTest::separateProcessChoiceSpawnsAClient() separate process: RemoteZynAddSubFx is a child of pid 1326011
PASS   : ZynSeparateProcessTest::separateProcessChoiceSpawnsAClient()
PASS   : ZynSeparateProcessTest::savedChoiceRoundTripsThroughSaveAndReload()
QINFO  : ZynSeparateProcessTest::hostSurvivesAndNoticesTheClientExit() client pid 1326184 was killed and the host kept running (pid 1326011)
PASS   : ZynSeparateProcessTest::hostSurvivesAndNoticesTheClientExit()
Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 2090ms
```

`tests/run-all-gates.sh --no-mutation` (Gate 5 skipped on purpose; Gate 2 needs
`--with-coverage` and was not run):

```
1      ctest                    PASS
3      no-tautology             PASS
4      complexity               PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS
RESULT: PASS — every executed gate passed
```

Gate 6 passing means the two edited upstream-inherited files are declared in
`tests/upstream-modifications.txt` with a reason naming this change (and the
new test file is not source, so it needs no entry).
`tests/scripted/check-namespace` also reports `0 errors.`;
`tests/scripted/check-strings` could not run here (`ModuleNotFoundError:
No module named 'tinycss2'`) — an environment gap, not a result.

### 3.2 The default is unchanged

Two comparisons, on a project with a ZynAddSubFx instrument whose choice is
`separateprocess="0"` (`tests/data/oop-hosting/zyn-separate-process-off.mmp`;
sha256 `5539f083…cf1a11`), rendered with `-f wav -a` (32-bit float) at 44.1 kHz:

| comparison | max abs delta | relative to peak | samples differing |
|---|---|---|---|
| **pristine build vs this change** (`default-d1.wav` vs `new-off.wav`) | **2.98e-08** | **−133.12 dB** | 960 890 of 10 432 512 |
| two renders of the *pristine* build (`default-d1.wav` vs `default-d2.wav`) | 2.24e-08 | −135.61 dB | 411 589 |
| two renders of *this* build (`new-off.wav` vs `fixture-off.wav`) | 2.98e-08 | −133.12 dB | 1 403 648 |

Peak and RMS agree to ten decimal places in every pair
(peak 0.1348993629 = −17.40 dBFS, RMS 0.0379495085 = −28.42 dBFS). The
before/after delta is **inside the band the same binary produces against
itself** — 1 to 2 float32 ULP, ~1/1000 of one 16-bit LSB at this signal level
(quantised to 16 bit, 76 of 10 432 512 samples land on the other side of a
rounding boundary). The synth is not bit-reproducible run to run on this box
(see §4), so an absolute zero cannot be shown for this plugin; what is shown is
that the change is invisible at the float-rounding level, and the *shape* of the
result is unchanged.

**Exact control.** For a project with no ZynAddSubFx instrument, the two builds
are sample-for-sample identical:

```
$ ./build/lmms render data/projects/shorties/sv-Trance-Startup.mmpz -o other-new.wav -f wav -a
$ python3 tests/data/oop-hosting/compare-renders.py other-n1.wav other-new.wav
max |a-b| = 0.000000000000e+00
samples differing: 0 of 755712 (0.000000%)
```

(sha256 of two WAV files is *not* a valid comparator here: libsndfile writes a
`PEAK` chunk whose payload includes the peak sample's *position*, which differs
between runs even when every audio sample is identical. The comparator in
`tests/data/oop-hosting/compare-renders.py` compares samples and reports the
file hashes alongside.)

**The mechanism, observed at program level** — `observe-render.sh` samples
`/proc/<pid>/exe` (never a command-line match: a shell that merely mentions the
name is not a client) while the render runs:

```
$ bash tests/data/oop-hosting/observe-render.sh "$PWD" tests/data/oop-hosting/zyn-separate-process-off.mmp /tmp/off.wav /tmp/off.log "TOGGLE OFF"
[TOGGLE OFF] host pid=1400322 project=…/zyn-separate-process-off.mmp
[TOGGLE OFF] sample 1: no client process
[TOGGLE OFF] sample 2: no client process
[TOGGLE OFF] sample 3: no client process
[TOGGLE OFF] render host exit=0
[TOGGLE OFF] 'Remote plugin crashed' lines: 0
```

### 3.3 Isolation: a dead client cannot take the host down

Same script, the `-on` project (sha256 `439b5a95…22d7`; the only difference from
the `-off` file is that one attribute):

```
[TOGGLE ON] host pid=1404856 project=…/zyn-separate-process-on.mmp
[TOGGLE ON] sample 1: no client process
[TOGGLE ON] sample 2: client pid=1405597 ppid=1404856 (child of the render host)
[TOGGLE ON] sample 3: client pid=1405597 ppid=1404856 (child of the render host)
[TOGGLE ON] render host exit=0
```

The client is a **separate process whose parent is the render host** — `pid`
above, and `/proc/1405597/status` says `Name: RemoteZynAddSub` (the kernel
truncates the name to 15 characters), `PPid: 1404856`. The same thing is
asserted in the committed test with no external tooling:

```
QINFO : …separateProcessChoiceSpawnsAClient() separate process: RemoteZynAddSubFx is a child of pid 1326011
QINFO : …hostSurvivesAndNoticesTheClientExit() client pid 1326184 was killed and the host kept running (pid 1326011)
```

**Killing it.** `kill-loop-experiment.sh` renders the ON project and SIGKILLs
the client as soon as it appears:

```
$ bash tests/data/oop-hosting/kill-loop-experiment.sh "$PWD" tests/data/oop-hosting/zyn-separate-process-on.mmp /tmp/killed.wav /tmp/killed.log
host pid=1413261
killed client pid=1414069 ppid=1413261 (kill #1, seen #1)
clients seen=1 killed=1
render host exit=0
```

and the audio that comes out is what the crash did:

```
$ python3 tests/data/oop-hosting/compare-renders.py fixture-on.wav fixture-killed.wav
a: peak=0.1348993629 (-17.40 dBFS) rms=0.0379495085 (-28.42 dBFS)   <- client alive
b: peak=0.1230406910 (-18.20 dBFS) rms=0.0135870149 (-37.34 dBFS)   <- client killed
max |a-b| = 1.348993629217e-01  (rel. to a peak: 0.00 dB, rel. to a rms: 11.02 dB)
samples differing: 9002685 of 10432512 (86.294509%)
fixture-killed: silent seconds 103 first 15 of 118
```

**This is the sensitivity control as well as the isolation evidence**: the
comparator registers a full-signal-level difference (max delta = the whole
signal, not a rounding artefact), 86% of samples change, and 103 of the 118
seconds of audio are *exactly* silent — because the plugin's audio comes from
the process that was killed. The host finished normally: exit 0, the complete
41 730 180-byte WAV written, and its own log line
`Remote plugin crashed` (`src/core/RemotePlugin.cpp:504-518`).

Two honest caveats about this experiment:

- I killed the client; no plugin was crashed by its own bug. What is proven is
  *a dead client cannot take the host down, the host notices, and that plugin's
  audio stops* — the same mechanism a real plugin crash would trip, but the
  trigger here is external.
- A **single** kill is not always enough to see silence: this instrument
  re-instantiates its client when the audio engine's sample-rate signal fires
  (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:144-145` → `reloadPlugin()`), and a kill
  that lands around project load is therefore replaced by a live client that
  renders the rest normally (measured: one early kill left the output identical
  to a live render within 2.98e-08 — i.e. that kill was undone). The kill loop
  is what pins the effect; the test's `hostSurvivesAndNoticesTheClientExit()`
  pins the detection side on its own.

### 3.4 The choice survives save and reload

Driven through the public entry points a project save and load use, by the new
ctest:

- save: `plugin->saveState(doc, parent)` — `SerializingObject::saveState()`
  calls the plugin's own `saveSettings()`
  (`src/core/SerializingObject.cpp:51-65`) — yields
  `separateprocess="1"`, asserted;
- reload: a **fresh** instrument is instantiated and fed that element through
  `restoreState()` (`src/core/SerializingObject.cpp:69-76`), which calls
  `loadSettings()`; the fresh instance must come up
  `separate-process` with a live client process, asserted.

The load half is additionally proven end-to-end by §3.3: a project file with
the attribute makes a render host the instrument out of process.

**Limit, stated plainly:** there is no way to drive a *project save* headlessly
in this tree — `LuaSong::saveProject()` is declared
(`include/ScriptBindings.h:309`) but not registered by the script engine
(`src/core/ScriptEngine.cpp`), and saving is a GUI action
(`src/gui/MainWindow.cpp:813`). So the save half is proven at the level of the
plugin's own `saveState()`, which is the exact call a GUI save makes, and not by
clicking Save. The writer never changed the file format: the attribute is one
more BoolModel attribute on the element, next to `forwardmidicc`.

---

## 4. What is not proven, and what is not in scope

- **No real plugin crash was survived.** The client was SIGKILLed by the
  experiment. "Separate process, host detects the exit and keeps running" is
  what was measured; "this particular crashing plugin was survived" is not.
- **VST3 and CLAP still run in-process**, as do the native, LADSPA, LV2 and
  SF2/GIG families. They have no client executable, so the toggle cannot apply
  to them yet; the new plugin API has to grow one first.
- **VST2 is out of scope for a toggle** because it has no in-process path: its
  remote process is unconditional (`plugins/VstBase/VstPlugin.cpp:174-181`).
  Nothing here changes VST2 behaviour.
- **The toggle does not change the sound.** The client and the in-process synth
  produce the same samples to within 1 float32 ULP (measured: 2.98e-08 between
  the ON and OFF renders of a project that differs only in this attribute). That
  is the right outcome for compatibility, but it means the toggle cannot be
  advertised as an audio change — only as crash isolation.
- **The ZynAddSubFx synth is not bit-reproducible run to run on this box**, and
  the render comparison is built around that: two renders of the *same* binary
  of the same project differ by 2.2e-08 to 3.0e-08 (−133 to −136 dB). With the
  shipped `Ashore` demo's PADsynth patch the difference is far larger — up to
  0.49 in sample value (RMS within 1%): a decaying transient consistent with
  per-voice randomisation. That is a pre-existing property of the synth, it is
  why the A/B fixture uses Zyn's built-in default patch, and it is worth a
  separate look: a ZynAddSubFx track cannot be re-rendered identically today.
- **The audio-thread cost of hosting out of process was not measured.** The
  round trip per period is the design (§1.2) and the CPU cost of the toggle was
  not quantified beyond one wall-clock figure for a 118 s render (5.76 s). The
  alpha ships the in-process default, so nothing regresses; a user who turns the
  toggle on pays for the process.
- **Platform coverage.** Everything here is `linux-x86_64`, built with the CI's
  `CMAKE_OPTS` plus `-DWANT_QT6=ON`. The other six matrix jobs (macOS ×2,
  MSVC x64, mingw64, windows-arm64, linux-arm64) are not reproduced on this
  box, and the test suite skips on Windows by construction (a Windows test host
  cannot load a plugin module — see `tests/src/plugins/AudioPluginTest.cpp`).
  Gate 2 (coverage) and Gate 5 (mutation) were not run in this session.

---

## 5. Reproduce

```sh
cd <worktree>
export LMMS_PLUGIN_DIR=$PWD/build/plugins

# build + tests (CI's linux-x86_64 steps, one printed deviation)
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4

# the toggle's storage, mode and crash-survival contract
(cd build/tests && QT_QPA_PLATFORM=offscreen ./ZynSeparateProcessTest)

# what runs where: OFF -> no client; ON -> client whose PPid is the render host
bash tests/data/oop-hosting/observe-render.sh "$PWD" tests/data/oop-hosting/zyn-separate-process-off.mmp /tmp/off.wav /tmp/off.log "TOGGLE OFF"
bash tests/data/oop-hosting/observe-render.sh "$PWD" tests/data/oop-hosting/zyn-separate-process-on.mmp  /tmp/on.wav  /tmp/on.log  "TOGGLE ON"

# the isolation experiment (and the sensitivity control)
bash tests/data/oop-hosting/kill-loop-experiment.sh "$PWD" tests/data/oop-hosting/zyn-separate-process-on.mmp /tmp/killed.wav /tmp/killed.log
python3 tests/data/oop-hosting/compare-renders.py /tmp/on.wav /tmp/killed.wav

# the unchanged-default A/B (render the OFF project with the base commit's build
# and with this one; see §3.2 for the numbers)
python3 tests/data/oop-hosting/compare-renders.py base-off.wav this-off.wav
```

Artifact hashes from this session (`sha256sum`, for the report's provenance —
the WAVs themselves are not committed, they are 41 MB each):

| file | sha256 |
|---|---|
| `zyn-separate-process-off.mmp` | `5539f08340b26302e53de686cc780781faa997fa0cb4d83ab14a093a75cf1a11` |
| `zyn-separate-process-on.mmp` | `439b5a95360f19f29cfdd3f21d0398827d8ef76f36ea3c25926f5838121e22d7` |
| pristine-build OFF render (`default-d1.wav`) | `a9ac31a1a70ce8fc71c1ca23bae35b6000b09d7a0b7a81dc922b2892bd1c6a58` |
| this-build OFF render (`new-off.wav`) | `75901b48dc8284f412a0fe4ba759f5f75b7d5b8ae8ee558f01530335ba1ca2df` |
| this-build ON render (`new-on.wav`) | `572e496a903f6dcd67c66bac6d74c475188796b2f217e9723a74ab1244f48247` |
| ON render with the client killed (`loop-killed2.wav`) | `e251c7d338a93361160a51b605ce75f05c5dde94b00baf7339066b350d3f95fc` |
