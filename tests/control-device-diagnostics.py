#!/usr/bin/env python3
"""control-device-diagnostics.py - mixer.cpu_load's audio_device on JACK, headless (Q050-074.2).

Drives the REAL binary on the JACK backend against a private `jackd -d dummy` server at
48000 Hz with 256-frame periods (no hardware, nothing leaves this machine) and asserts,
through the socket, the reply an agent reads:

  1. audio_device names the JACK backend, running, reporting timing;
  2. the device's negotiated sample_rate is the server's 48000 Hz and its period_frames the
     server's 256, each {available: true, value, unit}; the engine's own block sits beside
     them (its output rate follows the device);
  3. buffer_frames is available:false with no value - JACK does not say how many periods it
     queues, and an unreported value is never shown as 0;
  4. input_latency and output_latency are available, in frames with ms and the JACK latency
     range they are read from;
  5. xruns (the server's xrun notifications) is an available count, and late_periods (the
     engine's own deadline misses) is a separate count in the same reply.

The forced xrun and late period are proven in-process (AudioDeviceDiagnosticsTest); the
device-less reply is control-cpu-diagnostics.py.

Skipped (exit 77, ctest SKIP_RETURN_CODE) when `jackd` is not installed or does not stay up.

Usage: QT_QPA_PLATFORM=offscreen python3 control-device-diagnostics.py <zene-binary>
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import control_socket_harness as H  # noqa: E402

JACK_DEVICE = "JACK (JACK Audio Connection Kit)"
SKIP = 77


def reading(device, key):
    value = device.get(key)
    return value if isinstance(value, dict) else {}


def check_rates(device, problems):
    rate, period = reading(device, "sample_rate"), reading(device, "period_frames")
    problems.require(rate.get("available") is True and rate.get("value") == 48000 and rate.get("unit") == "Hz",
                     "the device rate is the server's 48000 Hz: %r" % rate)
    problems.require(period.get("available") is True and period.get("value") == 256 and period.get("unit") == "frames",
                     "the device period is the server's 256 frames: %r" % period)
    engine = device.get("engine") or {}
    problems.require(engine.get("output_sample_rate_hz") == 48000, "the engine renders at the device's rate: %r" % engine)
    problems.require(isinstance(engine.get("sample_rate_hz"), (int, float)) and isinstance(engine.get("period_frames"), (int, float)),
                     "the engine's own rate and period are numbers: %r" % engine)
    buffer = reading(device, "buffer_frames")
    problems.require(buffer.get("available") is False and "value" not in buffer,
                     "JACK's buffer is unavailable, not zero: %r" % buffer)


def check_latency(device, problems):
    for key, basis in (("input_latency", "jack_capture_latency_range"), ("output_latency", "jack_playback_latency_range")):
        latency = reading(device, key)
        problems.require(latency.get("available") is True and isinstance(latency.get("value"), (int, float))
                         and latency.get("unit") == "frames" and isinstance(latency.get("ms"), (int, float))
                         and latency.get("basis") == basis, "%s is reported with its unit and basis: %r" % (key, latency))
        if isinstance(latency.get("value"), (int, float)) and isinstance(latency.get("ms"), (int, float)):
            problems.require(abs(latency["ms"] - 1000.0 * latency["value"] / 48000.0) < 1e-6,
                             "%s ms is its frames at 48 kHz: %r" % (key, latency))


def run(binary, server, problems):
    transcript = H.Transcript()
    env = {"JACK_DEFAULT_SERVER": server, "JACK_NO_AUDIO_RESERVATION": "1", "JACK_NO_START_SERVER": "1"}
    with H.start_instance(binary, audiodev=JACK_DEVICE, extra_env=env) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        time.sleep(0.5)
        load = H.ok_result(client.call(1, "mixer.cpu_load", {}, transcript=transcript), 1)
        device = load.get("audio_device") or {}
        print("audio_device on JACK: %r" % device)
        problems.require(device.get("backend") == JACK_DEVICE, "the backend is JACK: %r" % device.get("backend"))
        problems.require(device.get("running") is True and device.get("reports_timing") is True,
                         "JACK is running and reports timing: %r" % device)
        check_rates(device, problems)
        check_latency(device, problems)
        xruns = reading(device, "xruns")
        problems.require(xruns.get("available") is True and isinstance(xruns.get("value"), (int, float))
                         and xruns.get("unit") == "count", "xruns is an available count: %r" % xruns)
        late = load.get("late_periods") or {}
        problems.require(isinstance(late.get("count"), (int, float)), "late_periods is its own count: %r" % late)
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    jackd = shutil.which("jackd")
    if jackd is None:
        print("SKIP: jackd is not installed - the JACK device readings cannot be exercised here")
        return SKIP
    server = "zene-device-diag-%d" % os.getpid()
    env = dict(os.environ, JACK_DEFAULT_SERVER=server, JACK_NO_AUDIO_RESERVATION="1")
    daemon = subprocess.Popen([jackd, "--no-realtime", "-n", server, "-d", "dummy", "-r", "48000", "-p", "256",
                               "-C", "2", "-P", "2"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    problems = H.Problems()
    try:
        time.sleep(1.5)
        if daemon.poll() is not None:
            print("SKIP: jackd -d dummy did not stay up (exit %s)" % daemon.returncode)
            return SKIP
        run(os.path.abspath(argv[1]), server, problems)
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
    return H.finish([("audio_device on a private JACK server", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
