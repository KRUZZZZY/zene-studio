# VST3 instrument fixture — the enabling step for instrument hosting

**Branch:** `post-alpha/vst3-instrument-fixture` (based on `post-alpha/instrument-hosting`).
**Base commit:** `6f6c7f1c8` ("docs: instrument-hosting spec — evidence inventory, exact delta, smallest
honest slice"). **Repo:** Zene Studio (LMMS-derived, GPL-2.0-or-later).
**Settles:** UNVERIFIED items 1 and 2 of `docs/INSTRUMENT-HOSTING-SPEC.md` §5.

## 0. Headline

- **The pinned VST3 SDK is provisioned at the pin, and its licence is MIT — read from the SDK's own
  `LICENSE.txt`, not repeated from the spec.** §1.
- **`lmms_vst3_sdk` does NOT compile on this box as the tree stood.** Under the CI's
  `-DUSE_WERROR=ON` it fails on a `-Wformat` diagnostic in the SDK's `ustring.cpp`, exit **2**. The
  cause is a one-line omission in this repo's own build configuration, not the SDK. Fixed with the
  fork's **own** third-party mechanism (`SYSTEM`), after which it compiles: **exit 0, 31 translation
  units, 31 objects, `liblmms_vst3_sdk.a` 8,873,776 bytes.** §2.
- **A minimal MIT VST3 instrument fixture now exists and is verified by the SDK's own validator:
  `Result: 47 tests passed, 0 tests failed`**, including the validator's own instrument verdict,
  *"No bypass parameter found. This is an instrument."* §4.
- **A standalone probe proves the fixture consumes MIDI from `ProcessData::inputEvents` and renders
  audio in response, sample-accurately: exit 0, 16/16 checks.** §5.
- **This is not host-side instrument hosting.** Nothing in `plugins/Vst3Effect/` changed. What is
  unblocked is the *subject* the host lane needs. §7.

---

## 1. The SDK pin and its verified licence

### 1.1 Provisioning, at the pin

Obtained exactly as `cmake/modules/Vst3Sdk.cmake:12-15` documents, into the build tree (never the
source tree; `/build*/` is already in `.gitignore:1`):

```bash
cd <worktree>/build
git clone --depth 1 --branch v3.8.1_build_84 \
    https://github.com/steinbergmedia/vst3sdk.git vst3sdk
cd vst3sdk
git submodule update --init --depth 1 base cmake pluginterfaces public.sdk
git rev-parse HEAD
```

```
3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96      # == LMMS_VST3_SDK_COMMIT, Vst3Sdk.cmake:29
EXIT=0
```

Sub-module commits actually checked out, matching the pin the spec recorded from the SDK's
`.gitmodules` (`docs/INSTRUMENT-HOSTING-SPEC.md:82-87`):

```
fcf9da0bd27a16f7f03773a3a39822f28f5c8477 base           (v3.8.1_build_84)
054c9143cbb8d47fc4694e473f2ee3b4d951a8f5 cmake          (v3.8.1_build_84)
4f547e8e102b47de4a8b8aaf343c73b700786372 pluginterfaces (v3.8.1_build_84)
586dc5e6c8012c3e4b01c79389375cbe96bdb1da public.sdk     (v3.8.1_build_84)
```

`doc`, `tutorials` and `vstgui4` are **not** checked out: the pin's own command asks for four
sub-modules only, and the compiled subset deliberately excludes VSTGUI (`Vst3Sdk.cmake:19-26`).
Total checkout on disk: **39 MB** (the comment at `Vst3Sdk.cmake:9` says "~150 MB" — that figure is
for a full non-shallow clone; the shallow checkout is 39 MB).

### 1.2 Licence — verified, not repeated

The spec's claim ("the VST3 SDK is MIT licensed since 3.8") was checked against the SDK's own file
rather than restated:

```
$ head -3 build/vst3sdk/LICENSE.txt
MIT License

Copyright (c) 2026, Steinberg Media Technologies GmbH

$ sha256sum build/vst3sdk/LICENSE.txt
e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e  LICENSE.txt

$ grep -c "MIT License" build/vst3sdk/LICENSE.txt
1

$ ls build/vst3sdk/pluginterfaces/vst2.x
ls: cannot access 'pluginterfaces/vst2.x': No such file or directory
```

**Verdict:** MIT, `Copyright (c) 2026, Steinberg Media Technologies GmbH`. GPLv2-clean: MIT is
compatible with GPL-2.0-or-later, and no VST2 header is present in the checkout, so the Vestige-only
VST2 rule (workspace rule 8) is intact.

The tree's own gates agree, unprompted — this line is CMake's, not mine:

```
-- Found VST3 SDK 3.8 (MIT) at <worktree>/build/vst3sdk      (Vst3Sdk.cmake:63)
```

The SDK's own source headers carry `"subject to the license terms in the LICENSE file found in the
top-level directory of this distribution"` — i.e. the MIT file read above. No SDK file is modified
by this change.

---

## 2. `lmms_vst3_sdk` — it did not compile, and why

Target defined at `cmake/modules/Vst3Sdk.cmake:123`; 21 SDK sources plus a platform split
(`:70-119`), compiled by `plugins/Vst3Effect/CMakeLists.txt` because it `INCLUDE(Vst3Sdk)`s.

### 2.1 The failure, exactly as it happened

```bash
cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official \
      -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON
cmake --build build --target lmms_vst3_sdk -j 4
```

Output (unpiped exit code **2**):

```
/home/.../build/vst3sdk/pluginterfaces/base/ustring.cpp: In member function
    ‘bool Steinberg::UString::scanInt(Steinberg::int64&) const’:
.../pluginterfaces/base/ustring.cpp:228:41: error: format ‘%lld’ expects argument of type
    ‘long long int*’, but argument 3 has type ‘Steinberg::int64*’ {aka ‘long int*’} [-Werror=format=]
  228 |         return sscanf (str.data (), "%lld", &value) == 1;
      |                                      ~~~^   ~~~~~~
/home/.../pluginterfaces/base/ustring.cpp:256:56: error: format ‘%lld’ expects argument of type
    ‘long long int’, but argument 4 has type ‘Steinberg::int64’ {aka ‘long int’} [-Werror=format=]
cc1plus: all warnings being treated as errors
gmake[3]: *** [.../lmms_vst3_sdk.dir/__/__/vst3sdk/pluginterfaces/base/ustring.cpp.o] Error 1
gmake: *** [Makefile:2841: lmms_vst3_sdk] Error 2
```

### 2.2 The cause

`int64` is `long` on LP64, so `%lld` / `long long` is a genuine type mismatch in the SDK's own
source. GCC emits it as a **warning** (`cc1plus: all warnings being treated as errors` — the last
line above) and the SDK's own build never promotes it. This repo's `-DUSE_WERROR=ON`, set for the
CI's `linux-x86_64` job, does — and it is applied to the SDK's translation units as well.

The omission is in this repo: other third-party trees are compiled with the fork's third-party flag
set, and `lmms_vst3_sdk` was not.

```
cmake/modules/ErrorFlags.cmake:76
add_compile_options("$<IF:$<BOOL:$<TARGET_PROPERTY:SYSTEM>>,${THIRD_PARTY_COMPILE_ERROR_FLAGS},${COMPILE_ERROR_FLAGS}>")
```

i.e. a target whose `SYSTEM` property is true is compiled with `-w` instead of `-Wall -Werror`.
`plugins/FreeBoy/CMakeLists.txt:10` (gme), `plugins/OpulenZ/CMakeLists.txt:8` (adplug),
`plugins/Xpressive/CMakeLists.txt:31` (exprtk) and `plugins/ZynAddSubFX/CMakeLists.txt:111-113` all
set it. There is also a narrower precedent for a *single* diagnostic —
`plugins/MidiImport/CMakeLists.txt:3-12` adds `-Wno-error=unknown-pragmas` with the comment
"portsmf raises these warnings, which will prevent compilation with -Werror".

### 2.3 The fix — one property, using the repo's own mechanism

`cmake/modules/Vst3Sdk.cmake`, the static host library block (`:123-132`), gains `SYSTEM ON`
alongside the existing `POSITION_INDEPENDENT_CODE ON`, with the failure recorded in the comment
above it. The SDK source is **not** patched — there is no SDK source in this repository to patch,
by design.

This is a build-config file: Gate 6 treats `*.cmake` as non-behavioural, and no runtime behaviour
changes — the same sources are compiled to the same objects.

### 2.4 The result

```bash
find build/plugins/Vst3Effect/CMakeFiles/lmms_vst3_sdk.dir -name '*.o' -delete
rm -f build/plugins/Vst3Effect/liblmms_vst3_sdk.a
cmake --build build --target lmms_vst3_sdk -j 4 > /tmp/sdk-verify.log 2>&1
echo "EXIT=$?"
```

```
EXIT=0
objects: 31
compiled TUs: 31
-rw-rw-r-- 1 kruzzzzy kruzzzzy 8873776 .../build/plugins/Vst3Effect/liblmms_vst3_sdk.a
```

**UNVERIFIED item 1 (§5.1 of the spec) is settled: `lmms_vst3_sdk` compiles on this box — once the
target is marked `SYSTEM`, as the repo's other third-party trees are.**

---

## 3. The fixture

### 3.1 What it is, and why it mirrors the CLAP one

`tests/data/clap-test-plugin/clap-test-gain.c` + its `CMakeLists.txt` exist so the CLAP host can be
driven without a third-party binary. The VST3 host had no equivalent, which is why
`docs/INSTRUMENT-HOSTING-SPEC.md` §3.2 ranks a purpose-built fixture above the SDK's samples (which
need VSTGUI/freetype) and above a real third-party instrument (not installable here without sudo).
This is that fixture.

