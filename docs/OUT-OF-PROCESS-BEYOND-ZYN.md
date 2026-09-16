# Out-of-process plugin hosting, beyond ZynAddSubFx: the `oop.*` group

**Feature row 80 of `docs/FEATURE-LIST-0.3.0.md`** ("Out-of-process plugin hosting / crash isolation
beyond ZynAddSubFx"), board card **#670**, lane `030/out-of-process` (worktree `woop`).

**What this is.** The prior art (`post-alpha/oop-hosting`, commit `0e1cdcef6`, documented in
`docs/OOP-HOSTING.md`) made ZynAddSubFx's remote path an opt-in per-instance toggle. Everything about it
was Zyn-specific: the choice lived in one plugin's own code, the fact that only that family *could* be
hosted lived in a paragraph of prose, and the one crash that arrived was logged (`Remote plugin crashed`)
and forgotten. This change makes the property a property of the **build**, adds the **lifecycle record**
nothing had, and makes both **drivable and observable through the control socket**:

| | what it is | where |
|---|---|---|
| the family table | what each plugin family's out-of-process story IS in this build, and the reason where it has none | `include/OutOfProcessHosting.h` + `src/core/OutOfProcessHosting.cpp` |
| the client record | every client executable this session started, its exits, crashes and restarts | `HostTracker`, same files |
| the surface | `oop.get_state`, `oop.list_families`, `oop.set_mode`, `oop.restart`, `oop.reset_crashes` | `src/core/ControlCommandsOutOfProcess{,Edit}.cpp` |
| the A16 rows | two `not_mutating` reads, three `irreversible` writers, each with its reason and fallback | `src/core/ControlReversibilityTableOutOfProcess.cpp` |
| the proof | `OutOfProcessHostTest` (registered ctest) | `tests/src/core/OutOfProcessHostTest.cpp` |

**Where the choice is made is one place.** `ZynAddSubFxInstrument::setHostingMode(bool)` is now
`Q_INVOKABLE` and is what the instrument view's checkbox calls as well, so the interface and the socket
cannot drift apart: `plugins/ZynAddSubFx/ZynAddSubFx.cpp` (the view handler,
`ZynAddSubFxView::separateProcessToggled()`, is three lines on top of it).

---

## 1. The family table: what "beyond ZynAddSubFx" actually means in this build

`oop.list_families` answers, family by family, and the answer is a table rather than a paragraph:

| family (plugin key) | availability | client executable | why |
|---|---|---|---|
| `zynaddsubfx` | `client-available` | `RemoteZynAddSubFx` | the one family in this tree that ships **both** implementations — the in-process synth (`LocalZynAddSubFx`, the default) and the client — so hosting an instance out of process is a per-instance choice |
| `vestige` | `always-separate` | `RemoteVstPlugin` | VST2 has no in-process path here: `VstPlugin::tryLoad()` starts the remote client unconditionally (`plugins/VstBase/VstPlugin.cpp:174-181`) |
| `vsteffect` | `always-separate` | `RemoteVstPlugin` | same, the effect half |
| `clapeffect`, `clapinstrument` | `no-client` | — | the CLAP host module is loaded into **this** process (`plugins/ClapEffect/ClapHost.cpp`); the build ships no CLAP client executable |
| `vst3effect`, `vst3instrument` | `no-client` | — | the VST3 host module runs in this process (`plugins/Vst3Effect/Vst3Host.cpp`); no client executable |
| `ladspaeffect`, `lv2effect`, `lv2instrument` | `no-client` | — | the hosted library is `dlopen(3)`ed into this process; a client would have to implement that host side and there is none |
| `sf2player`, `gigplayer`, `carlarack`, `carlapatchbay`, `carlabase` | `no-client` | — | in-tree modules; no client executable in this build |
| anything else | `no-client` (named) | — | the fallback family **names the key it was asked about**, so a refusal always reads "no client executable for '<key>' in this build" rather than a sentence about a family the caller never mentioned |

`no-client` is a statement about **this build**, not about the format: CLAP and VST3 are in-process
because the host module lives behind a function call, and the exact next step (move the host behind a
client boundary) is named in each family's own `reason`. That is the honest form of the row's title — the
generalization is the *mechanism*, and the count of families that can use it here is **one** (`zynaddsubfx`)
plus the family that always does (`vestige`/`vsteffect`).

**The convention a family implements to be drivable.** A plugin is hosting-capable when it exposes, as
`Q_INVOKABLE` members:

```
QString hostingState() const;          // "in-process" | "separate-process" | "separate-process-exited"
qint64  hostingProcessId() const;      // pid of its client, 0 when none
bool    setHostingMode(bool separate); // choose the mode; returns whether it CHANGED
void    reloadPlugin();                // re-create what is hosted (a fresh client)
```

The control surface asks the meta-object, so a family that implements them is drivable **without** a
change in the control surface — and a family that does not is refused with the missing half named
(`... does not implement setHostingMode, so its hosting cannot be changed from here`). An `oop.*` call on
a device never guesses.

---

## 2. Crash isolation, and what is new about it

**What already existed and is unchanged.** A client process that dies does not take the DAW down:
`RemotePlugin::processFinished()` invalidates the plugin (`src/core/RemotePlugin.cpp`) and
`RemotePlugin::process()` zero-fills that slot's output planes instead of reading a dead peer. The audio
for that plugin stops; the DAW keeps running. Nothing in this change touches that path, and no audio-thread
code reads anything new: the notifications come from `QProcess` signals (the application thread) and the
reads from control handlers (the same thread), serialised by one `QMutex` in `HostTracker`.

**What is new.**

1. **The death is recorded, under the client executable's name.** `RemotePlugin::init()` records a start
   (`noteStarted`), `processFinished()` records the exit (`noteExited`, with the exit code and whether it
   was `QProcess::CrashExit`), and the destructor's own deliberate shutdown is recorded as a shutdown
   (`noteShutdown`) — **not** as a crash, because a mode switch that stops a healthy client is the normal
   way a client goes away and counting it would make ordinary switching look like a crash loop.
   The key is the client **executable**, not the plugin instance: re-instantiating the plugin is exactly
   what a crash loop does, so an instance-keyed count could be cleared by the very reload its crash
   triggers.
2. **A crash loop refuses, typed.** After `maxCrashesPerClient()` = **3** deaths in one session,
   `oop.set_mode` and `oop.restart` REFUSE the out-of-process path for that client executable, quoting the
   count and the last exit code. `oop.reset_crashes` is the only thing that lifts it (and it does not
   erase the exit history — only the crash count). The in-process path is never blocked by it.
3. **A slot reports the crash instead of looking healthy.** A plugin re-instantiated after a crash reports
   a healthy `in-process`/`separate-process`; `oop.get_state` resolves a device's state from four sources
   in a fixed order — a live client pid, then the session's record (refusal, then `client-exited`), then
   the plugin's own answer, then the family table — so the record outranks the plugin's own answer.
4. **No auto-restart.** A slot that lost its client stays silent until a caller asks for it with
   `oop.restart`. Auto-restart is how a crash loop becomes a storm; keeping it out is a decision, and it is
   written down in `docs/KNOWN-LIMITATIONS.md`.

---

## 3. The surface (5 ids)

| id | kind | arguments | answers |
|---|---|---|---|
| `oop.get_state` | read | — | every device chain in the song, each device's resolved hosting (state, client, live pid, what the plugin reports, whether it can be driven, this session's record for its client) + the whole family table + every client record + the bound |
| `oop.list_families` | read | — | the family table on its own |
| `oop.set_mode` | write | `target` (`trk-<n>`/`ch-<n>`), `plugin` (`fx-<n>`/`inst`), `mode` (`in-process`/`separate-process`) | `mode_before`, `mode_after`, `changed`, the resolved hosting; **refused** with a typed reason for a family with no client, a family with no in-process path, a plugin without the convention, or a client in a crash loop |
| `oop.restart` | write | `target`, `plugin` | `state_before`/`state_after`, `process_id_before`/`process_id_after`, `restarted`; refused when there is no client to restart, no reload to deliver, or the client is in a crash loop |
| `oop.reset_crashes` | write | `client` (optional — every client when absent) | what was cleared, the record before and after, the bound |

