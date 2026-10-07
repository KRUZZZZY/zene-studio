# Cluster new-p8-79: saved MIDI channel numbering

**Verdict: FALSE-POSITIVE**

The finding assumes `MidiPort::inputChannel()` uses the engine's zero-based
channel index. It does not: selected port channels are 1-based, while `0`
means “any channel.” `MidiLearn::handleMidiEvent()` converts the incoming
zero-based event channel with `event.channel() + 1` (in
`src/core/MidiLearn.cpp:178-180`), and `bindMidiController()` stores that value
directly in the port (`src/core/MidiLearn.cpp:229-231`). The port's event filter
confirms the convention: `src/core/midi/MidiPort.cpp:159` compares
`inputChannel() - 1` to the zero-based event channel, with `0` handled as the
any-channel setting.

`ControllerSurface::currentBindings()` copies the port's 1-based value at
`src/core/ControllerSurface.cpp:253`. Applying a binding passes that same value
to `bindMidiController()` at `src/core/ControllerSurface.cpp:182`, so saving
and restoring do not add or lose a channel.

The existing `TemplatesSaveListReadAndApply()` case in
`tests/src/core/ControllerSurfaceTest.cpp:378-440` covers the path: a MIDI
event on channel index 5 is saved as channel 6, restored to port channel 6,
and reported by `currentBindings()` as channel 6. This test was not run in this
false-positive investigation.

No production change or new regression test was made. Per procedure step 2,
the RED/GREEN regression sequence is skipped when the finding is not real.

## Verification evidence

| Check | Exact command / result | Exit |
|---|---|---:|
| RED regression | Not run; false-positive finding (step 2 skips to reporting). | N/A |
| GREEN regression | Not run; there was no fix to verify. | N/A |
| Affected family | `ControllerSurfaceTest` not executed; no test count is claimed as passing. | N/A |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-79.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-79.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` | 0 |
| Log scan: `error` | `rg -in error /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-79.r1/logs` (no matches) | 1 |
| Log scan: `FAILED` | `rg -in FAILED /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-79.r1/logs` (no matches) | 1 |
| Log scan: `Assertion` | `rg -in Assertion /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-79.r1/logs` (no matches) | 1 |

The requested fff grep utility was not available in this session; `rg` was
used to scan the gate logs.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-79.md`