| File | Role |
| --- | --- |
| `tests/data/vst3-test-instrument/vst3-test-instrument.cpp` | the instrument (MIT, 440 lines) |
| `tests/data/vst3-test-instrument/CMakeLists.txt` | builds it as a `.vst3` bundle, opt-in |
| `tests/src/plugins/Vst3InstrumentFixtureProbe.cpp` | the independent probe (MIT, 333 lines) |
| `tests/CMakeLists.txt` (tail) | `WANT_VST3_TEST_INSTRUMENT` option + probe target + `add_test` |

### 3.2 It is an instrument, and it reads MIDI

- **Factory:** the standard VST3 module factory (`BEGIN_FACTORY_DEF` / `DEF_CLASS2` / `END_FACTORY`,
  exporting `GetPluginFactory`), plus the module entry points `ModuleEntry` / `ModuleExit` that
  `public.sdk/source/vst/hosting/module_linux.cpp:172-186` requires by name.
- **Category — an instrument, not an effect:** registered under `kVstAudioEffectClass` (every VST3
  component is) with subcategory `Vst::PlugType::kInstrumentSynth` = `"Instrument|Synth"`. That
  subcategory string is exactly what the host reads to tell an instrument from an effect
  (`plugins/Vst3Effect/Vst3Host.cpp:244`, `:304`). No `Fx` category is declared.