The addressing is the `plugin.*` convention (`target` + `plugin`), so an agent chains `plugin.list` →
`plugin.load` → `oop.get_state` → `oop.set_mode` on the same ids without learning a second scheme.

---

## 4. Evidence

*(The measurements below are filled in from this lane's build; see `docs/reports/` for the raw logs.)*

- `OutOfProcessHostTest` — registered ctest, `tests/CMakeLists.txt`.
- `oop.get_state` / `oop.list_families` on a live instance.
- the crash loop: hosted out of process through `oop.set_mode`, the client SIGKILLed, counted, restarted
  through `oop.restart`, refused at the bound, and drivable again after `oop.reset_crashes`.

**What is NOT proven, and is not claimed:**

- **No plugin was crashed by its own bug.** The client is SIGKILLed from outside; what is shown is "a dead
  client cannot take the host down, the build notices and counts it, and the out-of-process path refuses
  after the bound" — the mechanism a real crash trips, with an external trigger.
- **The audio of a killed slot is not compared here.** That is `docs/OOP-HOSTING.md`'s render comparison
  and the registered `ZynSeparateProcessTest`.
- **Thirteen of the fifteen classified families have no out-of-process path in this build** (§1). The
  table says so family by family; nothing here hosts CLAP, VST3, LADSPA, LV2, SF2, GIG or a native module
  in a separate process.
- **No platform coverage beyond `linux-x86_64`** (Qt6, the box this lane ran on). The client loop skips on
  Windows by construction (a Windows test host cannot load a plugin module — see
  `tests/src/plugins/AudioPluginTest.cpp`), and the suite's other cases run everywhere.
