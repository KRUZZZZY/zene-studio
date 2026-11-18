# Multichannel WAV export of a surround master (0.5.0 F7, MX-060)

Surround mixing landed in wave 2 (`009f591b2`): LCR / quad / 5.1 / 7.1 channels, the 2-D panner and the
ITU-R BS.775 downmix. Its export was cut then: the renderer wrote stereo, folding a surround master.
This is the export: a surround master can be written as it is, one WAV channel per speaker.

**Stereo stays the default, and is unchanged.** A render that does not ask for surround takes exactly the
path it took before; the stereo renders of a 5.1 fixture are byte-identical to the ones the base binary
made (16- and 24-bit; 32-bit float sample-identical, see "Proving it").

## What a surround export writes

| Master | Channels | Order (the mixer's own) | `dwChannelMask` |
|---|---|---|---|
| LCR | 3 | L R C | `0x7` |
| quad | 4 | L R Ls Rs | `0x33` (Ls/Rs as the back pair) |
| 5.1 | 6 | L R C LFE Ls Rs | `0x3F` (Ls/Rs as the back pair, `KSAUDIO_SPEAKER_5POINT1`) |
| 7.1 | 8 | L R C LFE Lrs Rrs Ls Rs | `0x63F` (Lrs/Rrs back, Ls/Rs side) |

- The file is `WAVE_FORMAT_EXTENSIBLE`. libsndfile 1.2.2 was measured: `SF_FORMAT_WAV` with more than two
  channels writes a plain `WAVE_FORMAT_PCM` header with no mask, so the encoder opens `SF_FORMAT_WAVEX`
  and sets the speakers with `SFC_SET_CHANNEL_MAP_INFO` before any audio. The map is always set: without
  it libsndfile writes mask `0x0` for three channels. `SFC_WAVEX_SET_AMBISONIC` is not used. This is a
  speaker-feed file, not B-format, and libsndfile's default (`SF_AMBISONIC_NONE`) is what that needs.
- 16-bit and 24-bit integer, 32-bit float, as for stereo. The dither (TPDF, and noise-shaped TPDF at
  16-bit) applies per channel, as it does to the stereo pair.
- The channels are the master after its fader and the engine's master gain, the same gains the pair gets.

## Choosing it

| Where | How |
|---|---|
| Export dialog | the WAV page's **Channels** row: *Stereo* (default) or *Surround 5.1 (6 channels)*. It offers Surround only for a surround master and a whole-project export. Choosing it turns the loudness report off. |
| CLI | `zene render song.mmpz -o out.wav --channels surround` (`stereo` is the default) |
| Agent surface | `render.render {"out": "/abs/out.wav", "channels": "surround"}`. The reply adds `channels` (6) and `channel_layout` ("5.1"). |

`channels` is additive: a caller that does not send it gets the stereo render it always got, and the reply's
two new fields say `2` / `"stereo"`.

## What is refused, and why

Each refusal names its reason and opens no file. `render.render` refuses `invalid_args` in-process,
before the session is serialised or a child spawned. The CLI exits non-zero. The dialog shows the reason
and does not start.

- **A stereo master:** there are no extra channels to write.
- **FLAC, OGG, MP3:** only WAV carries it here. Those formats export stereo, with the downmix.
- **The loudness report:** the EBU R128 meter measures a stereo pair, and a report of the pair alone
  would misstate a surround mix.
- **`rendertracks`, `exportstems`, `master` (CLI):** a surround export is one whole-project file.

Not offered at all: the render queue (`render.queue_add`), `render.stems` and export presets keep their
stereo behaviour and have no `channels` choice (docs/KNOWN-LIMITATIONS.md, F7).

## How it works

- `OutputSettings::ChannelMode` (`Stereo`, `Surround`) is the choice. It is a field with a default, so
  every existing construction is untouched.
- `include/SurroundExport.h` / `src/core/SurroundExport.cpp` hold the names, the refusal rule, the
  channel maps and masks, and the **tap**.
- The master's extra speakers live in its side buffer (`ChannelSurround`), which the master mix clears at
  the end of every period. While a surround export runs (`surroundexport::armed()` and `Song::isExporting()`),
  `surround::masterOutput` copies them, at the master fader and the master gain, into the tap instead of
  folding them into the pair. The tap has two sides that flip once per master mix, so the renderer reads
  the period that matches the stereo buffer `renderNextPeriod()` hands it. That buffer is the previous
  period's, the engine's one-period output offset.
- `ProjectRenderer` decides the channel count, refuses what cannot be carried, allocates the tap and an
  interleaving buffer on the control thread before the render, and frees them after. Per period it
  interleaves the pair and the tap into that buffer and calls `AudioFileDevice::writeInterleaved`. Only
  `AudioFileWave` implements it. The stereo path still calls `writeBuffer`, unchanged.
- Realtime (AGENTS.md rule 4): on the period path the tap does one atomic load, one `fill_n` of storage
  allocated before the render, and per-sample copies. It takes no lock. `SurroundExportTest` counts zero
  allocations in a master mix that feeds it.

## Proving it

```
cd <build>/tests
QT_QPA_PLATFORM=offscreen ctest -R '^(SurroundExportTest|ExportChannelsTest|ControlRenderSurround)$' --output-on-failure
QT_QPA_PLATFORM=offscreen ./SurroundExportTest    # prints the stereo renders' hashes (STEREO ... lines)
```

- `tests/src/core/SurroundExportTest.cpp` uses a real project (`SurroundExportTestSupport.h`). It has
  one sample track per source, each a sine at its own frequency, panned onto one speaker of a 5.1 master.
  The LFE source goes to the centre with its LFE send at 1, and a negated copy cancels it on the centre.
  The test measures each channel's level at every source's frequency over a window of whole cycles. It
  covers:
  - at 16, 24 and 32-bit, a 6-channel file with mask `0x3F` and every source on its own speaker,
    at least 60 dB down on every other channel;
  - the stereo export equals the BS.775 fold of the surround one, frame for frame;
  - 7.1 (`0x63F`, back and side pairs apart), LCR (`0x7`) and quad (`0x33`);
  - the refusals;
  - zero allocations in the tapped master mix, and the one-period offset.
- `tests/src/gui/ExportChannelsTest.cpp`: the dialog's Channels row.
- `tests/control-render-surround.py` (`ControlRenderSurround`) runs the real binary. It covers the
  advertised schema, the typed refusals, a 6-channel render through `render.render` (which runs the CLI
  with `--channels surround`), and the CLI's own refusals.

**Byte-identity of the stereo export.** The test's `STEREO` lines were recorded on the base binary
(`6d1a1143d`) and on this lane's binary, for the same fixture:

| Depth | File SHA-256 (base = lane) | Samples SHA-256 (base = lane) |
|---|---|---|
| 16-bit | `e4b11ce1b4acb5f5e12c184e490a1cea9f6a1176e796ed9a4e4f8dded0420323` | — |
| 24-bit | `add51370be4d6bcd43cf577b6aacc66e7f4b3854426340873ba38b322e1203aa` | `720f624a…6f8b81` |
| 32-bit float | differs run to run on the base binary too | `44a3e32e4bd86b0385657670b39f7fbca4a36c8f15179c99088cc1e36721cf2f` |

The 32-bit file's bytes are not reproducible even on the base binary. libsndfile writes a `PEAK` chunk
with a timestamp into float WAVs. Its samples are identical.