- **Bus layout — the instrument shape:** no audio input, one stereo `kAudio` output, one `kEvent`
  input. The asymmetry is the point: an effect has an audio input.
- **It consumes MIDI.** `process()` walks `data.inputEvents` (`IEventList`), folds note-on/note-off
  into a fixed `bool mGate[128]`, and renders **a constant level while any key is down and exact
  zero otherwise**, splitting the block at each event's `sampleOffset`. That makes host-level
  assertions exact — "non-silent up to the note-off offset, exactly silent after it" — with no
  dependence on DSP coincidence, which is what the spec's acceptance test (§3.4 item 2) asks for.
- **Real-time safe process path:** no allocation, no locking, no unbounded growth. State is
  fixed-size members; each event is fetched once via `getEvent`; the render loop is `O(numSamples)`
  writes plus `O(numEvents)` reads. The fixture runs on the audio thread in a real host.
- **Also implemented** so it is a whole plug-in rather than a stub: one automatable parameter
  (`Level`), `IComponent::getState/setState` and the edit-controller `getEditorState/setEditorState`,
  both of which `Vst3Host.cpp:448-500` calls.

### 3.3 Base class and why

Derived from `Vst::SingleComponentEffect` (SDK `public.sdk/source/vst/vstsinglecomponenteffect.h`),
the SDK's own base for a plug-in whose processor and edit controller are one component. The host has
an explicit branch for that case (`Vst3Host.cpp:295-299`), so the fixture exercises it. Built
against the pinned SDK; linked against the **same** `lmms_vst3_sdk` the host links.

