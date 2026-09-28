# Capture limits and backend reach — lane `040/feat-capture-limits`

**Worktree:** `zene-capture`. **Base:** `0.4.0/train-w1` @ `042abe489`.
**Owner decision (2026-09-28):** bounded hardening. The internal stereo mix bus STAYS stereo.

## Why the bus stays stereo, recorded so it is not re-litigated

The owner asked for "everything up to industry standard", including the internal mix engine. The
evidence put the two halves in different places, and the owner chose accordingly:

| Axis | Zene (base) | Reference / pro tier | Verdict |
| --- | --- | --- | --- |
| Mixer channels, tracks | unbounded (`std::vector`, no cap) | Reaper/Cubase unlimited | **at standard** |
| Mix bus width | stereo (`SampleFrame`) | **Ableton Live is stereo-only** — "Ableton works only in stereo"; "Ableton has no surround option… the outputs are limited to mono or stereo" (Ableton forums) | **at standard for a Live-class product** |
| Capture channels | 32 | 256 physical I/O (Cubase Pro; Pro Tools \| Ultimate) | **below tier** |
| Simultaneous record routes | 16 | hundreds of simultaneous tracks | **below tier** |

So the bus was not the gap; the input side was. Going N-channel would also be a spec change —
`SPEC-zene-studio.md` §6 lists **surround panning** as an explicit program non-goal — and an
architectural one: `include/SampleFrame.h:2` is *"Representation of a stereo sample"*, used in **123
files** plus the remote-plugin ABI. That is a wave, not a hardening pass, and it is deliberately not
in this lane.

## The four items

1. **Capture channels 32 → 128.** `AudioInputPath::MaxChannels`. 128 is not arbitrary: it is the
   engine's *own* per-buffer ceiling, `MaxChannelsPerAudioBuffer` — `AudioBuffer.cpp:41` asserts
   against it and `AudioPortsModel.cpp:236` parses its `inputs`/`outputs` pins against it. The
   capture path can now address every channel the routing layer can name. Going further means
   raising that ceiling first, not this constant.
2. **Record routes 16 → 64.** `MultiTrackRecorder::MaxRoutes`. This, not the input count, was the
   real multitrack ceiling: a 24-channel interface could be *captured* but only 16 of its channels
   written to files at once. Cost is pre-allocated rings, paid once off the audio thread (~4 MiB at
   16 → ~16 MiB at 64).
3. **JACK wired into the capture path** — see §"Still open" below.
4. **The stale `AGENTS.md` capture claim corrected.** It read *"LMMS's ALSA backend has no capture
   path … ALSA PCM capture absent — relevant to recording work"* (audit 2026-09-19). That audit
   predates the multi-input recording work and is now false in a way that misleads anyone scoping
   recording features. Replaced with the verified state and the still-true limitation.

## The real finding: the capture path is ALSA-only

`publishCaptured()` exists in exactly one place — `src/core/audio/AudioAlsa.cpp:545`, called only
from the ALSA capture thread (`:632`). `AudioInputPath`, the wide-input stage and the record routes
are all fed from there and nowhere else. JACK registers genuine multichannel input ports
(`AudioJack.cpp:217-218`, `m_inputPorts`, `resizeInputBuffer`), and PortAudio/SDL each open their own
input device, but none of them publish into the input path the recorder reads. On JACK — the
professional Linux backend, natively multichannel — the modern recorder is therefore blind.

## Blast radius, measured (not estimated)

`MaxChannels` is read by `AudioInputPath.cpp:48,103-106` (clamp, validate),
`ControlCommandsRecordingInput.cpp:222,226-228` (description + schema bounds) and
`ControlCommandsRecordingRoutes.cpp:322` (`input_channel` bound). `MaxRoutes` is read by
`AudioInputPath::recordRouteCapacity()` (`AudioInputPath.cpp:146`) and passed to the recorder at
`AudioEngine.cpp:89`. Both feed the wide-input stage width (`AudioEngine.cpp:126`).

The bound is substituted into command schemas and descriptions at runtime (`.arg(MaxChannels)`), so
the agent surface follows automatically. No fixed-size array is involved anywhere (`grep` for
`[32]`/`[16]` in the capture files: none), so the change is a constant plus its documentation.

**Tests are written relative to the constants**, so raising them cannot silently weaken a test:
`RecordingInputPathTest.cpp:296-313` uses `MaxChannels + 1` and compares
`recordRouteCapacity()` to `MultiTrackRecorder::MaxRoutes`. The one genuine coupling is the Python
driver's own copy — `tests/control-record-inputs.py:89` `ROUTE_COUNT`, plus its two "sixteen
routes" strings — which is updated here.

Deliberately **not** rewritten: `docs/RELEASE-NOTES-v0.3.0-alpha.md:2096` and
`docs/CAPABILITIES-0.3.0.md:1124` state 16 routes. Those are version-stamped records of what
0.3.0-alpha shipped, and 0.3.0-alpha did ship 16. Rewriting them would falsify history.

## Verification (2026-09-28) — the bound raises are proven, not just written

Built targeted (`zene` + the recording test targets, 274 MB binary) and driven through the **real
binary over the control socket**, which is how this program proves agent-surface behaviour.

**`tests/control-record-inputs.py` — the project's own driver, exit 0:**

```
instance A's engine prepared the default route count             ok
instance B's engine prepared sixty-four routes                   ok
PASS: recording engine surface control-surface transcript (every check held)
```

**Positive/negative control on the channel cap** (a throwaway probe run from `/tmp`, so it is not
part of the repo): a bound is only proven when the value *at* the limit is accepted **and** the value
past it is refused with a typed error, never silently clamped.

```
route_count                 = 64   (expect 64)
POSITIVE channels=128       = {"channels": 128, ..., "route_capacity": 64, "ok": true}
NEGATIVE channels=129       = {"error": {"kind": "invalid_args",
                               "message": "channels: 129 is above the maximum 128"}, "ok": false}
PASS: 128 accepted, 129 refused, 64 routes
```

`record.input_get_state` reports `capture_capable: false` on this box (no capture device under the
headless Dummy backend) — which is the documented, expected state, not a regression: the
real-interface half stays hardware-bound and unverified here, exactly as `docs/RECORD-INPUTS.md`
§4 already states.

**Unit tests** (`ctest -R "RecordingInputPath|MultiTrackRecorder|RecordingRealtime|AudioEngineTeardown"`,
from `build/tests`, `QT_QPA_PLATFORM=offscreen`, `LMMS_PLUGIN_DIR=<build>/plugins`): **4/4 passed,
exit 0**. Note these are written *relative* to the constants (`MaxChannels + 1`,
`recordRouteCapacity() == MaxRoutes`), so their passing alone would NOT have proven the new values —
which is why the probes above exist.

## Still open on this lane

- **JACK wiring (item 3).** Needs `AudioJack`'s process callback to publish its captured frames into
  `AudioInputPath`/the wide-input stage, in the shape `AudioAlsa::publishCaptured` already uses, and
  a `publishOpen` naming the granted port count. This is the item that makes capture real on the pro
  Linux backend, and it is the only piece of this lane that is a behaviour change rather than a bound
  raise — so it lands as its own commit with its own evidence.
- **Full `ctest` and the gates** before handover (the targeted run above covers this lane's own
  surface, not the whole suite).
