# VCA / mix-and-edit groups — task #622

Branch `post-alpha/vca`, base `post-alpha/v0.2` = `0c23587d2`. Every commit is local; nothing was
pushed and no upstream/origin remote was touched.

Owner decision **D9** (2026-09-11, `MASTER-PLAN.md` §3) reversed this out of DAW-GAP's "never" set
into **Bar 2**, with the register's own smallest honest version as the bar: *"group tracks with
relative volume/mute/solo linking + a VCA fader that scales its members, surviving save/reload."*
The register also warns that the cost is **verification** — Cubase's VCAs have been "broken ever
since" 2014 — so the tests below carry the weight, not the feature code.

---

## 1. What existed (at `0c23587d2`)

Established by reading the tree, not assumed:

| what | where |
|---|---|
| the channel entity | `include/Mixer.h:61` — `class MixerChannel : public ThreadableJob`; one per mixer channel, index 0 is master |
| the channel fader | `include/Mixer.h:83` — `FloatModel m_volumeModel`, range 0…2, unity 1.0 |
| the channel fader's **second** consumer | `src/core/Mixer.cpp:456`, `:466`, `:470` — a **receiving** channel reads `sender->m_volumeModel.value()` and multiplies it (times the send amount) into its own buffer. The fader is therefore applied on the *receiver's* side of a regular send |
| where a channel's own output is scaled per block | `src/core/Mixer.cpp:332` `MixerChannel::updatePostFaderBuffer()`, called from `doProcessing()` at `:508`; `m_buffer` stays post-FX/pre-fader so pre-fader sends and sidechain taps stay independent of the fader |
| mute / solo | `include/Mixer.h:81-82` — `BoolModel m_muteModel`, `BoolModel m_soloModel`; snapshotted per period into `MixerChannel::m_muted` in `Mixer::masterMix()` (`src/core/Mixer.cpp:1373`), gated in `mixToChannel()` (`:1145`), driven by `activateSolo()` `:585` / `deactivateSolo()` `:594` / `toggledSolo()` `:602`. Soloing works by *writing other channels' mute models*, saving `m_muteBeforeSolo` first |
| mixer serialisation | `Mixer::saveSettings()` `src/core/Mixer.cpp:1512` (channels as `<mixerchannel>`, models as attributes via `m_volumeModel.saveSettings(doc, el, "volume")` etc. at `:1523-1525`), `Mixer::loadSettings()` `:1590` (`:1621-1623`) |
| the model types | `FloatModel` / `BoolModel` in `include/AutomatableModel.h` (`:461`, `:495`) |

**There is no VCA or group entity anywhere in the tree.** `grep -riE 'vca'` over first-party
`src/`, `include/` and `tests/` matches only `tests/reference/Lb302/Lb302.{h,cpp}` — the *internal
voltage-controlled amplifier of the Lb302 bass synth*, entirely unrelated to mixer grouping. No
grouping object, no multi-channel selection entity, no `MixerGroup`.

---

## 2. Design

### 2.1 The entity

`include/VcaGroup.h` + `src/core/VcaGroup.cpp` (both new): `class VcaGroup` owns a **`FloatModel`
fader** (`vcaModel()`, same 0…2 range and 0.001 step as a channel fader), a **`BoolModel` mute**, a
**`BoolModel` solo**, a name, a stable **id**, and an ascending `std::vector<mix_ch_t>` of member
channel indices. `Mixer` owns `std::vector<VcaGroup*>` and gains
`createVcaGroup/vcaGroup/vcaGroupForChannel/vcaGroups/deleteVcaGroup/clearVcaGroups/refreshGroups/applyGroupSolo`.
A channel belongs to at most one group; master (index 0) can never be a member; a second group's
`addMember` for the same channel is refused.

### 2.2 The fader scales members **relatively** — and that is the whole point