### 3.4 Plugin-side SDK sources

`lmms_vst3_sdk` compiles the **hosting** subset, so the plug-in-side translation units come from the
SDK's own `smtg_create_public_sdk_target` list (`build/vst3sdk/cmake/modules/SMTG_VST3_SDK.cmake`)
and are built as an OBJECT library `vst3-test-instrument-sdkbase`:

```
public.sdk/source/common/pluginview.cpp
public.sdk/source/main/linuxmain.cpp
public.sdk/source/main/moduleinit.cpp
public.sdk/source/main/pluginfactory.cpp
public.sdk/source/vst/vstbus.cpp
public.sdk/source/vst/vstcomponentbase.cpp
public.sdk/source/vst/vstparameters.cpp
public.sdk/source/vst/vstsinglecomponenteffect.cpp
```

Three build facts worth recording, each of which cost a real build failure here:

1. **OBJECT library, not STATIC.** A `.vst3` module is `dlopen`ed and nothing inside it references
   `ModuleEntry`, so a static library loses `linuxmain.cpp.o` to the linker and the module fails to
   load with *"The shared library does not export the required 'ModuleEntry' function"*. Object
   libraries are always linked whole.
2. **`pluginview.cpp` is required** — `vsteditcontroller.cpp` (pulled in by
   `vstsinglecomponenteffect.cpp`) needs `typeinfo for Steinberg::CPluginView`.
3. **`BEGIN_FACTORY_DEF` is the 3-argument macro from `pluginterfaces/vst/ivstcomponent.h:37`**,
   not the 4-argument one in `public.sdk/source/main/pluginfactory_constexpr.h:176`. The 3-arg form
   pairs with `DEF_CLASS2`/`END_FACTORY` from `public.sdk/source/main/pluginfactory.h`. Also:
   the buffer type is `AudioBusBuffers`, not `BusBuffer`.

### 3.5 Bundle layout, and the option

The SDK's Linux loader insists on the VST3 **directory bundle**
(`module_linux.cpp:getSOPath` returns nothing for a non-directory; it wants
`Contents/<uname -m>-linux/<stem>.so`). The target produces
`build/tests/data/vst3-test-instrument/vst3-test-instrument.vst3/Contents/x86_64-linux/vst3-test-instrument.so`.
(The host's scanner already accepts directories ending `.vst3` —
`plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:66-70`.)

**Opt-in and never installed.** `WANT_VST3_TEST_INSTRUMENT` (`tests/CMakeLists.txt`, default `OFF`)
gates both the bundle and the probe, so an ordinary build is unaffected. It is built under `tests/`,
never under `plugins/`, so it is not in the product's plug-in set or its install rules.

---

## 4. Verdict of the SDK's own validator (raw)

The pinned SDK ships one: `public.sdk/samples/vst-hosting/validator/` (source, README, its own
CMakeLists that links `sdk_hosting`).

**The SDK's own build system cannot configure on this box** with hosting samples enabled, because
`editorhost` hard-requires two pkg-config modules that are absent, and the samples are added
unconditionally (`public.sdk/samples/vst-hosting/CMakeLists.txt:8` calls `smtg_add_subdirectories()`):

```
-- Configuring incomplete, errors occurred!
Call Stack (most recent call first):
  public.sdk/samples/vst-hosting/editorhost/CMakeLists.txt:42 (pkg_check_modules)
   - gtk+-3.0
VALIDATOR_PIPELINE_EXIT=1
```

That is a defect of the SDK's sample collection, not of the validator. `editorhost` is a GUI *host*,
needed for none of this. So the validator was built by the SDK's own build system, with configure's
pkg-config check satisfied by two stub `.pc` files and **only the `validator` target** built —
`editorhost` is never compiled, and no SDK file is modified. The two stubs, verbatim, are what made
configure pass; each is `Name`/`Version` metadata with empty `Cflags`/`Libs`, since nothing that
consumes them is built:

