#!/usr/bin/env python3
"""Generate the loudness-report render fixtures.

The evidence for docs/LUFS-WIRING.md needs real renders of signals whose
loudness is known independently of Zene Studio. This script writes:

  tone-23.wav     1 kHz stereo sine, each channel's peak at -23 dBFS
                  (EBU Tech 3341 case 1 -> -23.0 LUFS)
  tone-33.wav     the same signal 10 dB quieter (case 2 -> -33.0 LUFS)
  silent.wav      digital silence (every sample exactly 0.0 -> -inf LUFS)
  tone-23.mmp     a project that plays tone-23.wav through
  tone-33.mmp              AudioFileProcessor with the mixer at unity
  silent.mmp               (so the render is the fixture, unchanged)

The signals are written as 32-bit float WAV, so the sample values are the
generated values: no quantisation, no dither, nothing to explain away.

The .mmp files are templated on tests/emptyproject.mmp (same tree, same
schema) with a single AudioFileProcessor track and one note long enough to
play the whole sample.

Usage:  python3 tests/data/loudness/make-fixtures.py [output-directory]
        (default: the directory holding this script)
"""

import struct
import sys
from pathlib import Path

SAMPLE_RATE = 48000
SECONDS = 16.0
FREQUENCY_HZ = 1000.0
CHANNELS = 2
# The clip/note positions are in ticks: LMMS time is 192 ticks to a 4/4 bar,
# i.e. 48 ticks to a beat. A note that spans the whole sample keeps the tone
# playing end to end.
BPM = 140.0
TICKS_PER_BEAT = 48
TICKS = int(round(SECONDS / (60.0 / BPM) * TICKS_PER_BEAT))


def float32_wav_bytes(samples, sample_rate=SAMPLE_RATE, channels=CHANNELS):
    """Minimal IEEE-float32 WAV (format 3), interleaved."""
    frames = len(samples) // channels
    data = struct.pack("<%df" % len(samples), *samples)
    byte_rate = sample_rate * channels * 4
    block_align = channels * 4
    fmt_chunk = struct.pack(
        "<HHIIHH", 3, channels, sample_rate, byte_rate, block_align, 32)
    riff_size = 4 + (8 + len(fmt_chunk)) + (8 + len(data))
    return (b"RIFF" + struct.pack("<I", riff_size) + b"WAVE"
            + b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
            + b"data" + struct.pack("<I", len(data)) + data)


def sine(peak_dbfs, sample_rate=SAMPLE_RATE, seconds=SECONDS,
         frequency_hz=FREQUENCY_HZ, channels=CHANNELS):
    """1 kHz sine in phase in every channel, peak at peak_dbfs dBFS."""
    import math
    amplitude = 10.0 ** (peak_dbfs / 20.0)
    frames = int(seconds * sample_rate)
    out = []
    for i in range(frames):
        value = amplitude * math.sin(2.0 * math.pi * frequency_hz * i / sample_rate)
        out.extend([value] * channels)
    return out


def silence(sample_rate=SAMPLE_RATE, seconds=SECONDS, channels=CHANNELS):
    return [0.0] * (int(seconds * sample_rate) * channels)


PROJECT = """<?xml version="1.0"?>
<!DOCTYPE multimedia-project>
<multimedia-project version="1.0" creator="LMMS" creatorversion="1.2.0" type="song" >
  <head timesig_numerator="4" mastervol="100" timesig_denominator="4" bpm="140" masterpitch="0" />
  <song>
    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0" >
      <track muted="0" type="0" name="fadeout" >
        <instrumenttrack pan="0" mixch="0" pitch="0" basenote="57" vol="100" >
          <instrument name="audiofileprocessor" >
            <audiofileprocessor src="{src}" amp="100" loop="0" reverse="0" stutter="0" interpolation="0" endpoint="1" startpoint="0" />
          </instrument>
          <eldata fres="0.5" ftype="0" fcut="14000" fwet="0" >
            <elvol lspd_denominator="4" pdel="0" userwavefile="" dec="0.5" lamt="0" syncmode="0" latt="0" rel="0.1" sus="0.5" amt="0" x100="0" att="0" lpdel="0" hold="0.5" lshp="0" lspd="0.1" ctlenvamt="0" lspd_numerator="4" />
            <elcut lspd_denominator="4" pdel="0" userwavefile="" dec="0.5" lamt="0" syncmode="0" latt="0" rel="0.1" sus="0.5" amt="0" x100="0" att="0" lpdel="0" hold="0.5" lshp="0" lspd="0.1" ctlenvamt="0" lspd_numerator="4" />
            <elres lspd_denominator="4" pdel="0" userwavefile="" dec="0.5" lamt="0" syncmode="0" latt="0" rel="0.1" sus="0.5" amt="0" x100="0" att="0" lpdel="0" hold="0.5" lshp="0" lspd="0.1" ctlenvamt="0" lspd_numerator="4" />
          </eldata>
          <chordcreator chord="0" chordrange="1" chord-enabled="0" />
          <arpeggiator arptime="100" arprange="1" arptime_denominator="4" syncmode="0" arpmode="0" arp-enabled="0" arp="0" arptime_numerator="4" arpdir="0" arpgate="100" />
          <fxchain numofeffects="0" enabled="0" />
        </instrumenttrack>
        <pattern steps="16" muted="0" type="0" name="tone" pos="0" len="{steps}" frozen="0" >
          <note key="57" vol="100" pos="0" pan="0" len="{steps}"/>
        </pattern>
      </track>
    </trackcontainer>
    <track muted="0" type="6" name="Automation track" >
      <automationtrack/>
    </track>
  </song>
</multimedia-project>
"""


def write_project(path, wav_path):
    # An absolute sample path: Zene Studio resolves a bare file name against its
    # own working directory, not against the project.
    text = PROJECT.format(src=wav_path, steps=TICKS)
    path.write_text(text)


def main():
    out_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parent
    out_dir.mkdir(parents=True, exist_ok=True)

    fixtures = {
        "tone-23": sine(-23.0),
        "tone-33": sine(-33.0),
        "silent": silence(),
    }
    for name, samples in fixtures.items():
        wav = out_dir / (name + ".wav")
        wav.write_bytes(float32_wav_bytes(samples))
        write_project(out_dir / (name + ".mmp"), str(wav))
        print(f"{wav.name}: {len(samples) // CHANNELS} frames "
              f"{SAMPLE_RATE} Hz {CHANNELS}ch float32 -> {out_dir / (name + '.mmp')}")


if __name__ == "__main__":
    main()
