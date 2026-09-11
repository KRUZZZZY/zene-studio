# Instrument hosting — what exists, the exact delta, the smallest honest slice

**Status:** research spec, no product code changed. **Branch:** `post-alpha/instrument-hosting`
**Base:** `0c23587d2` (= tag `v0.1.0-alpha`). **Repo:** Zene Studio (LMMS-derived, GPL-2.0-or-later).
**Evidence rule:** every claim below carries a `file:line` in *this tree* or a `file:line` in the
*pinned VST3 SDK source* (read from the SDK's own repository at the pinned commit — see §1.1).
Claims that could not be verified are collected in §5 and marked **UNVERIFIED** rather than softened
into prose.

The question this document answers: **what is the exact delta from what is already in this tree to
"one VST3 instrument on one track: MIDI in, audio out, GUI opens, state saves and reloads with the
project"?** No generic essay on plugin hosting.

---

## 0. Headline verdict

- The VST3 **effect** host is real, complete for effects, and built around a clean, SDK-independent
  seam (`vst3::HostedPlugin`, `plugins/Vst3Effect/Vst3Host.{h,cpp}`). 1,961 lines across 15 files
  (`wc -l plugins/Vst3Effect/*`).
- **MIDI in does not exist anywhere in the VST3 host.** There is no event bus enumeration, no
  `IEventList`, and `ProcessData::inputEvents` is never assigned — `grep -n "inputEvents" plugins/Vst3Effect/`
  returns nothing; the `ProcessData` assembly at `plugins/Vst3Effect/Vst3Host.cpp:573-581` sets only
  audio buses plus `inputParameterChanges`.
- **The plug-in's own editor does not exist anywhere in the tree.** `grep -rn "IPlugView" src/ include/ plugins/`
  returns zero hits. What the VST3 effect "editor" is, is a generated grid of LMMS `Knob` widgets
  built from the parameter list (`plugins/Vst3Effect/Vst3EffectControlDialog.cpp:45-80`). The task's
  phrase "the plugin's editor window is opened" therefore describes work that has **no** precursor in
  this tree, in either the VST3 or the CLAP lane.
- **Much more exists than the gap analysis implies**, and the slice is smaller than "months":
  `AudioPlugin<Instrument, …>` already exists (`include/AudioPlugin.h:134-207`), the browser already
  filters by instrument vs effect (`plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:99-105`), the host
  already detects instruments (`Vst3Host.cpp:244`, `:304`), the SDK's RT-safe `EventList` is already
  compiled into `lmms_vst3_sdk` (`cmake/modules/Vst3Sdk.cmake:92`), the SDK's `setupProcessing →
  setActive → setProcessing` sequence the host already runs is byte-for-byte the SDK reference host's
  sequence, and there are three worked examples of "an LMMS plugin that hosts an instrument"
  in-tree (LV2, Vestige, Carla).
- **The two genuine engineering risks** are (a) the `IPlugView` editor, which needs a foreign-window
  embed plus an `IRunLoop` implementation and has no in-tree analogue for VST3, and (b) *nothing else
  about the audio path* — the event-in work is a known, small, RT-safe shape.
- **On this box the slice cannot be fully proven.** There is no VST3 SDK checkout, no VST3/CLAP
  plug-in artifact in any of the 14 local build trees enumerated by `find` (§1.12). Network
  access to fetch the pinned SDK works (§1.12) and the SDK's own instrument samples are available
  (§3.2), so the *build* is obtainable; the *third-party instrument* leg is not installable here
  without user action.

---

## 1. What exists

### 1.1 Where the SDK comes from, and the pin

**Not vendored, not FetchContent — clone-by-hand into the build tree.** `cmake/modules/Vst3Sdk.cmake`
is the single source of truth:

| Fact | Evidence |
| --- | --- |
| Pin = tag `v3.8.1_build_84` | `cmake/modules/Vst3Sdk.cmake:28` |
| Pin commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` | `cmake/modules/Vst3Sdk.cmake:29` |
| URL `https://github.com/steinbergmedia/vst3sdk.git` | `cmake/modules/Vst3Sdk.cmake:30` |
| Checkout path default `${CMAKE_BINARY_DIR}/vst3sdk` | `cmake/modules/Vst3Sdk.cmake:31-32` |
| Obtained by `git clone --depth 1 --branch v3.8.1_build_84` + `git submodule update --init --depth 1 base cmake pluginterfaces public.sdk` | `cmake/modules/Vst3Sdk.cmake:12-15` |
| No `FetchContent` for VST3 | read of the whole file — the only `FetchContent` in the neighbouring CLAP module is `cmake/modules/ClapHeaders.cmake:33-35`, VST3 has no equivalent |
| Hard fail if the checkout is absent | `cmake/modules/Vst3Sdk.cmake:34-41` |
| Licence gate: `LICENSE.txt` must match `MIT License` | `cmake/modules/Vst3Sdk.cmake:44-49` |
| Gate: `pluginterfaces/vst2.x` must not exist | `cmake/modules/Vst3Sdk.cmake:50-54` |
| Gate: SDK version parsed from the SDK's own `CMakeLists.txt`, must be `>= 3.8` | `cmake/modules/Vst3Sdk.cmake:55-67` |
| `plugins/Vst3Effect/CMakeLists.txt` repeats the pin in a comment and the `WANT_VST3` gate | `plugins/Vst3Effect/CMakeLists.txt:1-18`, default `AUTO` at `:6`, skip-when-absent at `:12-16` |

**Verified against the live remote** (not from memory):

```
$ git ls-remote --tags https://github.com/steinbergmedia/vst3sdk.git | grep 3.8.1
3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96	refs/tags/v3.8.1_build_84
$ git ls-remote --heads https://github.com/steinbergmedia/vst3sdk.git
3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96	refs/heads/master
```

So the pinned tag is *also* currently `master`. **Reading the SDK at `master` today is reading the
pinned commit** — which is how §1.7, §2 and §3.2 were checked.

Sub-module commits behind that pin (from the SDK's own tree at `3cdf9ca5` and its `.gitmodules`):
`base fcf9da0bd27a16f7f03773a3a39822f28f5c8477`, `cmake 054c9143cbb8d47fc4694e473f2ee3b4d951a8f5`,
`pluginterfaces 4f547e8e102b47de4a8b8aaf343c73b700786372`,
`public.sdk 586dc5e6c8012c3e4b01c79389375cbe96bdb1da`, `vstgui4 5db272256172557818b6158cf0bb2c4410bddb25`.
All SDK file citations in this document are at those two commits, fetched raw from
`steinbergmedia/vst3_public_sdk` and `steinbergmedia/vst3_pluginterfaces`.

**The compiled subset.** `cmake/modules/Vst3Sdk.cmake:70-99` lists 21 SDK sources, deliberately
excluding VSTGUI. The comment at `:19-26` states the list is taken from the SDK's own
`cmake/modules/SMTG_VST3_SDK.cmake` plus three extra files. Two of those extras matter here:

- `public.sdk/source/vst/hosting/eventlist.cpp` — **already compiled** (`Vst3Sdk.cmake:92`).
- `public.sdk/source/vst/hosting/plugprovider.cpp` — **already compiled** (`Vst3Sdk.cmake:97`).

Platform split adds `module_linux.cpp` / `module_mac.mm` / `module_win32.cpp` (`:101-119`). The target
`lmms_vst3_sdk` is static, PIC, `cxx_std_17`, includes the **SDK root** as its public include dir, and
links `dl` on Linux (`:123-132`). Because the SDK root is on the include path, a new
`#include "pluginterfaces/gui/iplugview.h"` needs **no build change**.

### 1.2 Module loading

| Step | Evidence |
| --- | --- |
| `VST3::Hosting::Module::create(path, err)` — used to enumerate | `Vst3Host.cpp:230` |
| …and again to load | `Vst3Host.cpp:254` |
| Class enumeration keeps only `kVstAudioEffectClass` | `Vst3Host.cpp:238` |
| …and instruments are **not** lost by that filter, because instruments also register as `kVstAudioEffectClass` | SDK `samples/vst/note_expression_synth/source/factory.cpp:37` (category) vs `:40` (`Vst::PlugType::kInstrumentSynth`); same pattern at `samples/vst/mda-vst3/source/mdafactory.cpp:309,355,401,539` |
| Instrument detection = substring `"Instrument"` of `subCategoriesString()`, case-insensitive | `Vst3Host.cpp:244`, `Vst3Host.cpp:304` |
| Saved class identity is the class **name** string, not the UID/CID | `Vst3Host.cpp:267` (`QString::fromStdString(info.name()) != classId`) |

That last row is a real, pre-existing robustness defect, not a hypothetical: a plug-in that renames
its class between versions stops resolving in saved projects, because the host never stores the CID
(the SDK reports it as `ClassInfo::classID()`). See open question Q12.

### 1.3 Processor / controller instantiation

| Step | Evidence |
| --- | --- |
| Host application object, `PluginContextFactory::instance().setPluginContext(app)` before the first `PlugProvider` | `Vst3Host.cpp:69-77` |
| `PlugProvider(factory, classInfo)` then `initialize()` | `Vst3Host.cpp:279-284` |
| `getComponentPtr()` / `getControllerPtr()` | `Vst3Host.cpp:285-286` |
| `IAudioProcessor` obtained via `FUnknownPtr` from the component | `Vst3Host.cpp:287-288` |
| Single-component (`IEditController` implemented by the processor) detected by `queryInterface` | `Vst3Host.cpp:295-299` |
| `prepare()` = `setupProcessing` → `setActive(true)` → `setProcessing(true)`, tolerating `kNotImplemented` from `setProcessing` (with the SDK-reference justification quoted in the comment) | `Vst3Host.cpp:523-549` |
| `release()` = `setProcessing(false)` → `setActive(false)` | `Vst3Host.cpp:596-613` |
| Everything audio-thread is pre-allocated in `prepare()`; `process()` allocates nothing | `Vst3Host.cpp:551-571`, header contract `Vst3Host.h:57-62` |

**This sequence is the SDK's own reference-host sequence**, verified:

- SDK `samples/vst-hosting/audiohost/source/media/audioclient.cpp:343-349` —
  `processor->setupProcessing(setup)` → `component->setActive(true)` → `processor->setProcessing(true); // != kResultOk`.
- SDK same file `:334-337` — `setProcessing(false)` → `setActive(false)` on teardown.

**There is no instrument-specific setup sequence.** The SDK reference host builds one `ProcessData`
and one activation path for effects and instruments alike; the only instrument-specific work is the
**event bus** (§2, row A/B). This corrects a premise in the task brief (see §1.13, correction 2).

### 1.4 Parameters

| Fact | Evidence |
| --- | --- |
| Enumeration via `IEditController::getParameterInfo`, defaults read with `getParamNormalized` | `Vst3Host.cpp:342-363` |
| Sorted `(id → index)` table, lock-free `std::atomic<float>[]` snapshot | `Vst3Host.cpp:364-375` |
| Minimal `IComponentHandler`: `performEdit` mirrors into the snapshot by binary search; no allocation, no lock | `Vst3Host.cpp:84-135` (esp. `:100-110`) |
| Host→plug-in parameter flow is a per-block diff into `ParameterChanges` | `Vst3Host.cpp:645-658` |
| Plug-in→host GUI refresh is a 100 ms `QTimer` poll | `plugins/Vst3Effect/Vst3EffectControls.cpp:58-97` |
| Value → display string via the plug-in's own formatter | `Vst3Host.cpp:701-712`, wired at `Vst3EffectControls.cpp:53-55` |
| No program/preset enumeration anywhere | `grep -rn "getProgramList\|IUnitInfo\|IProgramListData" plugins/Vst3Effect/` → 0 hits; SDK has `IUnitInfo::getProgramListCount` at `pluginterfaces/vst/ivstunits.h:166`, `getProgramName` at `:175`, `IProgramListData::getProgramData` at `:252` |

### 1.5 State save / restore

| Step | Evidence |
| --- | --- |
| Capture: `IComponent::getState` + `IEditController::getState`, `kNotImplemented` treated as "keeps no state" | `Vst3Host.cpp:448-469` |
| Restore: `IComponent::setState`, then `IEditController::setComponentState` with the *same* stream rewound, then `IEditController::setState`, then re-read every parameter | `Vst3Host.cpp:471-506` |
| `MemoryStream` used on both sides with an explicit `seek(0)` before plug-in reads | `Vst3Host.cpp:137-164` |
| Serialised into the project as base64 `<componentstate>` / `<controllerstate>` | `plugins/Vst3Effect/Vst3EffectControls.cpp:99-113` |
| Restored from those nodes | `plugins/Vst3Effect/Vst3EffectControls.cpp:115-134` |
| Unit test for the round trip (effect) | `tests/src/plugins/Vst3EffectIntegrationTest.cpp:182` (`testStateRoundTripThroughMmp`) |

This is the *effect* path: the nodes live in the effect-chain XML. An instrument's state must instead
be written from `Instrument::saveState` into the `<instrument>` element (§1.8) — the same two
`MemoryStream` blobs, a different parent node.

### 1.6 The editor window — **does not exist**

- `grep -rn "IPlugView" src/ include/ plugins/` → **zero hits**.
- The VST3 effect "GUI" is `Vst3EffectControlDialog`, a `QGridLayout` of `Knob` widgets built from
  `controls->paramModels()`, 4 per row, `setFixedSize(sizeHint())` | `plugins/Vst3Effect/Vst3EffectControlDialog.cpp:45-80`.
- The CLAP effect GUI is the same pattern | `plugins/ClapEffect/ClapEffectControlDialog.cpp` (same file family; `CLAP_EXT_GUI` is not fetched anywhere — `grep -n "CLAP_EXT_GUI" plugins/ClapEffect/ClapHost.cpp` → 0 hits).
- What the SDK requires, for reference (all read at the pinned `pluginterfaces`):
  - `class IPlugView : public FUnknown` | `pluginterfaces/gui/iplugview.h:134`
  - `isPlatformTypeSupported(FIDString)` | `:140`
  - `attached(void* parent, FIDString type)` | `:149`
  - `getSize(ViewRect*)` / `onSize(ViewRect*)` | `:175` / `:179`
  - `setFrame(IPlugFrame*)` | `:185`
  - `IPlugFrame::resizeView(IPlugView*, ViewRect*)` | `:214`
  - Platform strings `kPlatformTypeX11EmbedWindowID` / `kPlatformTypeWaylandSurfaceID` | `:79` / `:86`
  - The documented resize handshake (plug-in → host `resizeView` → host `onSize`) | `:109-123`
- A Linux `IRunLoop` reference implementation ships with the SDK | `samples/vst-hosting/editorhost/source/platform/linux/runloop.h:70` (`class RunLoop`), `:39` (`TimerProcessor`), `:101`.

**In-tree prior art for embedding a foreign window exists, but it is VST2/Vestige, not VST3.** LMMS
has an X11-embed method setting (`ConfigManager::vstEmbedMethod()`,
`src/core/ConfigManager.cpp:216-220`) plus the `qt5-x11embed` sub-module (`.gitmodules`) and
`src/gui/SubWindow.cpp:175-180`, which explicitly documents forwarding show/hide to an attached
child window. That is the closest analogue for the embed half of the editor problem; the
`I...` interface plumbing and the run loop still have to be written.

### 1.7 The CLAP host, for comparison

| Fact | Evidence |
| --- | --- |
| Descriptor type is `Plugin::Type::Effect`; browser is constructed with the same type | `plugins/ClapEffect/ClapEffect.cpp:44-55` |
| Hosted extensions fetched: params, audio-ports, state, latency — and nothing else | `ClapHost.cpp:554-562`; fields at `ClapHost.cpp:122-125` |
| Latency is implemented and exposed | `ClapHost.h:87`, `ClapHost.cpp:328-332` |
| The only occurrence of the word "note" in the whole CLAP plug-in is a parameter-event field | `ClapHost.cpp:766` (`event.note_id = -1`) |
| No note-port extension, no GUI extension | `grep -n "note_ports\|CLAP_EXT_NOTE_PORTS\|CLAP_EXT_GUI" plugins/ClapEffect/ClapHost.cpp` → 0 hits |
| Tests are wired and gated on the headers being present | `tests/CMakeLists.txt:484` (`if(TARGET lmms_clap)`), `:491-522`, test plug-in built from `tests/data/clap-test-plugin/clap-test-gain.c`, `CLAP_TEST_PLUGIN_PATH` define, coverage registration at `:565` |
| CLAP headers are also absent from the build tree by default (`AUTO`), with an **opt-in** fetch | `cmake/modules/ClapHeaders.cmake:23-35`; `WANT_CLAP:STRING=AUTO` in every local `CMakeCache.txt` |

Conclusion: **the CLAP lane gives no head start on instruments, and its hosts has strictly less
capability than the VST3 one** (no MIDI in at all, no latency → no, it has latency; no editor, no
event bus). The VST3 host is the right place to build instruments, and the CLAP lane is a
*second* instrument host to be done later, not a template for the first.

### 1.8 LMMS's own Instrument / InstrumentTrack abstraction

| Fact | Evidence |
| --- | --- |
| `class LMMS_EXPORT Instrument : public Plugin` | `include/Instrument.h:54` |
| `Flag::IsMidiBased = 0x02` — "controlled by MIDI events rather than NotePlayHandles" | `include/Instrument.h:61` |
| `isMidiBased()` | `include/Instrument.h:147-150` |
| `virtual void play(SampleFrame*)` / span entry point | `include/Instrument.h:88` / `:92-95` |
| `virtual void playNote(NotePlayHandle*, SampleFrame*, f_cnt_t)` / span entry | `include/Instrument.h:98` / `:105-108` |
| `virtual bool handleMidiEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t offset = 0)` — default returns `true` | `include/Instrument.h:166-169` |
| `Instrument::instantiate(name, track, key, keyFromDnd)` → `Plugin::instantiateWithKey`, and **`DummyInstrument` if the instantiated plug-in is not an `Instrument`** | `src/core/Instrument.cpp:96-107` (the `dynamic_cast` + fallback is `:103-106`) |
| `class LMMS_EXPORT InstrumentTrack : public Track, public MidiEventProcessor` | `include/InstrumentTrack.h:61` |
| `Instrument* m_instrument` | `include/InstrumentTrack.h:305` |
| `InstrumentTrack::loadInstrument()` | `src/tracks/InstrumentTrack.cpp:1060-1070` |

**The AudioPorts transport already has an instrument specialization.** `include/AudioPlugin.h`:

- `class AudioPlugin<Instrument, settings, AudioPortsT>` — `:134-207`
- constructed as `Instrument{parent, desc, key, flags}` + `m_audioPorts{true, this, …}` + `m_audioPorts.init()` — `:142-151`
- `playImpl(std::span<SampleFrame> inOut) final` → `router.process(bus, …)` → `processImpl(buffers...)` — `:164-190`
- `playNoteImpl(...) final {}` with the comment *"Only MIDI-based instruments are currently supported by AudioPlugin… The `Instrument::playNote()` method is still called for MIDI-based instruments when notes are played, so this method is a no-op."* — `:192-203`

So the effect's `AudioPluginExt<Effect, Vst3EffectAudioPortsSettings>`
(`plugins/Vst3Effect/Vst3Effect.h:48`) has a ready-made instrument analogue: an
`AudioPluginExt<Instrument, Vst3InstrumentAudioPortsSettings>`.

**Serialisation of the instrument itself** — `src/tracks/InstrumentTrack.cpp`:

- save: `<instrument name="<descriptor()->name>">`, then `m_instrument->saveState(doc, i)`, then the
  key appended as a child | `:861-870` (`:864` name, `:865` state, `:867` key, `:869` append)
- load: read the `Key` from the `<key>` element, `Instrument::instantiate(node.attribute("name"), this, &key)`,
  then `m_instrument->restoreState(node.firstChildElement())` | `:957-975`
- the legacy/compat branch (no `<key>` node) instantiates by node name with `keyFromDnd = true` | `:986-999`
- the `Key`/attribute XML format itself | `src/core/Plugin.cpp:276-288` (ctor), `:293+` (`saveXML`)

Consequence: because the key carries `file` + `class` attributes
(`plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:110-111`), the module path and class name survive
save/reload **already today** — the missing half is only the state blobs and the `name` attribute of
a new descriptor.

**A hard architectural constraint the lane must respect:** one shared library exposes exactly one
descriptor, named after the file. `PluginFactory` resolves `<file.baseName()>_plugin_descriptor`
(`src/core/PluginFactory.cpp:179-185`, with the `lib` prefix stripped at `:180-182`) and requires the
`lmms_plugin_main` symbol to exist (`:177`). Therefore a VST3 instrument **cannot** share the
`vst3effect.so` target; it needs its own plugin directory and target, and the host sources must be
compiled into both (or factored into a small static library).

### 1.9 MIDI routing from a track to its instrument

This is the existing machinery an instrument would feed from, and it is complete:

| Step | Evidence |
| --- | --- |
| A clip note creates a `NotePlayHandle`; its ctor emits `MidiNoteOn` through the track with a **sample offset** | `src/core/NotePlayHandle.cpp:235-237` (`offset()` passed both as `TimePos` and as the `f_cnt_t offset`) |
| Note end emits `MidiNoteOff` with `_s` as the offset | `src/core/NotePlayHandle.cpp:396-400` |
| `InstrumentTrack::processOutEvent` routes to the instrument: note-on at `:497`, note-off at `:510`, everything else at `:517`, also mirrored to the MIDI port at `:522` | `src/tracks/InstrumentTrack.cpp:467-522` |
| Master key transposition is applied before delivery | `:475-482` |
| Running-note bookkeeping emits a note-off before a re-triggered note | `:486-500` |
| Externally received MIDI that the track does not itself consume is forwarded to the instrument | `:455-460` (from `processInEvent`, `:322`) |
| The precedent for a MIDI-based instrument doing this: LV2 | `plugins/Lv2Instrument/Lv2Instrument.cpp:165-172` (`handleMidiEvent` → `handleMidiInputEvent`), flags at `:76-82` (`IsSingleStreamed \| IsMidiBased`), and the play handle that produces audio at `:92-94` (`new InstrumentPlayHandle(this, instrumentTrackArg)`) |
| The AudioPorts-side is explicit that the note path is a no-op for MIDI-based instruments | `include/AudioPlugin.h:192-203` |

So: **MIDI in is routed, timed, thread-safe and precedent-backed; what is missing is only the
translation of a delivered `MidiEvent` + offset into a VST3 `Event` + `sampleOffset`.**

### 1.10 Discovery / browser — already instrument-aware

`plugins/Vst3Effect/Vst3SubPluginFeatures.cpp`:

- `collectModules()` walks `ConfigManager::inst()->vstDir()` to depth 4, accepting `*.vst3`
  directories and files, and recursing | `:64-91`, depth guard `:41`
- `listSubPluginKeys()` computes `wantInstrument = m_type == Plugin::Type::Instrument` and filters
  `if (info.isInstrument != wantInstrument) { continue; }` | `:93-115` (the two lines at `:99` and `:105`)
- the key's attributes are `file` and `class` | `:110-111`
- accepted extension for a drag-and-drop / file dialog | `:59-62`
- the effect descriptor passes `Plugin::Type::Effect` | `plugins/Vst3Effect/Vst3Effect.cpp:55`

An instrument descriptor constructed as `new Vst3SubPluginFeatures(Plugin::Type::Instrument)` gets
instrument enumeration **with zero changes to this file**. This is a strong signal the original
author already expected this lane.

Scan directory: `vstDir()` default is `workingDir + "vst/"` (`src/core/ConfigManager.cpp:77`,
sanitised at `:551-559`), and the live config on this box has
`vstdir="/home/kruzzzzy/Documents/lmms/vst/"` (read from `~/.lmmsrc.xml`, line 4). That directory
**does not exist** on this box (§1.12).

### 1.11 Test state: the VST3 test sources are orphaned

- `tests/CMakeLists.txt` contains **zero** occurrences of `vst3` (case-insensitive:
  `grep -c -i vst3 tests/CMakeLists.txt` → `0`).
- Therefore `tests/src/plugins/Vst3HostTest.cpp`, `Vst3BusMapTest.cpp` and
  `Vst3EffectIntegrationTest.cpp` are **never compiled and never run** by any CMake configuration.
  They appear only in the gate's source inventory (`tests/all-sources.txt`).
- Their plug-in fixture is supplied by a CMake define that nothing in the tree sets:
  `VST3_TEST_PLUGIN_PATH` (`Vst3HostTest.cpp:34-36`, `Vst3EffectIntegrationTest.cpp:44-46`) and they
  `QSKIP` when it is empty or the bundle is missing (`Vst3HostTest.cpp:89-94`).
- By contrast the CLAP tests are wired and gated (`tests/CMakeLists.txt:484-522`, `:565`).

So the first slice's test work starts by *wiring*, not by inventing: there is a headless integration
harness shape to copy (`tests/src/plugins/Vst3EffectIntegrationTest.cpp`, incl. an mmp round-trip
test at `:182` and a before/after WAV render at `:236`) and a CMake gating pattern to copy
(`tests/CMakeLists.txt:484-522`).

### 1.12 What this box can and cannot host today

All of the following were run on the box hosting this branch:

| Fact | Command | Result |
| --- | --- | --- |
| No VST3 SDK checkout anywhere | `find / -xdev -maxdepth 7 -name ivstcomponent.h`; `find /home/kruzzzzy -maxdepth 8 -type d -name 'vst3sdk*'` | empty / empty |
| No VST3 or CLAP plug-in artifact in any local build | `find <parent> -maxdepth 6 \( -name '*vst3effect*' -o -name '*clapeffect*' \) -name '*.so'` | empty |
| Every local build skips VST3 | `grep -h WANT_VST3 */build*/CMakeCache.txt \| sort -u` | `WANT_VST3:STRING=AUTO`, paths pointing at non-existent `<build>/vst3sdk` |
| CLAP is skipped too | `find <parent> -maxdepth 5 -path '*clap/include/clap/clap.h'` | empty |
| No plugin is installed | `ls ~/.vst3 ~/.vst ~/.lv2 ~/.clap` | all four: no such file or directory |
| The VST3 scan dir does not exist | `ls /home/kruzzzzy/Documents/lmms/vst/` | no such file or directory |
| No sudo | `sudo -n true` | `sudo: a password is required` |
| Network to GitHub works | `git ls-remote --tags https://github.com/steinbergmedia/vst3sdk.git` | the pin resolves (§1.1) |
| Toolchain | `which cmake g++ pkg-config` | all present |
| X11 dev libs | `pkg-config --exists x11 / xcb` | yes / yes |
| freetype by pkg-config | `pkg-config --exists freetype2` | **no** |
| GTK3 | `pkg-config --exists gtk+-3.0` | **no** |
| Headless GUI possible | `which Xvfb xvfb-run xdotool`; `ls /tmp/.X11-unix` | all present; `X0` socket exists; `DISPLAY` unset in this shell |
| Instrument-hosting plug-ins *do* build here | `ls <zene-w0>/build/plugins/*.so` | `liblv2instrument.so`, `libvestige.so`, `libcarlabase.so`, `libcarlarack.so`, `libcarlapatchbay.so`, `libgigplayer.so`, `libsf2player.so`, `libmonstro.so`, `libtripleoscillator.so`, `libaudiofileprocessor.so` |
| LV2/Carla are enabled in those caches | `grep -h 'WANT_LV2\|WANT_CARLA' <zene-w0>/build/CMakeCache.txt` | `WANT_LV2:BOOL=ON`, `WANT_CARLA:BOOL=ON` (a sibling cache has `WANT_LV2:BOOL=OFF`, so LV2 is optional) |

**Consequence for sizing:** the audio/MIDI/state slice can be *written* here and *compiled* only
after the SDK is fetched; the "one real third-party VST3 instrument" leg cannot be done on this box
at all without the user obtaining a binary (§3.4).

### 1.13 Corrections to premises in the task brief

1. **`IInstrument` does not exist in VST3.** `pluginterfaces/vst/` at the pinned commit contains
   `ivstevents.h`, `ivstmidicontrollers.h`, `ivstmidilearn.h`, `ivstmidimapping2.h`,
   `ivstunits.h`, … and **no instrument interface**; a path search for `instrument` returns nothing.
   The instrument marker is a *sub-category string*, `Vst::PlugType::kInstrumentDrum|Sampler|Synth|SynthSampler`
   (`pluginterfaces/vst/ivstaudioprocessor.h:75-80`), carried in the class registration next to
   `kVstAudioEffectClass`. Nothing in the host needs to `queryInterface` for an instrument; the host
   already reads the marker correctly (`Vst3Host.cpp:244`, `:304`). Any design step justified by
   "the SDK requires `queryInterface(IInstrument)`" should be dropped.
2. **There is no separate `setupProcessing` ritual for instruments.** The SDK reference host runs one
   sequence for both kinds (`audioclient.cpp:343-349`), and LMMS already matches it
   (`Vst3Host.cpp:523-549`). What an instrument needs that an effect does not is *event* buses and
   `ProcessData::inputEvents` — not a different processing setup.
3. **"The DAW cannot load an instrument at all" is too strong.** In-tree and building on this box:
   LV2 instruments (`plugins/Lv2Instrument/`, `liblv2instrument.so` present in 7 worktrees), VST2
   instruments through Vestige (`plugins/Vestige/`, `libvestige.so` present, `WANT_CARLA=ON`), and
   Carla-hosted instruments (`libcarlabase.so` present). The product's own wording is precise and
   should be the one carried forward: *"VST3 and CLAP are effects only. No instrument in either
   format can load. VST2 instruments still work through Vestige."* —
   `docs/KNOWN-LIMITATIONS.md:59-60`; also `docs/RELEASE-NOTES-v0.1.0-alpha.md:122,140` and
   `docs/STATUS.md:89,191`. The accurate framing of this lane is therefore **"first-party in-process
   VST3 instrument hosting"**, not "instrument hosting".
4. **`EventList` is already compiled and is RT-safe if pre-sized.** `setMaxSize(n)` allocates once
   (`new Event[n]`, SDK `eventlist.cpp:40-51`); `addEvent` is a `memcpy` into that array and fails
   (rather than allocating) when full (`:65-74`). No new SDK source file is needed for the event
   path; only a `setMaxSize()` in `prepare()` and an `inputEvents` assignment.

---

## 2. The delta — per-area decision table

Legend: **E** = exists today (cite), **M** = missing, **C** = the change to make, **R** = risk.
Row letters are referenced from §4 (open questions).

| # | Area | Exists | Missing | Change (files) | Risk |
| --- | --- | --- | --- | --- | --- |
| **A** | Event **input bus** on the component | Audio bus enumeration only (`Vst3Host.cpp:307-339`) | `getBusCount(kEvent, kInput)` / `getBusInfo(kEvent, kInput, i, …)`, and an explicit record of the event bus index | `Vst3Host.cpp` `load()`: add an event-bus loop beside `:310-337`; add a field to `struct Impl` (`:168-207`). SDK reference: `audioclient.cpp:202-217` | Low. Some instruments declare **multiple** event inputs (`addEventInput(…, 1)` count argument); the slice must pick a documented policy (first active bus) and say so |
| **B** | `ProcessData::inputEvents` + sample-accurate timing | `ProcessData` has audio buses and `inputParameterChanges` only (`Vst3Host.cpp:573-581`) | `IEventList` wired to `processData.inputEvents`; a per-block, RT-safe, ordered event queue; `Event::sampleOffset` population | `Vst3Host.cpp`: include + `EventList` member + `setMaxSize()` in `prepare()` (`:551-571`) + `clear()`/`addEvent()` in `process()` (`:645-698`). SDK: `eventlist.h:28-39`, `audioclient.cpp:187-191` | Medium — the free function in the slice is *ordering*: LMMS delivers note events with an offset relative to the period (`NotePlayHandle.cpp:237`, `:400`) |
| **C** | Instrument vs effect detection on load | Already correct: `isInstrument` field (`Vst3Host.h:48`), computed from sub-categories (`Vst3Host.cpp:244`, `:304`), accessor (`Vst3Host.h:81`, `Vst3Host.cpp:403-406`), test asserts an effect is not an instrument (`Vst3HostTest.cpp:106`) | A policy for what happens when the *user picks an effect* in the instrument browser or vice versa | One guard in the new instrument plug-in's ctor: if `!plugin.isInstrument()`, warn and stay silent (`plugins/Vst3Instrument/Vst3Instrument.cpp`), mirroring the existing failure shape at `Vst3Effect.cpp:67-71` | Low |
| **D** | LMMS-side concrete class | `AudioPlugin<Instrument,…>` (`AudioPlugin.h:134-207`), `InstrumentPlayHandle` precedent (`Lv2Instrument.cpp:92-94`), `AudioPluginExt<Effect,…>` shape to mirror (`Vst3Effect.h:48`), MIDI delivery contract (`Instrument.h:166`) | The class itself | **New** `plugins/Vst3Instrument/Vst3Instrument.{h,cpp}` (~equivalents of `Vst3Effect.h:1-84` + `Vst3Effect.cpp:1-150`, i.e. ~234 lines), **new** `plugins/Vst3Instrument/CMakeLists.txt` (mirror `plugins/Vst3Effect/CMakeLists.txt:1-45`) | Low. Must decide the `Instrument::Flags` (`IsSingleStreamed \| IsMidiBased`, per `Lv2Instrument.cpp:78`) and must add the instrument to `cmake/modules/PluginList.cmake:23-84` (the list ends `…Vst3Effect\n\tVestige…`, so the new dir slots alphabetically) |
| **E** | Preset / state save + restore **in the project file** | `HostedPlugin::saveState/loadState` (`Vst3Host.cpp:448-506`); the effect's base64 node writer (`Vst3EffectControls.cpp:99-134`); the instrument XML slot (`InstrumentTrack.cpp:861-870`, `:957-975`) | An `Instrument`-side `saveState/loadSettings` that writes `<componentstate>`/`<controllerstate>` under `<instrument>` | **New** controls class mirroring `Vst3EffectControls.cpp:99-134`; nothing in the host changes | Low. Order matters: the host must be able to restore state *after* `setActive(true)` if a plug-in requires it — verify per plug-in (Q7) |
| **F** | Editor window (`IPlugView`) and resizing | Nothing in-tree (`grep IPlugView` → 0). Param-grid widget only (`Vst3EffectControlDialog.cpp:45-80`). VST2/X11 embed prior art exists (`ConfigManager.cpp:216-220`, `SubWindow.cpp:175-180`, `.gitmodules` `qt5-x11embed`) | `IEditController::createView(kEditorView)`, `isPlatformTypeSupported("X11EmbedWindowID")`, `attached(windowId, type)`, an `IPlugFrame` implementation, `resizeView`→`onSize` handshake, an `IRunLoop` for Linux, and a Qt host widget | **New** `Vst3EditorWindow.{h,cpp}` + `Vst3PlugFrame`; touch `Vst3Host.{h,cpp}` to expose the controller's view and to provide `IPlugFrame`. SDK refs: `iplugview.h:134,140,149,175,179,185,214`, run loop `runloop.h:70` | **Highest.** Needs a display; Wayland/X11 duality (`iplugview.h:79,86`); main-thread `attached()`; DPI/content-scale; and it is the only area with no in-tree analogue |
| **G** | MIDI translation: LMMS `MidiEvent` → VST3 `Event` | LMMS side complete and timed (`NotePlayHandle.cpp:235-237`, `:396-400`; `InstrumentTrack.cpp:467-522`); SDK reference function exists (`samples/vst-hosting/audiohost/source/media/miditovst.h:46` `midiToEvent(status, channel, data0, data1) -> OptionalEvent`, note-on at `:63-65`, note-off at `:54-56`, both with `noteId = -1`; poly-pressure `:72-75`) | The mapping itself, in the host | `Vst3Host.{h,cpp}`: a `pushMidiEvent(...)` entry point; ~60-120 lines | Low–Medium. Decide: hand-roll the mapping (small, no licence question — the SDK sample is MIT but is a *sample*, i.e. `Vst3Sdk.cmake` would need new sources) — recommended — vs. adding `miditovst.h` + `miditovst.cpp` to `LMMS_VST3_SDK_SOURCES` |
| **H** | Latency reporting into the existing PDC path | `Effect::latencyFrames()` virtual returning 0 (`include/Effect.h:173-176`); the chain sums it (`src/core/EffectChain.cpp:63`), caches it (`include/EffectChain.h:85-87,106-107`), and the mixer consumes it (`src/core/Mixer.cpp:1250`, `:1284`, `:1339-1345`); `LatencyCompensation` exists (`include/LatencyCompensation.h:53-100`) | (a) `Vst3Effect` **does not override `latencyFrames()` at all** → `getLatencySamples()` is never called, so VST3 effects report 0 today; (b) an instrument is not an `Effect` and is not a member of the track's `EffectChain`, so there is no path for instrument latency to reach the PDC graph | Add `IAudioProcessor::getLatencySamples()` to the host and an override in both plug-ins. The instrument case needs a decision: treat the track's instrument as a latency contributor at the track→mixer edge (`Mixer.cpp:1250` reads `channel->m_fxChain.latencyFrames()`) or accept `MaxFrames`-capped error | Medium. Clamping already exists and warns (`src/core/EffectChain.cpp:68-80`); `LatencyCompensation::MaxFrames = 16384` (`LatencyCompensation.h:57`) |
| **I** | Multi-out audio | `BusLayout` already models N output buses with per-bus channel counts (`plugins/Vst3Effect/Vst3BusMap.h:64-80`), and the host partitions planar channels per bus (`Vst3Host.cpp:678-691`) | Routing bus 2..N to separate LMMS mixer channels | Nothing in the slice | **Declare it OUT of the smallest version.** Reason: it depends on the unmerged multi-channel work (program context: PR #7459 is open, not merged) and on `AudioPortsSettings` allowing N outputs. `plugins/Vst3Effect/Vst3Effect.cpp:77-81` already forces the channel count to `max(2, plugin channels)` — an instrument with e.g. 16 outs would be summed to 2. That is an *acceptable, documented* limitation for slice 1 |
| **J** | Program / preset management (`IUnitInfo`, `IProgramListData`) | Nothing (`grep` → 0 hits in `plugins/Vst3Effect/`) | Program enumeration and selection | **Out of the smallest version.** Presets arrive via the plug-in's own state blob (§E), which is what save/reload needs | Low for slice 1; SDK refs `ivstunits.h:166,175,252` |
| **K** | Registration / build wiring | The `vst3effect` target (`plugins/Vst3Effect/CMakeLists.txt:20-45`), the plugin list (`cmake/modules/PluginList.cmake:23-84`), the one-descriptor-per-library rule (`src/core/PluginFactory.cpp:177-185`) | A `vst3instrument` target + descriptor symbol + plugin-list entry | **New** dir + `CMakeLists.txt`; edit `cmake/modules/PluginList.cmake`; decide shared vs duplicated host sources | Low, but must not try to add a second descriptor to `vst3effect.so` |
| **L** | Tests | A headless mmp-round-trip + WAV-render harness shape (`Vst3EffectIntegrationTest.cpp:182`, `:236`), a CMake gating pattern (`tests/CMakeLists.txt:484-522`), an SDK-sample fixture define (`Vst3HostTest.cpp:34-36,89-94`), a precedent for building a purpose-made test plug-in from pinned headers (`tests/data/clap-test-plugin/clap-test-gain.c` + its `CMakeLists.txt`) | The VST3 tests are **not wired at all** (`grep -c -i vst3 tests/CMakeLists.txt` → 0); no instrument fixture exists | Wire the existing three VST3 tests under a `if(TARGET lmms_vst3_sdk)`-style guard; add a VST3 **instrument** fixture | Low–Medium: wiring three files that have never compiled will surface rot (they were written against an earlier tree) |

---

## 3. The smallest honest slice

### 3.1 Definition

> **One VST3 instrument, on one instrument track, playing one MIDI clip: MIDI in through the track's
> existing MIDI path, audio out into the track's audio output, the plug-in's parameter state saving
> into the `.mmp` and reloading bit-identically, and the plug-in's own editor window opening.**

Scope decisions, stated plainly:

- **Multi-out: OUT** (row I). Output buses beyond the first active one are summed into the first, as
  LMMS already does for a single stereo pair (`Vst3Host.cpp:678-691`).
- **Program/preset lists (`IUnitInfo`): OUT** (row J). State round-trip covers "reload my project".
- **The plug-in's editor: OUT of slice 1 if — and only if — the lane is prepared to ship an
  instrument with a parameter-grid window.** The brief names "GUI opens" as an acceptance criterion,
  so the honest reading is that **slice 1 must include it**; §3.3 therefore sizes it as a required
  item and marks it as the schedule risk, not as optional.
- **CLAP instruments: OUT.** The CLAP host has no event path at all (§1.7).

### 3.2 Where the test instrument comes from

The machine has no instrument and no sudo (§1.12). Three sources, in order of honesty:

| # | Source | Obtainable on this box? | Licence | Evidence |
| --- | --- | --- | --- | --- |
| 1 | **A purpose-built minimal VST3 instrument built from the pinned SDK**, mirroring the CLAP lane's `tests/data/clap-test-plugin/clap-test-gain.c`. Deterministic, in-tree, exercises exactly the contract | **Yes** — needs only the pinned SDK checkout, which the network can fetch | MIT (same SDK) | Precedent: `tests/data/clap-test-plugin/CMakeLists.txt` + `clap-test-gain.c`, wired at `tests/CMakeLists.txt:489` |
| 2 | **The SDK's own instrument samples**, which exist at the pinned commit: `samples/vst/note_expression_synth` (a full polyphonic synth with a VSTGUI editor; registers `kVstAudioEffectClass` + `Vst::PlugType::kInstrumentSynth` and declares its event input with `addEventInput(STR16("Event Input"), 1)`), and `samples/vst/mda-vst3` whose factory registers four `kInstrumentSynth` classes — `mdaPiano`, `mdaEPiano`, `mdaJX10`, `mdaDX10` (`mdafactory.cpp:309,355,401,539`) | Probably — but building the samples pulls in VSTGUI, and `freetype2` is absent from this box's `pkg-config` (§1.12) | MIT for the SDK; **the `mda-*` sources carry their own upstream pedigree — licence not verified here, see §5** | SDK tree at `public.sdk@586dc5e6`: `samples/vst/note_expression_synth/source/factory.cpp:34-40`, `note_expression_synth_processor.cpp:62`; `samples/vst/mda-vst3/source/mdafactory.cpp:309,355,401,539` |
| 3 | **A real free/commercial VST3 instrument** (e.g. Surge XT, Vital, Dexed) dropped into `~/Documents/lmms/vst/` | **No** — nothing is installed, nothing can be installed without the user downloading it; the target directory does not exist yet (§1.12) | varies | scan dir `src/core/ConfigManager.cpp:77`, live value `vstdir="/home/kruzzzzy/Documents/lmms/vst/"` in `~/.lmmsrc.xml:4` |

The `.vst3` scan directory must exist and needs no sudo to create
(`~/Documents/lmms/vst/`, from `Vst3SubPluginFeatures.cpp:97` + `ConfigManager.cpp:77,551-559`).

### 3.3 Size, in files and lines

The estimates below are anchored on measured line counts of the effect analogue
(`wc -l plugins/Vst3Effect/*`), not on intuition:

```
Vst3Host.cpp 714  Vst3Host.h 123                       (host, reusable)
Vst3BusMap.cpp 62  Vst3BusMap.h 90                     (bus flattening, reusable)
Vst3ParamDescriptor.h 59  Vst3Parameter.cpp 51  Vst3Parameter.h 74   (param plumbing, reusable)
Vst3SubPluginFeatures.cpp 133  Vst3SubPluginFeatures.h 59            (browser, reusable — §1.10)
Vst3Effect.cpp 150  Vst3Effect.h 84                    (the 234-line shell to mirror as Instrument)
Vst3EffectControls.cpp 141  Vst3EffectControls.h 88    (the 229-line controls/state to mirror)
Vst3EffectControlDialog.cpp 82  Vst3EffectControlDialog.h 51  (the 133-line window to mirror)
CMakeLists.txt 45                                      (to mirror)
                                                        total 1961
```

| Item | New / touched | Lines (**estimate**) |
| --- | --- | --- |
| `Vst3Host.{h,cpp}`: event bus enumeration, `EventList` in `prepare()`, `inputEvents`, `pushMidiEvent`, MIDI→`Event` mapping, `getLatencySamples()` | touch 2 files | **+150 … +250** |
| `plugins/Vst3Instrument/Vst3Instrument.{h,cpp}` — mirror of `Vst3Effect.{h,cpp}` (234 lines) with the `Instrument` base and the MIDI-drain | 2 new files | **+230 … +290** |
| `plugins/Vst3Instrument/Vst3InstrumentControls.{h,cpp}` — mirror of `Vst3EffectControls.{h,cpp}` (229 lines) | 2 new files | **+210 … +250** |
| `plugins/Vst3Instrument/Vst3InstrumentControlDialog.{h,cpp}` — mirror of the param-grid window (133 lines), used until the real editor lands | 2 new files | **+130 … +150** |
| `plugins/Vst3Instrument/CMakeLists.txt` | 1 new file | **+45** |
| `cmake/modules/PluginList.cmake` — add the directory | touch 1 file | **+1** |
| `Vst3SubPluginFeatures`, `Vst3BusMap`, `Vst3Parameter`, `Vst3ParamDescriptor` | **reused unchanged** | **0** |
| **Subtotal, MIDI in + audio out + state + param grid** | 7 new + 3 touched | **≈ +770 … +990** |
| **Editor**: `Vst3EditorWindow`/`Vst3PlugFrame`/run loop, Qt host widget, resize handshake | 2–3 new files + host touchpoints | **≈ +300 … +600 (estimate, low confidence — no in-tree analogue to measure)** |
| **Test fixture**: minimal VST3 instrument plug-in + CMake, and wiring the three orphaned VST3 tests | 3–4 new + 1 touched | **≈ +250 … +400** |

Honest total: **roughly 1,300–2,000 net new lines across ~12 new files and ~4 touched files**, of
which the editor is the only part whose size is a guess. The comparison that matters: the whole
existing VST3 *effect* plug-in (host included) is 1,961 lines. This is a lane of the same order, not
a re-architecture — which is why "months of work" in the gap analysis is wrong if it assumes a
green-field host, and right if it assumes VST3 instrument compatibility work in the wild
(`docs/STATUS.md:132`).

### 3.4 The acceptance test — and what cannot be proven on this box

**Acceptance test (the executable form):**

1. `tests/data/vst3-test-instrument/` — a minimal VST3 instrument built from the pinned SDK
   (roundtrip-simple: `IComponent` with one `kEvent` input bus, one `kAudio` output bus,
   `IAudioProcessor`, an `IEditController` with one parameter; the renderer synthesises a fixed
   sample value for any active note so output is assertable without DSP coincidence). Gated like the
   CLAP fixture (`tests/CMakeLists.txt:484-489`).
2. `tests/src/plugins/Vst3InstrumentTest.cpp` (host level): `load()` the fixture;
   `isInstrument() == true`; `busLayout()` has **1 audio input-count 0-or-equal and ≥1 output**; push
   a note-on at `sampleOffset = 0` and a note-off at `sampleOffset = N/2`; call `process()`; assert
   the first half of the block is non-silent and the second half is silent (the sample-accurate
   proof); assert a note pushed for the *next* block does not leak backwards.
3. `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` (product level), modelled on
   `Vst3EffectIntegrationTest.cpp:182/:236`: build a Song with one `InstrumentTrack` carrying the
   new plug-in, one `MidiClip` with a note; render to WAV; assert RMS over the note window is above
   the silence floor and that the pre-note window is silent (the "MIDI in → audio out" proof);
   `saveState` → `srcdoc`; `loadSettings` into a **fresh** plug-in instance; assert the second render
   is byte-identical to the first (the state-round-trip proof), reusing the existing mmp helper shape.
4. Editor: under `xvfb-run`, open the track window, assert the plug-in's view reports a supported
   platform type, is `attached()`, and that its `getSize()` rect matches the host widget rect after
   the `resizeView` → `onSize` handshake. `Xvfb` and `xdotool` are present (§1.12), so this is
   mechanically possible headlessly — but **the fixture plug-in must itself provide an
   `IPlugView`**, which doubles the fixture's cost.
5. Real-plug-in leg (see below): repeat 3 with the user-supplied instrument of §3.2 row 3.

**What can be proven on this box:**

- Everything in 1–2 (host-level, headless, no display) **once the pinned SDK is fetched** — the
  fetch is verified reachable (§1.1), and compilation of `lmms_vst3_sdk` on this box is
  **UNVERIFIED** (§5) because no build may be run while five builds are in flight.
- Item 3, likewise.
- Item 4 under `Xvfb` (`/tmp/.X11-unix/X0` exists, `xvfb-run` present).

**What cannot be proven on this box:**

- **Any real third-party VST3 instrument.** None is installed, none is installable without sudo
  (§1.12), and the alpha's scan directory does not exist. Naming a "specific free instrument and how
  it will be obtained" is therefore, honestly, a *plan* and not a verified fact: the lane must have
  the user drop a `.vst3` bundle into `~/Documents/lmms/vst/` (created without sudo), then re-run
  acceptance item 3 against it. Candidates are not asserted here — see §5.
- **The SDK's own sample instruments as a fixture**, because building them appears to need
  `freetype2` (VSTGUI) and `freetype2` is absent from `pkg-config` on this box; whether the mda/note
  expression samples build without it is **UNVERIFIED** (§5). This is precisely why fixture 1
  (purpose-built, no VSTGUI) is the primary acceptance vehicle.
- **Multi-out, Wayland editor embedding, macOS/Windows editor paths, and the "matrix of which real
  plug-ins load"** (`docs/STATUS.md:132`) — all out of slice 1 by construction.

---

## 4. Open questions an implementation lane must answer before editing

Each question names the file:line that will have to change, where it is discoverable.

**Q1 — Bus policy for a plug-in with several event inputs.** Which event input bus does the slice
drive, and what happens to the others? The SDK sample declares one input with a count argument
(`note_expression_synth_processor.cpp:62`), but `getBusCount(kEvent, kInput)` is N-valued
(SDK `audioclient.cpp:42`, `:202-206`). *Files:* `Vst3Host.cpp:307-339` (the audio-bus loop to
parallel), `Vst3Host.cpp:168-207` (`struct Impl`).

**Q2 — Who owns the event queue, and what is the exact ordering contract?** LMMS may deliver MIDI
from the GUI/note thread (`InstrumentTrack.cpp:497`, `Lv2Instrument.cpp:168-170` says "can be called
from GUI threads while the plugin is running"), while `process()` runs on the audio thread.
Ring buffer, or a pre-sized double buffer swapped per block? What is the cap on events per block, and
what happens on overflow (`EventList::addEvent` returns `kResultFalse` when full —
SDK `eventlist.cpp:65-74`)? *Files:* `Vst3Host.h:57-62` (the threading contract is already written
and must be extended, not contradicted), `Vst3Host.cpp:551-571`, `Vst3Host.cpp:645-698`.

**Q3 — Sample-offset frame of reference.** LMMS delivers `f_cnt_t offset` in the note handle's own
frames (`NotePlayHandle.cpp:235-237` passes `offset()`, `:396-400` passes `_s`), while VST3 wants
`Event::sampleOffset` = "sample frames related to the current block start sample position"
(SDK `pluginterfaces/vst/ivstevents.h:145`). Confirm the two are the same origin for an `InstrumentPlayHandle`
driven track. *Files:* `Vst3Host.cpp:639-699`, `include/AudioPlugin.h:164-190`.

**Q4 — Which `Instrument::Flags`, and does the note path need suppressing?**
`IsSingleStreamed | IsMidiBased` is the LV2 precedent (`Lv2Instrument.cpp:78`), and the AudioPorts
instrument specialization explicitly documents that `playNote` is a no-op yet is still called
(`AudioPlugin.h:192-203`). Confirm `IsMidiBased` is enough to stop double-triggering, given
`InstrumentTrack::playNote` → `m_instrument->playNote` at `InstrumentTrack.cpp:584`.

**Q5 — Does the instrument need an audio *input*, and what does the host do when the plug-in has
none?** `Vst3Effect.cpp:77-81` forces at least 1 input and ≥2 channels; the host correctly substitutes
a silence buffer when the track provides fewer inputs than the plug-in wants
(`Vst3Host.cpp:660-676`). Decide whether the instrument's `AudioPortsSettings` keeps
`DynamicChannelCount` on the input side at all, given `AudioPlugin<Instrument>`'s single-bus
`playImpl` (`AudioPlugin.h:164-190`). *Files:* new plug-in header, `Vst3BusMap.h:57-80`.

**Q6 — When is state restored relative to activation?** The effect path restores in
`loadSettings` after `prepare()`/`setActive(true)` have already run
(`Vst3EffectControls.cpp:115-134` vs `Vst3Effect.cpp:83-86`), and `loadState` writes into a
`MemoryStream` and calls `setState` (`Vst3Host.cpp:471-506`). Instruments that build wavetables from
`setState` may require `setActive(false)` first. Decide and write the order down; then test it with
the fixture. *Files:* `Vst3Host.cpp:448-506`, new controls class.

**Q7 — What is written into the `<instrument>` node, and what is the compatibility promise?**
`InstrumentTrack.cpp:865` takes `m_instrument->saveState(doc, i)` and appends the key at `:867`; the
loader re-instantiates by `node.attribute("name")` at `:971`. Confirm the node names
(`componentstate` / `controllerstate`, as `Vst3EffectControls.cpp:111-112`) and whether the descriptor
`name` must be frozen forever (`vst3instrument`) for forward compatibility — the one-descriptor rule
is at `src/core/PluginFactory.cpp:179-185`.

**Q8 — Shared host code or duplicated sources?** `plugins/Vst3Effect/CMakeLists.txt:20-28` lists
`Vst3Host.cpp` as a plug-in source; `tests/CMakeLists.txt:528-540` shows the codebase's preference
for compiling a plug-in's sources directly into a test. Options: (a) new target lists
`../Vst3Effect/Vst3Host.cpp` too, (b) factor `lmms_vst3_host` static lib, (c) move the host to
`src/`. Decide before writing, because it determines the diff shape. *Files:*
`cmake/modules/BuildPlugin.cmake:8-90`, `plugins/Vst3Effect/CMakeLists.txt:45`.

**Q9 — Editor: X11 embed, Wayland, or both — and who owns the run loop?** The SDK offers
`kPlatformTypeX11EmbedWindowID` and `kPlatformTypeWaylandSurfaceID`
(`pluginterfaces/gui/iplugview.h:79,86`) and requires `IPlugFrame::resizeView` → `onSize` (`:214`,
`:109-123`). Linux plug-ins that use timers or file descriptors need `IRunLoop`; the SDK sample's
implementation is the only in-tree-free reference (`samples/vst-hosting/editorhost/source/platform/linux/runloop.h:70`).
Does the lane implement a minimal `IRunLoop` (registerEventHandler + registerTimer) on Qt's event
loop, or does it depend on plug-ins not needing one? *Files:* new editor files; `Vst3Host.cpp:69-77`
(the host context object, which is where `IPlugFrame` support would be exposed).

**Q10 — Where does instrument latency enter the PDC graph?** The chain sums `Effect::latencyFrames()`
(`src/core/EffectChain.cpp:63`) and the mixer reads `channel->m_fxChain.latencyFrames()`
(`src/core/Mixer.cpp:1250`); an instrument is not an `Effect`. Is the instrument's latency added as a
constant on the track's outgoing edge inside `updateLatencyCompensation()`
(`src/core/Mixer.cpp:1257-1345`), or is the track's instrument treated as a pseudo-effect at the head
of the chain? Note also that today **no VST3 plug-in reports latency at all** — `Vst3Effect` has no
`latencyFrames()` override (`grep -n latency plugins/Vst3Effect/` → 0 hits) while the default returns
0 (`include/Effect.h:173-176`). *Files:* `Vst3Host.cpp:508-594` (call `getLatencySamples()` after
`setupProcessing`), `src/core/EffectChain.cpp:55-85`, `src/core/Mixer.cpp:1257-1345`.

**Q11 — Multi-out: what is the documented behaviour when an instrument has more outputs than the
first bus?** Current code sums nothing — it just ignores channels beyond `numOutputs`
(`Vst3Host.cpp:678-691`) and forces the port count at `Vst3Effect.cpp:77-81`. Say "first bus only,
others discarded, reported in the release notes" explicitly. *Files:* `Vst3BusMap.h:64-86`,
`plugins/Vst3Instrument/...` (`audioPorts().setAllChannelCounts`, cf. `Vst3Effect.cpp:79-81`).

**Q12 — Class identity: name or CID?** The key stores `class` = the class **name**
(`Vst3SubPluginFeatures.cpp:110-111`) and `load()` matches on that name (`Vst3Host.cpp:267`), even
though the SDK exposes `ClassInfo::classID()`. Any instrument that renames its class breaks saved
projects. Decide whether slice 1 keeps name-matching (fast) and files a follow-up, or stores the CID
now and keeps the name for display. *Files:* `Vst3Host.cpp:250-276`, `Vst3SubPluginFeatures.cpp:107-112`.

**Q13 — What happens when the user picks a VST3 *effect* from the instrument browser, or an
instrument from the effect browser?** `isInstrument()` is available on both sides
(`Vst3Host.h:81`), the browser filters by type (`Vst3SubPluginFeatures.cpp:105`), and
`Instrument::instantiate` silently substitutes `DummyInstrument` when the cast fails
(`src/core/Instrument.cpp:103-106`). Is a visible warning required (the effect path already warns at
`Vst3Effect.cpp:70`)? *Files:* new instrument ctor, `src/gui/instrument/InstrumentTrackWindow.cpp:375`.

**Q14 — Do the three orphaned VST3 test files still compile?** They were written for an earlier tree
and have never been built (`tests/CMakeLists.txt` mentions `vst3` zero times). Budget for rot, and
decide whether the slice wires them or replaces them. *Files:* `tests/CMakeLists.txt` (add a
`if(TARGET lmms_vst3_sdk)` block modelled on `:484-522`), `tests/src/plugins/Vst3HostTest.cpp:34-36`.

**Q15 — What is the VST3 test fixture's licence header and where does it live?** The CLAP precedent
is an in-tree C file under `tests/data/` (`tests/data/clap-test-plugin/clap-test-gain.c`, wired at
`tests/CMakeLists.txt:487-489`); for VST3 the fixture must be C++ against the SDK and must carry the
same MIT notice. *Files:* `tests/data/vst3-test-instrument/` (new `CMakeLists.txt` + sources).

---

## 5. UNVERIFIED

Stated plainly, not softened:

1. **UNVERIFIED — that `lmms_vst3_sdk` and the VST3 plug-ins compile on this box at all.** No SDK
   checkout exists and no `libvst3effect.so` exists in any of the 14 local build trees that `find`
   enumerated (§1.12); five
   builds were in flight and this task forbids building. The SDK source list
   (`cmake/modules/Vst3Sdk.cmake:70-99`) has evidently been compiled somewhere, but not here.
2. **UNVERIFIED — whether the SDK's own instrument samples build here.** `freetype2` is absent from
   `pkg-config` on this box (§1.12) and VSTGUI sample editors need it; whether `mda-vst3` or
   `note_expression_synth` can be configured without it was not tested.
3. **UNVERIFIED — the licence of the `samples/vst/mda-vst3` sources.** They are inside the MIT
   VST3 SDK repository, but the mda plug-in heritage is a separate question that was not checked.
   Do not use them as a fixture until someone reads their headers.
4. **UNVERIFIED — any specific third-party free VST3 instrument's availability, licence and
   behaviour.** No name is asserted in §3.2 for that reason. The lane must obtain one from the user.
5. **UNVERIFIED — the editor line-count estimate** in §3.3 (`+300 … +600`). It has no in-tree
   analogue to measure against; the other estimates are anchored on `wc -l` of the effect analogue.
6. **UNVERIFIED — that `DISPLAY` is usable for a GUI test from a non-interactive shell.** The X0
   socket exists and `Xvfb`/`xvfb-run`/`xdotool` are installed, but `DISPLAY` is unset in this
   environment; the editor acceptance leg must be run under `xvfb-run` explicitly.
7. **UNVERIFIED — behaviour of real-world instruments with respect to Q6 (state-before-activate).**
   No plug-in was available to test the ordering hypothesis; it is a design question, not a finding.
8. **UNVERIFIED — whether `Vst3HostTest.cpp` / `Vst3BusMapTest.cpp` / `Vst3EffectIntegrationTest.cpp`
   still compile.** They have never been wired into any build (`grep -c -i vst3 tests/CMakeLists.txt`
   → 0), so their rot state is unknown.

---

## Appendix — exact commands used

```bash
# tree / branch
git -C <worktree> branch --show-current        # post-alpha/instrument-hosting
git -C <worktree> log --oneline -1             # 0c23587d2
# effect host size
wc -l plugins/Vst3Effect/*.cpp plugins/Vst3Effect/*.h    # 1961 total
# SDK pin, and its live resolution
sed -n '1,70p' cmake/modules/Vst3Sdk.cmake
git ls-remote --tags https://github.com/steinbergmedia/vst3sdk.git | grep 3.8.1
# SDK source list compiled into lmms_vst3_sdk
sed -n '70,132p' cmake/modules/Vst3Sdk.cmake
# absence of an editor / event path / programs / latency in the VST3 plug-in
grep -rn "IPlugView" src/ include/ plugins/                       # 0 hits
grep -rn "inputEvents\|EventList\|kEvent" plugins/Vst3Effect/     # 0 hits
grep -rn "getProgramList\|IUnitInfo" plugins/Vst3Effect/          # 0 hits
grep -rn "latency" plugins/Vst3Effect/                            # 0 hits
# VST3 tests are never built
grep -c -i vst3 tests/CMakeLists.txt                              # 0
# SDK facts, at the pinned commits (raw.githubusercontent.com)
#   vst3_public_sdk@586dc5e6: host/eventlist.{h,cpp}, samples/vst-hosting/audiohost/source/media/{audioclient.cpp,miditovst.h},
#                             samples/vst/note_expression_synth/**, samples/vst/mda-vst3/source/mdafactory.cpp
#   vst3_pluginterfaces@4f547e8e: vst/ivstaudioprocessor.h, vst/ivstevents.h, vst/ivstunits.h, gui/iplugview.h,
#                             (and a recursive tree listing proving no instrument interface exists)
# this box
find / -xdev -maxdepth 7 -name ivstcomponent.h                    # empty
find <parent> -maxdepth 6 \( -name '*vst3effect*' -o -name '*clapeffect*' \) -name '*.so'   # empty
grep -h WANT_VST3 */build*/CMakeCache.txt | sort -u               # WANT_VST3:STRING=AUTO
ls ~/.vst3 ~/.vst ~/.lv2 ~/.clap                                  # all absent
sudo -n true                                                      # a password is required
pkg-config --exists freetype2; pkg-config --exists x11            # no / yes
which Xvfb xvfb-run xdotool                                       # present
ls <zene-w0>/build/plugins/*.so | grep -iE 'vestige|lv2|carla'    # present
```
