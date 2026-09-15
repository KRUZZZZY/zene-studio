#!/usr/bin/env python3
"""The control for the golden-audio instrument: a known difference in, the same out.

A measurement programme is only as good as its measuring instrument, and this project has
already paid for assuming one: the comparator's job in this release is to say "this render
moved" and "this render did not", and it can only be trusted to say either if it has been
seen to say BOTH about inputs whose answer is known independently.

Every WAV here is SYNTHESISED in a temp directory by this file - no build, no engine, no
fixture - so the test runs in seconds anywhere and its inputs are exact:

  * two identical renders          -> 0 differing frames, 0 LSB, identical data chunks
  * one sample moved by one LSB    -> 1.000 LSB on 1 frame, and NO level term sees it
                                      (the recorded non-determinism's own shape)
  * two silent renders             -> no nan, and the level delta is undefined, not zero
  * a -0.5 dB gain                 -> a +0.5 dB level delta, ~900 LSB of sample delta
  * three renders 3 LSB apart      -> a 3 LSB floor, so a 6 LSB tolerance
  * THE NEGATIVE CONTROL           -> the -0.5 dB gain FAILS the comparison
  * ... and the bound              -> a 1-LSB change against a 3-LSB floor does NOT

Usage: python3 tests/golden_audio_selftest.py          (exit 0 only if every check held)
"""

import array
import math
import shutil
import struct
import sys
import tempfile

import golden_audio_lib as G


def synth(path, samples, gain=1.0):
    """A 16-bit mono WAV of `samples` at 44.1 kHz, scaled by `gain` (never clips)."""
    import wave
    data = array.array("h", (max(-32768, min(32767, int(round(v * gain * 32767))))
                             for v in samples))
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(44100)
        handle.writeframes(data.tobytes())
    return path


def nudge(path, samples_by):
    """Add `samples_by` to chosen samples of an existing 16-bit WAV, in place."""
    blob = bytearray(open(path, "rb").read())
    for offset_frames, amount in samples_by:
        offset = 44 + 2 * offset_frames
        struct.pack_into("<h", blob, offset,
                         struct.unpack_from("<h", blob, offset)[0] + amount)
    open(path, "wb").write(bytes(blob))
    return path