```ini
# /tmp/pcfake/gtk+-3.0.pc  (and gtkmm-3.0.pc, identical but for Name: gtkmm-3.0)
prefix=/nonexistent
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include

Name: gtk+-3.0
Description: stub for configure-time pkg_check_modules only (editorhost is never built)
Version: 3.24.0
Cflags:
Libs:
```

```bash
PKG_CONFIG_PATH=/tmp/pcfake cmake -S build/vst3sdk -B build/vst3sdk-validator \
    -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
    -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON
PKG_CONFIG_PATH=/tmp/pcfake cmake --build build/vst3sdk-validator --target validator -j 4
```

`VALIDATOR_PIPELINE_EXIT=0`. Run against the fixture:

```bash
./build/vst3sdk-validator/bin/validator \
    build/tests/data/vst3-test-instrument/vst3-test-instrument.vst3
echo "VALIDATOR_EXIT=$?"
```

```
VALIDATOR_EXIT=0

* Loading module...
* Check valid bundle structure...
* Scanning classes...

  Class Info 0:
	name = Zene VST3 Test Instrument
	category = Audio Module Class
	subCategories = Instrument|Synth
	version = 1.0.0
	sdkVersion = VST 3.8.1
	cid = 5A1E57B09C4D42E7B0C3F81A6D2E4A90

[Scan Editor Classes]
Info:  Processor and edit controller united.
[Succeeded]

[Scan Buses]
Info:  => Audio Buses: [0 In(s) => 1 Out(s)]
Info:       Out[0]: "Audio Output" (Main-Default Active)
Info:  => Event Buses: [1 In(s) => 0 Out(s)]
Info:       In [0]: "Event Input" (Main-Default Active)
[Succeeded]

[Scan Parameters]
Info:  This component exports 1 parameter(s)
Info:     Parameter 000 (id=100): [title="Level"] [unit=""] [type = Float, default = 0.500000, unit = 0]
Info:  No bypass parameter found. This is an instrument.
[Succeeded]

[Process Test] [Succeeded]
[Process function running in another thread] [Succeeded]
[Silence Flags] [Succeeded]
[Silence Processing] [Succeeded]
[Variable Block Size] [Succeeded]
[Process Format]
Info:  ***Tested Sample Rates***
Info:        22050 Hz - processed successfully!
      ... 32000, 44100, 48000, 88200, 96000, 192000, 384000, 1234.5678, 12345.678,
          123456.78, 1234567.8 Hz - processed successfully!
[Succeeded]
[In: Stereo: 2 Channels, Out: Stereo: 2 Channels] [Succeeded]
[Accuracy: Block, 1 Parameters, Change every100 Samples] [Succeeded]
[Accuracy: Sample, 1 Parameters, Change every100 Samples] [Succeeded]

-------------------------------------------------------------
Result: 47 tests passed, 0 tests failed
-------------------------------------------------------------
```

**The instrument verdict line, verbatim:**

> `Info:  No bypass parameter found. This is an instrument.`

and the bus verdict:

> `Info:  => Audio Buses: [0 In(s) => 1 Out(s)]` / `Info:  => Event Buses: [1 In(s) => 0 Out(s)]`

Also relevant to the lane that follows: the validator reports
`Processor and edit controller united` (the single-component path) and
`64bit Audio Processing not supported` (the fixture is 32-bit only, like the host's
`setup.symbolicSampleSize = kSample32` at `Vst3Host.cpp:525`).

Two informational notes, neither a failure: the validator notes that the controller implements
`IUnitInfo` with no units (inherited from `SingleComponentEffect`/`EditControllerEx1`), and that
sample-accurate parameter changes are not all read — the fixture does not consume
`inputParameterChanges`, only events, which is precisely its designed scope.

**UNVERIFIED item 2 (§5.2) is settled by construction:** the SDK's own instrument samples were never
needed. The purpose-built fixture is VSTGUI-free and builds with no `freetype2`, so the "will the
samples build without VSTGUI" question is moot for this lane's purpose.

---

## 5. The probe (second, independent check)