The group's gain is published to each member as a **separate multiplier**,
`MixerChannel::m_vcaGain` (a `std::atomic<float>`, default 1.0f). **A member's own `m_volumeModel`
is never written.** The group is therefore a relative offset on top of whatever the member's fader
already says, and "move the VCA and move it back" restores the member models *bit-exactly by
construction* — there is nothing to restore, because nothing was overwritten.

This is exactly the trap the register points at: a scheme that writes the scaled value into the
member fader and divides it back out is **not** invertible in binary floating point. The test suite
proves both halves — the real scheme round-trips, and an emulation of the naive scheme does not
(`tests/src/core/VcaGroupTest.cpp::absoluteScalingWouldNotRoundTrip`: member 0.9f through a 0.7f
move comes back as `0.900000035763`, i.e. lost).

### 2.3 Where the multiply sits on the audio path

One site: `MixerChannel::doProcessing()`, immediately after the FX chain, guarded by
`if (vcaGain != 1.0f)`. `m_buffer` at that point is the post-FX signal that the channel's own fader
snapshot (`updatePostFaderBuffer`), the peak meter, both sidechain taps and **every receiving
channel** read, so a single in-place multiply scales the member's whole output. Pre-FX taps are
taken before it (unaffected), and the guard means an *ungrouped* channel — or a group at unity —
takes the pre-#622 arithmetic path unchanged, consequently with **byte-identical output**.

Mute is folded into the same factor (a VCA at zero gain *is* a mute), so muting a group never writes
a member's mute model either, and is equally reversible. The quiet-bit optimisation of `AudioBus` is
deliberately *not* touched here: the flags are an optimisation the DSP trusts, and claiming silence
the graph does not have loses audio, while claiming audio it does not have only wastes work.

### 2.4 Threading and the realtime contract

- All group state (models, member list, every mutator) is **control-thread only**.
- The **only** state the audio thread touches is the per-channel `m_vcaGain` float: a relaxed load
  once per block in `doProcessing()`.
- A change reaches the audio thread by the control thread storing the new float in
  `Mixer::refreshGroups()` (called from the models' `dataChanged` signal, from membership edits and
  from load). That loop is `O(channels + members)` and allocation-free.
- No allocation, no locking, no unbounded growth on the audio path — proved by
  `AllocationProbe` around eight real `Mixer::masterMix()` periods with a group at −4.4 dB:
  **0 allocations**.
- `deleteChannel` / `moveChannelLeft` carry the group membership with the channel indices, the same
  bookkeeping the tracks already get.

### 2.5 Persistence

`saveSettings()` appends one `<vcagroup id name vca muted soloed>` element per group inside
`<mixer>`, with one `<member channel="N"/>` child each; `loadSettings()` recreates them and calls
`refreshGroups()`. The id is preserved so a group keeps its identity across a round trip.

Two supporting changes are load-order requirements, not decoration:

- the channel-loading loop now **skips non-`mixerchannel` children** — without it a `<vcagroup>`
  sibling would be parsed as a channel with `num=0` and clobber master's name and volume;
- `MixerChannel::m_muteBeforeSolo` is now **initialised to false** in the constructor. It was read
  by `deactivateSolo()` before ever being written (indeterminate for any channel never soloed) —
  a latent upstream defect that the group's solo path exercises.
- On downgrade a legacy LMMS ignores `<vcagroup>` and plays every member at its own fader value —
  the same forward-compatible degradation the Phase D `<bus>` marker already uses.

---

## 3. Mute / solo semantics (chosen, then tested one rule each)