def main():
    tmp = tempfile.mkdtemp(prefix="golden-audio-selftest-")
    failures = []

    def check(name, ok, evidence):
        print("  %-4s %-62s %s" % ("PASS" if ok else "FAIL", name, evidence))
        if not ok:
            failures.append(name)

    try:
        rate = 44100
        frames = int(rate * 1.0)
        tone = [0.5 * math.sin(2.0 * math.pi * 440.0 * i / rate) for i in range(frames)]
        silence = [0.0] * frames
        ident = synth(tmp + "/ident.wav", tone)
        silence2 = synth(tmp + "/silence2.wav", silence)
        one_lsb = nudge(synth(tmp + "/one-lsb.wav", tone), [(1000, 1)])
        gain_half_db = synth(tmp + "/gain-half-db.wav", tone, 10.0 ** (-0.5 / 20.0))
        gain_2db = synth(tmp + "/gain-2db.wav", tone, 10.0 ** (-2.0 / 20.0))
        # A three-render floor whose pairs really do differ: 3 LSB on 5 samples each run.
        noisy = [nudge(synth(tmp + "/noisy-%d.wav" % index, tone),
                       [(100 * (index + 1) + sample, 3) for sample in range(5)])
                 for index in range(3)]

        same = G.compare(ident, synth(tmp + "/ident2.wav", tone))
        check("two identical renders: 0 differing frames, 0 LSB",
              same["differing_frames"] == 0 and same["max_delta_lsb"] == 0.0
              and same["identical_bytes"],
              "%d frames, %.3f LSB" % (same["differing_frames"], same["max_delta_lsb"]))

        one = G.compare(ident, one_lsb)
        check("a 1-LSB change on one sample is reported as 1.000 LSB on 1 frame",
              one["differing_frames"] == 1 and abs(one["max_delta_lsb"] - 1.0) < 1e-9,
              "%d frames, %.3f LSB" % (one["differing_frames"], one["max_delta_lsb"]))
        check("... the recorded non-determinism's shape: no LEVEL term sees it",
              abs(one["level_delta_db"]) < G.DB_FLOOR
              and one["envelope"]["max_delta_db"] < G.DB_FLOOR
              and not one["identical_bytes"],
              "level %+.9f dB, envelope %.9f dB - both under the %.2f dB resolution"
              % (one["level_delta_db"], one["envelope"]["max_delta_db"], G.DB_FLOOR))

        quiet = G.compare(synth(tmp + "/silence1.wav", silence), silence2)
        check("two silent renders: no divide-by-zero, no false difference, no nan",
              quiet["max_delta_lsb"] == 0.0 and quiet["level_delta_db"] is None,
              "%.3f LSB, level %r (undefined, not 0.0 and not nan)"
              % (quiet["max_delta_lsb"], quiet["level_delta_db"]))

        gain = G.compare(ident, gain_half_db)
        check("a -0.5 dB gain is reported as a +0.5 dB level delta",
              abs(gain["level_delta_db"] - 0.5) < 0.001 and gain["max_delta_lsb"] > 100.0,
              "level %+.6f dB, max |delta| %.0f LSB"
              % (gain["level_delta_db"], gain["max_delta_lsb"]))

        floor0 = G.measure_floor([ident, synth(tmp + "/ident3.wav", tone),
                                  synth(tmp + "/ident4.wav", tone)])
        check("the same-build floor of three identical renders is 0 LSB",
              floor0["max_delta_lsb"] == 0.0 and floor0["identical_bytes"],
              "%.3f LSB over %d pairs" % (floor0["max_delta_lsb"], floor0["pairs"]))

        tol0 = G.tolerance(floor0)
        quiet_ok, _ = G.verdict(quiet, tol0)
        check("... and an undefined level term cannot fail a verdict", quiet_ok is True,
              "verdict passed with the level term undefined")
        caught, _ = G.verdict(gain, tol0)
        check("THE NEGATIVE CONTROL: a deliberate -0.5 dB gain FAILS the comparison",
              caught is False, "tolerance %.3f LSB, measured %.0f LSB"
              % (tol0["lsb"], gain["max_delta_lsb"]))
        within, _ = G.verdict(one, tol0)
        check("... and a 1-LSB change is NOT distinguished (the resolution floor)",
              within is True, "tolerance %.3f LSB, measured %.3f LSB"
              % (tol0["lsb"], one["max_delta_lsb"]))

        noisy_floor = G.measure_floor(noisy)
        tol_noisy = G.tolerance(noisy_floor)
        check("three renders 3 LSB apart measure a 3 LSB floor",
              abs(noisy_floor["max_delta_lsb"] - 3.0) < 1e-9,
              "%.3f LSB over %d pairs" % (noisy_floor["max_delta_lsb"],
                                          noisy_floor["pairs"]))
        check("... so the tolerance is twice it (6 LSB): the model, not a magic number",
              abs(tol_noisy["lsb"] - 6.0) < 1e-9, "%.3f LSB" % tol_noisy["lsb"])
        below, _ = G.verdict(one, tol_noisy)
        check("a change below the floor is NOT caught - the programme's stated bound",
              below is True,
              "a 1-LSB change against the 3-LSB floor's 6-LSB tolerance: measured %.3f LSB"
              % one["max_delta_lsb"])
        big, _ = G.verdict(G.compare(ident, gain_2db), tol_noisy)
        check("... and a -2 dB change is caught even against the noisy floor",
              big is False, "tolerance %.0f LSB" % tol_noisy["lsb"])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("")
    if failures:
        print("RESULT: FAIL - %d check(s) did not hold: %s" % (len(failures), failures))
        return 1
    print("RESULT: PASS - the instrument reports known differences exactly, fails a "
          "deliberate gain change, and invents nothing between identical files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