The validator says the module is well-formed and is an instrument. It does not — and cannot — prove
this *host's* problem: that a note delivered through `ProcessData::inputEvents` produces audio.
`tests/src/plugins/Vst3InstrumentFixtureProbe.cpp` does, linking `lmms_vst3_sdk` and using the SDK's
own `VST3::Hosting::Module::create` + `PlugProvider`, exactly as `Vst3Host::load()` does.

```
$ ./build/tests/Vst3InstrumentFixtureProbe; echo "PROBE_EXIT=$?"
PROBE_EXIT=0
== VST3 instrument fixture probe ==
bundle: .../build/tests/data/vst3-test-instrument/vst3-test-instrument.vst3

-- module load (VST3::Hosting::Module::create) --
  [PASS] the .vst3 bundle loads through the SDK's own module loader

-- class infos reported by the module factory --
  name="Zene VST3 Test Instrument"
    category="Audio Module Class" subCategories="Instrument|Synth" vendor="Zene Studio test fixture"
  [PASS] declares a kVstAudioEffectClass whose subcategory is an Instrument
  [PASS] declares no Fx-categorised class (it is an instrument, not an effect)

-- instantiation (SDK PlugProvider) --
  [PASS] PlugProvider::initialize()
  [PASS] IComponent and IAudioProcessor are available
  [PASS] an IEditController is available
  component is single-component (processor == controller): yes

-- bus layout reported by the module --
  kAudio input: 0 bus(es)
  kAudio output: 1 bus(es)
    [0] name="Audio Output" channels=2 busType=kMain active=yes
  kEvent input: 1 bus(es)
    [0] name="Event Input" channels=1 busType=kMain active=yes
  kEvent output: 0 bus(es)
  [PASS] NO audio input bus - the instrument shape, not the effect shape
  [PASS] exactly one audio output bus
  [PASS] at least one kEvent input bus - the MIDI path
  [PASS] setupProcessing(kSample32, 44.1 kHz, 512)
  [PASS] setActive(true)
  [PASS] setProcessing(true)

-- MIDI in -> audio out (ProcessData::inputEvents) --
  [PASS] process() returns kResultOk
  [PASS] a block with no events renders exact silence (no note is latent)
  [PASS] process() with a note-on at 0 and a note-off at 256
  [PASS] the block is NON-SILENT before the note-off - MIDI really drives audio
  [PASS] the block is EXACTLY silent from the note-off sampleOffset onwards
    level=0.500000  sample[0]=0.500000  sample[255]=0.500000  sample[256]=0.000000  sample[511]=0.000000
  [PASS] the next block is silent again (the note did not leak forward)
  [PASS] process() with a note-on at sampleOffset 128
  [PASS] a note-on at sampleOffset 128 sounds from sample 128, not from sample 0

RESULT: PASS (0 failed check(s))
```

16 checks, 0 failures. `sample[255] == 0.5` and `sample[256] == 0.0` is the sample-accurate
note-off: MIDI in through `inputEvents`, audio out, landing exactly on the event's `sampleOffset`.

The probe is a helper executable, not a QTest class — the class Gate 3 explicitly puts out of scope
("Helper executables (harnesses, probes, reference renderers) are NOT QTest classes", bottom of
`tests/no-tautology-gate.sh`) — but it exits non-zero on a failed check and is registered with
`add_test`, so ctest enforces it.

---

## 6. Registration and gates

**Registration.** Both new C++ sources are listed in `tests/all-sources.txt`, matching the CLAP
fixture's registration exactly (`tests/all-sources.txt:1060`
`tests/data/clap-test-plugin/clap-test-gain.c`) — test subjects live in the whole-tree manifest, and
`tests/` has never held an entry in `tests/fork-sources.txt`. `cmake/modules/Vst3Sdk.cmake` and
`tests/CMakeLists.txt` are build/config files, which Gate 6 allows without a ledger entry, and the
new `tests/**` files are in Gate 6's allowed `tests/**` scope by rule. No upstream-inherited
production file was touched, so `tests/upstream-modifications.txt` gains no line.

**Gate exit codes** (all unpiped):