| id | rule |
|---|---|
| **S1** | Soloing a **member** soloes its whole group: setting any member's solo makes every member audible, while non-members are muted exactly as a channel solo mutes them. |
| **S2** | Soloing the **group** makes exactly its members audible and nothing else. Group solos are exclusive (like the mixer's own single-solo latch): setting a second group's solo clears the first's. |
| **S3** | A non-member is muted while the group is soloed. |
| **M1** | Group mute silences every member, reversibly; members' own mute models are never written. |
| **M2** | Group mute is a **gain of zero on the audio path**, not the channel mute machinery, so a soloed member of a muted group stays silent. |
| **M3** | Mute does **not** propagate member → group: muting one member mutes only that member. |

S1 is the "mix-and-edit" half (edit one, all follow); M3 keeps a member's own mute local, because
propagating it would make "mute this one channel" silence the whole group.

---

## 4. What a user must do to reach it today

**No GUI was built.** There is no group strip, no VCA fader widget, no "group selected channels"
command, and no entry in `MixerView`. What exists is the engine API and its persistence:

- **Project file (works today, no code needed).** Add the group by hand to a `.mmp`/`.mmpz`; the
  loader accepts it. The exact edit used for the render proofs:

  ```python
  # insert before "    </mixer>" inside <mixer>
  <vcagroup id="0" name="Group" vca="0.5" muted="0" soloed="0">
    <member channel="1"/>
    <member channel="2"/>
  </vcagroup>
  ```

  (Full fixture and the injector command are in §5.3.) This is how the render evidence below was
  produced, so it is a *verified* path, not a claim.
- **C++ / the in-tree API.** `Engine::mixer()->createVcaGroup("Drums")`, `addMember(ch)`,
  `vcaModel()->setValue(x)`, `muteModel()`, `soloModel()`, `deleteVcaGroup(id)`.

**What a user of the product still lacks:** any way to create or move a VCA group without editing a
project file or writing code. Concretely — no VCA strip in `MixerView` (`src/gui/MixerView.cpp`
knows nothing about groups), no script binding (`src/core/ScriptBindings.cpp` exposes no mixer
object at all, so `lmms.mixer`/VCA verbs do not exist), no undo action, no keyboard shortcut. A
mixer-side GUI is the obvious next slice; the register's "weeks" size band is mostly this plus the
verification, and this change deliberately spends its budget on the verification half.

---

## 5. Proofs

### 5.1 Build and test (exit codes read unpiped)

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0   (log: build/configure.log)
build EXIT=0       (log: build/build.log)
ctest EXIT=0       (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
local-ci: overall exit=0
```
Deviation printed by the script and repeated here: this box has no Qt5 development files, so the
build adds **`-DWANT_QT6=ON`** to the CI linux-x86_64 flag set. The suite is 26 tests; the same tree
without this change is 25, so the count moved by exactly the new `VcaGroupTest`.

### 5.2 In-harness semantics (`tests/src/core/VcaGroupTest.cpp`, 16 slots)

`QT_QPA_PLATFORM=offscreen ./build/tests/VcaGroupTest` → **exit 0, 16 passed, 0 failed**. Every
number below is printed by the test itself (`VCA_EVIDENCE` lines) and is reproducible.

**Reversibility.** Members 2, 4, 6, group fader 1.0 → 0.5 → 1.0:
`models_bit_exact=1 render_byte_exact=1`; every channel's own `m_volumeModel` is unchanged
(bit-compared, not fuzzy-compared), and the render after the round trip is byte-identical to the
render before it (`render_sha256=94bcd2af…`).

**The inverted control.** The naive scheme this must *not* be, emulated with the same `FloatModel`
and the same arithmetic: `naive_roundtrip v=0.9 vca=0.7 restored=0.900000035763 bit_exact=0`,
`naive_lost_members=1 real_lost_members=0`. The check therefore has teeth.

**Relative scaling, measured on audio.** One member fed at a time, VCA 1.0 → 0.5:
`db_delta vca=-6.0206 member[2]=-6.0206 member[4]=-6.0206 member[6]=-6.0206
non_members_checked=5 balance_unchanged=1 exact_scaling=1`. Each member's *actual* rendered level
drops by the fader's own delta, each of the five non-members is unmoved, the scaling is
sample-exact, and the ratios between members are unchanged to <1e-6.

**Mute / solo.** `group_mute members_silent=1 member_models_untouched=1 solo_cannot_lift=1
restored=<sha of the pre-mute render>`; `solo_member soloed=4 group_audible=2,4,6
non_member_silent=1`; `solo_group members_audible=2,4,6 others_muted=1 restored=1`.

**Persistence.** Two groups (`Drums` vca 0.6 members 2,4,6,7; `Still-muted` muted member 3), saved
and reloaded: a full dump of the mixer (every channel's volume, mute, solo, published gain and
sends, plus both groups) is **identical before and after**, and the reloaded project renders
**byte-identical** audio: `save_load … state_identical=1 audio_identical=1 sha256=2a22bb45…`.

**Old project.** A pre-#622 `<mixer>` (three channels, no `<vcagroup>`) loads to **0 groups** with
unity gain everywhere, renders non-silent, gains **no** `<vcagroup>` element when saved again, and
renders byte-identically when loaded a second time from a clean mixer
(`sha256=ac349947…`).

**Realtime.** `rt_allocation periods=8 allocations=0` — eight real `masterMix()` periods with a
group at 0.6, all buffers allocated outside the measured window.

**Harness controls** (so the "unchanged" claims cannot be vacuous): halving a channel fader and
halving a send amount each halve the render **exactly** (`send_half_max_err=0 fader_half_max_err=0`),
and a solo render's level is independent of whatever was rendered before it (`levels_independent_of_preceding_render` ratios all 1).

### 5.3 Byte-identical render on a real project (the behaviour-preserving proof)

Fixture: `tests/data/vca-render-fixture.mmp` — 2 bars at 140 bpm, two TripleOscillator instruments
on mixer channels 1 and 2 (volumes 0.8 / 0.6, sends 0.9 / 0.7 to master), no external samples or
plugins. Rendered headless at 44.1 kHz float32 → 226,560 frames (5.14 s), data-chunk SHA-256:

| render | binary | sha256 of the `data` chunk | peak |
|---|---|---|---|
| fixture, **before** this change | `build/lmms-base`, verified pre-change (`strings \| grep -c vcagroup` = **0**) | `8730ba1958f616ce3bc649042e690ccd88c7393096d01d98ea6bfdc14aaf109a` | 1.285403848 |
| fixture, after this change, **no group** | this branch | `8730ba1958f616ce3bc649042e690ccd88c7393096d01d98ea6bfdc14aaf109a` | 1.285403848 |
| fixture + group {1,2} at **vca=1** | this branch | `8730ba1958f616ce3bc649042e690ccd88c7393096d01d98ea6bfdc14aaf109a` | 1.285403848 |
| fixture + group {1,2} at **vca=0.5** (sensitivity control) | this branch | `3d26f63ae538ecd9daffe69f48d7ba53389b50b57787e66817c64c4031e8981c` | 0.642701924 |

The first three are **byte-identical**. The fourth **must** differ and does, and it differs in the
only way it is allowed to: `max|half − 0.5·base| = 0.0` over all 453,120 samples and the peak is
exactly half (`1.285403848 / 2 = 0.642701924`), i.e. −6.0206 dB. Commands:

```bash
python3 tests/data/vca-inject-group.py tests/data/vca-render-fixture.mmp /tmp/g1.mmp 1   1 2
python3 tests/data/vca-inject-group.py tests/data/vca-render-fixture.mmp /tmp/g2.mmp 0.5 1 2
QT_QPA_PLATFORM=offscreen ./build/lmms render tests/data/vca-render-fixture.mmp -f wav -a -s 44100 -o base.wav
# …same for /tmp/g1.mmp and /tmp/g2.mmp, then sha256 the RIFF `data` chunk (not the container)
```

### 5.4 Gates

```
$ bash tests/run-all-gates.sh --no-mutation     → exit 0
  gate 1 ctest PASS · gate 2 coverage SKIP · gate 3 no-tautology PASS
  gate 4 complexity PASS · gate 5 mutation SKIP · gate 6 upstream-regression PASS
  gate 7 file-length PASS · gate 8 duplication PASS
  RESULT: PASS — every executed gate passed
$ bash tests/no-upstream-regression-gate.sh     → exit 0
  PASS: every change to upstream-inherited code since 01148947e… is declared (31 files in the ledger)

$ (unpiped) bash tools/local-ci.sh … → configure 0, build 0, ctest 0
```

**Deviation, stated plainly.** The brief expected the third gate's exit code to be **3**
(`PASS-WITH-SKIPS`). This worktree does not carry that encoding: `tests/run-all-gates.sh` here is the
pre-fix revision that records a skipped gate as `SKIP` and still exits **0**. The sibling lanes
(and the `post-alpha/gate-debt` branch) have the fixed runner that separates `0` from `3`; porting
it is that lane's deliverable, not this one's, so this branch reports `0, 0, 0` where the newer
runner reports `0, 0, 3` for the identical gate results (2 of 8 gates skipped: coverage needs
`--with-coverage`, mutation was skipped by the flag).

Registry work in this change: `include/VcaGroup.h` and `src/core/VcaGroup.cpp` are fork-NEW and are
registered in `tests/fork-sources.txt`; the two upstream files touched (`src/core/Mixer.cpp`,
`include/Mixer.h`) carry reasons read off the diff in `tests/upstream-modifications.txt`. The test
file lives under `tests/`, which `fork-sources.txt` deliberately does not cover; it is checked by
Gate 3 instead, which parses `set(LMMS_TESTS …)`.

---

## 6. What is NOT done

- **No GUI at all.** No VCA strip in `MixerView`, no fader widget, no "group selected channels",
  no undo action. Reachability is limited to project-file authoring and the C++ API (§4). This is
  the single largest remaining piece, and it is what "a user cannot reach it" means for this
  feature today.
- **No scripting surface.** `src/core/ScriptBindings.cpp` has no mixer object, so a Lua script
  cannot create or move a group.
- **The VCA is not sample-exact / automatable.** The group gain is read as a plain scalar once per
  block; a `<vca>` automation clip on the group fader would be interpolated by nothing — the model
  round-trips and saves, but the audio path reads `value()`, not `valueBuffer()`. Channel faders
  have sample-exact support; the group fader does not.
- **No group nesting, no multi-group membership, no group-of-groups.** One channel, one group.
- **The VCA scales a member's whole output, including its pre-fader sends.** A member's
  pre-fader (bus) send is scaled by the group gain, because the multiply sits on the shared
  post-FX buffer. This is stated rather than fixed: making pre-fader sends VCA-independent needs a
  second tap point and is a larger change than Bar 2 asks for.
- **No `deleteChannel`/`moveChannelLeft` interaction test for the group's *audio*** — the index
  bookkeeping is tested (member 6 → 5 after deleting channel 4), but not a render after a move.
- **The Windows/macOS jobs are CI-only.** Only linux-x86_64 was built here.

## 7. One harness finding worth keeping

While proving the above, every level comparison went wrong in a way that looked like a mixer bug and
was not: after a programmatic `setValue()` on a channel fader or a send amount, the **next render
uses a one-period interpolation ramp** instead of the settled value. `AutomatableModel::valueBuffer()`
(`src/core/AutomatableModel.cpp:527`) hands out that ramp and **caches** it until
`AutomatableModel::incrementPeriodCounter()` ticks — a tick only the real `AudioEngine` performs
(`src/core/AudioEngine.cpp:316`). Every one of these Phase-D-style harnesses drives
`Mixer::masterMix()` directly, so the counter never moves, the ramp is frozen, and a post-render
`setValue()` never reaches the audio at all. `tests/src/core/VcaGroupTest.cpp` now settles the
machinery explicitly before each measurement (`renderFeed`), and a dedicated control
(`levelsDoNotDependOnThePrecedingRender`) pins it. **The VCA path is immune by construction** — its
gain is a plain float published on the control thread, with no `ValueBuffer` anywhere — which is why
every VCA scaling assertion is bit-exact while the fader/send ones needed the harness fixed first.
Worth knowing for any future direct-drive mixer harness, in this repo or another.
