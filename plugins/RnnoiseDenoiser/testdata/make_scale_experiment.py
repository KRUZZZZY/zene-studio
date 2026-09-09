#!/usr/bin/env python3
"""Scale experiment for the RNNoise denoiser (AI-KOS #559 root-cause test).

Hypothesis: the plugin feeds LMMS's +/-1.0 floats straight into
rnnoise_process_frame(), which expects the CELT int16 scale (+/-32768).
RNNoise's silence gate (denoise.c: `if (!TRAINING && E < 0.04)`) then treats
every frame as silence and the neural denoiser never runs.

Test: write the same test signal as a 32-bit FLOAT WAV scaled by 32768
(LMMS's SampleDecoder uses sf_read_float with no normalisation, so the sample
plays at +/-10486 and the plugin sees RNNoise-scale input), then render it
through the same denoiser and see whether noise suppression appears.

Writes: speechlike_noise_2s_48k_x32768.wav
        rnnoise_test_D_float_nofx.mmp   (float sample, no FX = control)
        rnnoise_test_E_float_denoise.mmp (float sample + denoiser)
"""
import struct

TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
SR = 48000
SRC = f"{TD}/speechlike_noise_2s_48k.wav"
FLOAT = f"{TD}/speechlike_noise_2s_48k_x32768.wav"

# --- read the 16-bit source ---
raw = open(SRC, "rb").read()
pos, data = 12, None
while pos + 8 <= len(raw):
    cid, size = struct.unpack_from("<4sI", raw, pos)
    if cid == b"data":
        data = raw[pos + 8: pos + 8 + size]
    pos += 8 + size + (size & 1)
ints = struct.unpack("<%dh" % (len(data) // 2), data)
n = len(ints)

# --- write float32 WAV at x32768 (values = original int16 sample values) ---
floats = struct.pack("<%df" % n, *[float(v) for v in ints])
hdr = b"RIFF" + struct.pack("<I", 36 + len(floats)) + b"WAVE"
hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, SR, SR * 4, 4, 32)
hdr += b"data" + struct.pack("<I", len(floats))
open(FLOAT, "wb").write(hdr + floats)
print(f"wrote {FLOAT}: {n} frames float32, peak={max(abs(v) for v in ints)} (x32768 of +/-1.0 scale)")

# --- project templates ---
COMMON_HEAD = """<?xml version="1.0"?>
<!DOCTYPE multimedia-project>
<multimedia-project version="1.0" creator="LMMS" creatorversion="1.3.0" type="song">
  <head timesig_numerator="4" mastervol="100" timesig_denominator="4" bpm="120" masterpitch="0" />
  <song>
    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0">
      <track muted="0" type="2" name="DenoiseTest">
        <sampletrack vol="100" pan="0" mixch="0">
{fxchain}
        </sampletrack>
        <sampleclip pos="0" len="240" muted="0" src="{src}" off="0" autoresize="1" sample_rate="48000"/>
      </track>
    </trackcontainer>
    <mixer width="865" x="5" y="310" maximized="0" height="278" visible="1" minimized="0">
      <mixerchannel num="0" muted="0" volume="1" name="Master">
        <fxchain numofeffects="0" enabled="0"/>
      </mixerchannel>
    </mixer>
    <controllerrackview width="258" x="880" y="310" maximized="0" height="278" visible="1" minimized="0"/>
    <timeline lp1pos="192" lp0pos="0" lpstate="0"/>
    <controllers/>
  </song>
</multimedia-project>
"""
FX_ON = """          <fxchain numofeffects="1" enabled="1">
            <effect name="rnnoisedenoiser" on="1" wet="1" autoquit="1">
              <RnnoiseDenoiserControls/>
              <key/>
            </effect>
          </fxchain>"""
FX_OFF = """          <fxchain numofeffects="0" enabled="0"/>"""

open(f"{TD}/rnnoise_test_D_float_nofx.mmp", "w").write(
    COMMON_HEAD.format(fxchain=FX_OFF, src=FLOAT))
open(f"{TD}/rnnoise_test_E_float_denoise.mmp", "w").write(
    COMMON_HEAD.format(fxchain=FX_ON, src=FLOAT))
print("wrote rnnoise_test_D_float_nofx.mmp, rnnoise_test_E_float_denoise.mmp")