| Gate | Exit | Verdict |
| --- | --- | --- |
| `bash tests/fork-sources-gate.sh` | **127** | script absent from this branch; the registration was verified with its own logic instead → **0** (below) |
| `bash tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is declared (31 file(s) in the ledger)` |
| `bash tests/run-all-gates.sh` | **0** | `RESULT: PASS — every executed gate passed` (7 PASS, 1 SKIP — below) |

```
================ SUMMARY ================
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS

RESULT: PASS — every executed gate passed
RUN_ALL_GATES_EXIT=0
```

Gate 2 is SKIP because it needs `--with-coverage`; Gate 5 (mutation) ran and passed. Note for the
next lane: this branch's `run-all-gates.sh` returns **0** for a run with skips — it records `SKIP`
as a row rather than encoding it in the exit code — so "PASS-WITH-SKIPS" is read off the summary
table above, not off `$?`.

`tests/fork-sources-gate.sh` is **absent** from `post-alpha/instrument-hosting` and from this
branch: it was added later by a sibling lane (`879e251ef`, "test(gates): Gate 9 - every tracked
source must be in a scope manifest"), and it is present on `post-alpha/gate-debt`,
`post-alpha/integration`, `post-alpha/clipslice`, `post-alpha/stems`, `post-alpha/lufswire`,
`post-alpha/midirace` and `post-alpha/hardening` — with **two different contents** across them
(md5 `94498bf8` on six lanes, `3615ed5c` on `post-alpha/hardening`). Importing another lane's gate
was therefore not done; the registration above was instead **verified with that gate's own logic**,
run from a temporary copy of the six-lane version, which was deleted before committing:

```
$ bash tests/fork-sources-gate.sh          # the real name, on this branch
FORK_SOURCES_GATE_EXIT=127
bash: tests/fork-sources-gate.sh: No such file or directory

$ cp <sibling>/tests/fork-sources-gate.sh tests/ && bash tests/fork-sources-gate.sh
scanned 1093 tracked source file(s) under src/, include/, plugins/, tests/;
  100 fork-sources entry(ies), 994 inherited upstream, 0 stale entry(ies).
PASS: every tracked source in scope is registered (100 fork-NEW, 994 inherited).
FORK_SOURCES_GATE_EXIT=0
```

(The gate reads `git ls-files`, so it can only see files that are tracked. Run before the commit it
scans 1091 files and passes trivially — worth knowing before trusting a green run on a dirty tree.)

### 6.1 The repo's own reproduction command

The script is committed mode `100644`, so it must be invoked through `bash` or it exits 126 —
exactly as the task's brief says.

```bash
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```

```
machine     : Linux x86_64, Ubuntu 24.04.4 LTS
compiler    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake       : cmake version 3.28.3
ccache      : /home/kruzzzzy/.local/bin/ccache
build dir   : build (jobs=4, ctest -j2)
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON

--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26

linux-x86_64: REPRODUCED
  run: configure OK, build OK, ctest OK (100% tests passed, 0 tests failed out of 26)
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs
             qtbase5-dev; this box has Qt6 only)
local-ci: overall exit=0 (0 = every executed step passed)
```

The probe is one of the 26 tests, and it runs in that same suite:

```
      Start 26: Vst3InstrumentFixtureProbe
25/26 Test #26: Vst3InstrumentFixtureProbe .......   Passed    0.01 sec
100% tests passed, 0 tests failed out of 26
```

**`LOCAL_CI_EXIT=0`**, and the full build under `-DUSE_WERROR=ON` — the flag combination the SDK
target failed on at the start of this document — completed with **0 errors**, which is the
end-to-end confirmation of §2.

### 6.2 Behaviour with the option OFF

```bash
cmake -S . -B build -DWANT_VST3_TEST_INSTRUMENT=OFF    # RECONFIGURE_OFF_EXIT=0
cmake --build build --target vst3-test-instrument       # TARGET_WITH_OPTION_OFF_EXIT=2
gmake: *** No rule to make target 'vst3-test-instrument'.  Stop.
```

With the option OFF neither the bundle nor the probe exists as a build target, so an ordinary build
is unchanged. The one thing that *is* different for every configuration is the `SYSTEM` property in
§2.3 — which is a build fix the target needed regardless of this fixture.

### 6.3 A note on the shared machine

There are ~30 lanes on this box and they all share `/tmp`. A log written to a generic path
(`/tmp/local-ci.log`) was overwritten mid-run by a sibling lane, and the result read back belonged
to *that* lane's tree — it reported 27 tests including a `MidiLearnThreadTest` that does not exist
anywhere in this tree (`grep -rn -i midilearn tests/CMakeLists.txt` → nothing;
`git ls-tree 6f6c7f1c8 -- tests/src/core/MidiLearnThreadTest.cpp` → nothing). Every number quoted
in this document comes from a lane-unique log path, and was re-run and re-read from this worktree's
own `build/` after the sibling's file was identified. The tree was also still mid-build at that
point, which is why some test binaries were momentarily missing. **Read `$BUILD_DIR/ctest.log` from
your own build directory, never a shared `/tmp` path.**

---

## 7. What this unblocks, and what is still unverified

### Now UNBLOCKED for the implementation lane

1. **The SDK is provisioned at the pin and its licence is settled from the SDK's own file.** No
   network step remains in the lane's critical path: `build/vst3sdk` is a complete checkout, and
   `-DLMMS_VST3_SDK_PATH=<dir>` selects any other.
2. **`lmms_vst3_sdk` compiles, and the reason it did not is understood and fixed**, using the
   repo's own `SYSTEM` mechanism rather than an SDK patch or a per-file warning exemption.
3. **There is a MIDI-capable VST3 instrument to test against, and it is verified twice** — by the
   SDK's own validator (47/47, "This is an instrument") and by a probe that demonstrates a note event
   becoming audio. `docs/INSTRUMENT-HOSTING-SPEC.md` §3.4 item 2's "first half non-silent, second
   half silent" assertion is now a demonstrated property of the subject, not an aspiration.
4. **The host's event contract is pinned down in a runnable form.** The probe shows exactly what the
   host must produce for the lane to pass: `ProcessData::inputEvents` assigned to an `IEventList`
   carrying note events with real `sampleOffset`s, `processMode = kRealtime`,
   `symbolicSampleSize = kSample32`, and one `AudioBusBuffers` output bus.
5. **A validation command exists** for the next lane to re-run after it touches the host: §4's three
   commands, plus `./build/tests/Vst3InstrumentFixtureProbe`.

### Still UNVERIFIED

1. **Whether the LMMS host will actually drive the fixture.** This task was explicitly forbidden from
   implementing host-side hosting, and nothing in `plugins/Vst3Effect/` changed:
   `ProcessData::inputEvents` is still never assigned (`Vst3Host.cpp:573-581`), there is still no
   event-bus enumeration, and `grep -rn inputEvents plugins/Vst3Effect/` still returns nothing. The
   fixture proves the *subject* works; it does not prove the host does.
2. **The three orphaned VST3 test sources** (`tests/src/plugins/Vst3HostTest.cpp`,
   `Vst3BusMapTest.cpp`, `Vst3EffectIntegrationTest.cpp`) still compile nowhere —
   `grep -c -i vst3 tests/CMakeLists.txt` is still 0 for them. This task added its own probe; it did
   not wire theirs.
3. **Any third-party VST3 instrument.** Still none installed, still not installable here without
   sudo; the `.vst3` scan directory still does not exist. Nothing here changes that.
4. **The editor (`IPlugView`).** Not attempted and not in scope; the fixture deliberately provides
   no view, so the editor acceptance leg (§3.4 item 4) has no subject yet.
5. **The gates this branch cannot run.** `tests/fork-sources-gate.sh` is not on this branch (§6).
   `run-all-gates.sh` here covers Gates 1, 3, 4, 6, 7, 8 only — Gate 2 (coverage) needs
   `--with-coverage`, Gate 5 (mutation) is skipped per the task's expectation of exit 3, and Gate 9
   is the absent script above.
6. **`-DUSE_WERROR=ON` on the other platforms.** The `SYSTEM` fix is
   compiler-flag-agnostic (it selects `THIRD_PARTY_COMPILE_ERROR_FLAGS`, i.e. `/W0` on MSVC, not
   just a GCC flag), but only the Linux GCC path was built here.
