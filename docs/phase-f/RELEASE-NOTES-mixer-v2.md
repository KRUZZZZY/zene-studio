# Release note — mixer v2 project format (dynamic routing)

> **Historical program artifact.** Written inside the FL-program worktree
> `lmms-partd` (branch `part-d-sidechain`) and kept for provenance. It is not a
> product document; treat the branch names as history.

Status: implemented in `part-d-sidechain` (Phase D, task #587) and hardened in Phase F (task #592).
Audience: LMMS users, packagers, and downstream readers of `.mmp` / `.mmpz` project files.

## TL;DR

- The mixer can now save/load **parallel bus channels**, **pre-fader sends** and **sidechain sends**.
- **No project-version bump and no migration step.** The new data is additive: an old project file
  loads byte-for-byte the same routing it always had, and a new file still loads in old LMMS builds.
- Old LMMS builds **silently drop** the new routing when they open a new file. If you re-save in an
  old build, that routing is gone permanently — keep a copy or stay on the new build for v2 projects.

## Related: plugin delay compensation (#605, landed 2026-09-10)

The mixer now compensates for plugin latency, so a latent effect chain sums in
phase instead of smearing against its parallel paths:

- `EffectChain::refreshLatency()` caches chain latency; the mixer's
  `updateLatencyCompensation()` recomputes every channel's input/output latency and the
  per-edge compensation delays once per audio period (audio thread, allocation-free,
  lock-free, O(channels + edges)). Scratch storage is sized on the control thread.
- Every incoming path is delayed to its receiver's alignment point; the compensation
  delay lines are touched only by the receiving worker.
- A zero-latency graph is **bit-identical** to the pre-PDC path
  (`PdcMixerTest::zeroLatencyGraphIsBitIdentical`), and the compensated null shows
  dry + inverted wet cancelling only when latency is reported
  (`PdcMixerTest::parallelNullCancelsOnlyWhenLatencyIsReported`; it emits `PDC_NULL`
  evidence with the residual dBFS). Above the delay-line cap the compensation is clamped
  and diagnosed rather than silently dropped (`latencyAboveTheCapIsClampedAndDiagnosed`).
- **No project-format change**: PDC is a runtime behaviour, so `.mmp`/`.mmpz` files are
  unaffected and no version bump is required.

## 1. What changes in the XML

The mixer lives in `<song><mixer>…</mixer></song>`. Channels are still `<mixerchannel num="N">`;
the v2 additions are one optional attribute and two optional child elements.

### Legacy (unchanged, still written and still accepted)

```xml
<mixerchannel num="1" name="Kick">
  <volume type="float" value="1.5" min="0" max="2" mid="1"/>
  <muted type="bool" value="0"/>
  <soloed type="bool" value="0"/>
  <fxchain>…</fxchain>
  <send channel="0" amount="0.75"/>
</mixerchannel>
```

### v2 additions

```xml
<!-- a parallel bus channel: new optional <bus> child; sources is informational -->
<mixerchannel num="12" name="Drum Bus">
  <volume …/><muted …/><soloed …/><fxchain>…</fxchain>
  <bus sources="5,6,7"/>
  <send channel="0" amount="1"/>
</mixerchannel>

<!-- a pre-fader send: new optional prefader="1" attribute on <send> -->
<mixerchannel num="5" name="Snare">
  …
  <send channel="12" prefader="1" amount="1"/>
</mixerchannel>

<!-- a sidechain send: new <sidechain-send> element -->
<mixerchannel num="13" name="Duck Target">
  …
  <sidechain-send channel="12" mode="2" amount="1"/>
</mixerchannel>
```

| Element / attribute | Meaning | Notes |
|---|---|---|
| `<bus sources="…"/>` | marks the channel as a parallel bus | `sources` is a human-readable list of sender indices; the loader only checks for the element's presence (routing is rebuilt from the `<send>` elements) |
| `<send … prefader="1">` | send taps the sender **before** its volume multiply | absent attribute = post-fader; sends **to a bus** are pre-fader by default and are saved with `prefader="1"` |
| `<sidechain-send channel="M" mode="K" amount="A"/>` | sidechain (key) send to channel `M` | `mode`: `0` PostFader, `1` PreFx, `2` PreFader, `3` PostFaderNoGain (unknown values load as `2` PreFader) |

Amounts use the normal `AutomatableModel` serialization: a plain `amount="…"` attribute when the
value is not automated, or a nested node when it is. Nothing about existing elements changed.

## 2. Versioning: there is no "mixer version 2" marker

- The project file carries one format version: `<lmms-project version="N" …>`, where `N` is the
  `DataFile` upgrade-chain length (`UPGRADE_METHODS.size()`, currently **31**). The mixer has no
  separate version attribute, and the v2 elements are **not** tied to an upgrade method.
- Consequence: a v2 file is a valid legacy file plus optional elements. Old builds upgrade it
  through the normal chain; new builds load old files without any migration.
- Therefore "mixer v2" in this document means *the mixer XML vocabulary that understands buses,
  pre-fader sends and sidechain sends* — it is detected by the presence of those elements, not by a
  version number.

## 3. Backward compatibility (old project → new build): PROVEN

Evidence: `tests/src/core/MixerRoutingBackwardCompatTest.cpp` (runs in ctest).

| Test slot | What it proves |
|---|---|
| `legacyProjectLoadsWithIdenticalRouting` | a legacy fixture (no `<bus>`, no `<sidechain-send>`, no `prefader`) loads with an **exact** expected routing table (`ch1 send->0 amount=0.75 prefader=0`, …), renders audio identical to a programmatically built graph, and re-saving emits **no** `<bus>`/`sidechain` elements |
| `phaseDProjectRoundTripsThroughSaveLoad` | a v2 graph (bus + pre-fader + sidechain) saves and loads with identical routing |
| `unknownLegacyEffectIsPreservedAsDummy` | an effect type unknown to this build is preserved as a dummy rather than dropped |

Missing elements default to the legacy behaviour: no `<bus>` ⇒ regular channel, no `prefader` ⇒
post-fader, no `<sidechain-send>` ⇒ no key route. Legacy channel counts and names are untouched.

## 4. Forward compatibility (new file → old build): source-level analysis

Checked against the program base `4e677cb6c` (`git show 4e677cb6c:src/core/Mixer.cpp`): the loader
recognizes only `mixerchannel` and `send` nodes. It ignores any other child element (`<bus>`,
`<sidechain-send>`) and any unknown attribute (`prefader`). The save path never wrote them.

| v2 feature | What an old build does | User-visible effect |
|---|---|---|
| `<bus>` | loads the channel as a regular channel | the bus still receives its sends, but its incoming sends are treated as post-fader, and the channel now accepts instrument/audio input directly (`mixToChannel` no longer skips it) |
| `prefader="1"` | attribute ignored ⇒ post-fader send | moving the sender's fader now changes the send level |
| `<sidechain-send>` | element ignored ⇒ route dropped | no ducking/keying; the receiver plays without sidechain |
| `amount`/channel/name/volume/… | unchanged | routing and levels preserved |

There is no automated test for this direction (it requires running an old LMMS build); the table is
a source-level reading of the legacy loader, and the code comments in `Mixer::saveSettings`
("A legacy LMMS ignores this element and loads the channel as a regular channel, which is the
intended forward-compatible degradation") state the same intent.

## 5. What a user must know

1. **Saving a project in a new build does not change the format version**, so your files stay
   loadable in older builds — but the new routing is not understood by them.
2. **Do not re-save a v2 project in an old build** unless you accept losing bus/pre-fader/sidechain
   routing. The old build will write back only what it understood.
3. **Channel counts above 64 are fine.** Channels are allocated on demand (`allocateChannelsTo`), so
   a project with more than 64 mixer channels loads in both old and new builds. (The "fixed 64"
   limitation in the rejected upstream design was architectural, not a constant in this code.)
4. **A channel with no `<send>` to master is silent by design.** The loader removes the default
   master send when it allocates channels and then rebuilds exactly the sends stored in the file.
5. Sidechain sends carry audio only to the key input; they are not audible routes. They can be
   saved, copied between projects, and removed like normal sends.

## 6. How to verify

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-partd
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R MixerRoutingBackwardCompat --output-on-failure
```
