#!/usr/bin/env python3
"""midi_reconnect_probe - an EXTERNAL ALSA-sequencer client, for the
midi.reconnect_* proof (tests/control-midi-reconnect.py).

This process is a controller-shaped device: it opens the ALSA sequencer, takes
a client NAME and a port NAME, and offers one READABLE port - exactly what a
USB keyboard offers and exactly what `MidiAlsaSeq::readablePorts()` lists, since
the engine lists the ports whose capabilities include READ and SUBSCRIBE-READ.

It exists because the obvious tool cannot do this job. `aplaymidi` REQUIRES
`--port` (measured: exit 1, "Please specify at least one port with --port"), and
`--port` addresses the destination DIRECTLY, which delivers the events without
any subscription - so it can never show whether the engine's SUBSCRIPTION is
live. This probe sends with `snd_seq_ev_set_subs()`, i.e. to whatever has
subscribed to it: if the engine's binding is gone, the kernel drops the event
and the engine receives nothing.

Everything it sends is a real ALSA-sequencer event from a real separate client
process, named by the caller, so the engine cannot tell it from hardware.

Usage (driven by the ctest; the file protocol exists so the test can order
"the engine attached" BEFORE "the controller plays"):

    midi_reconnect_probe.py --name CNAME --port PNAME --ready-file F
                           [--burst-file F] [--hold-ms MS]

  * <ready-file> receives "<client id> <port id>" once the port exists;
  * <burst-file>, when the caller writes "go:<n>" into it, is answered by
    sending n note-on events to SUBSCRIBERS and then writing "done:<n>";
  * the process runs until it is KILLED (the test kills it by explicit PID -
    that is the device being unplugged), or until --hold-ms elapses.

Exit status: 0 when it ran and held as asked; 77 when this host cannot open an
ALSA sequencer at all (the caller reports a skip, never a pass); 2 on a usage
error.
"""

import argparse
import ctypes
import ctypes.util
import os
import sys
import time

# alsa-lib's sequencer API, by the values in <alsa/asoundlib.h> / <alsa/seq.h>.
SND_SEQ_OPEN_DUPLEX = 3
SND_SEQ_PORT_CAP_READ = 1 << 0
SND_SEQ_PORT_CAP_SUBS_READ = 1 << 5
SND_SEQ_PORT_TYPE_MIDI_GENERIC = 1 << 1
SND_SEQ_PORT_TYPE_APPLICATION = 1 << 20
SND_SEQ_EVENT_NOTEON = 6
# SND_SEQ_QUEUE_DIRECT: deliver now, do not schedule on a queue.
SND_SEQ_QUEUE_DIRECT = 253
SND_SEQ_ADDRESS_SUBSCRIBERS = 254

POLL_SECONDS = 0.05


class Addr(ctypes.Structure):
    _fields_ = [("client", ctypes.c_ubyte), ("port", ctypes.c_ubyte)]


class TstampStruct(ctypes.Structure):
    _fields_ = [("tv_sec", ctypes.c_uint), ("tv_nsec", ctypes.c_uint)]


class Tstamp(ctypes.Union):
    _fields_ = [("tick", ctypes.c_uint), ("time", TstampStruct)]


class NoteData(ctypes.Structure):
    _fields_ = [("channel", ctypes.c_ubyte), ("note", ctypes.c_ubyte),
                ("velocity", ctypes.c_ubyte), ("off_velocity", ctypes.c_ubyte),
                ("duration", ctypes.c_uint)]


class CtrlData(ctypes.Structure):
    _fields_ = [("channel", ctypes.c_ubyte), ("param", ctypes.c_ubyte),
                ("value", ctypes.c_int), ("_pad", ctypes.c_ubyte * 4)]


class ExtData(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("len", ctypes.c_uint)]


class EvData(ctypes.Union):
    _fields_ = [("note", NoteData), ("control", CtrlData), ("ext", ExtData),
                ("raw", ctypes.c_ubyte * 16)]


class SeqEvent(ctypes.Structure):
    """snd_seq_event_t as the 64-bit ABI lays it out (32 bytes)."""

    _fields_ = [("type", ctypes.c_ubyte), ("flags", ctypes.c_ubyte),
                ("tag", ctypes.c_ubyte), ("queue", ctypes.c_ubyte),
                ("time", Tstamp), ("source", Addr), ("dest", Addr),
                ("data", EvData)]


class Probe:
    def __init__(self, name, port_name):
        library = ctypes.util.find_library("asound") or "libasound.so.2"
        self.lib = ctypes.CDLL(library)
        self.handle = ctypes.c_void_p()
        self.event = SeqEvent()
        if ctypes.sizeof(SeqEvent) != 32:
            raise RuntimeError("snd_seq_event_t is not 32 bytes on this host (%d)"
                               % ctypes.sizeof(SeqEvent))
        opened = self.lib.snd_seq_open(ctypes.byref(self.handle), b"default",
                                       SND_SEQ_OPEN_DUPLEX, 0)
        if opened < 0:
            raise RuntimeError("cannot open the ALSA sequencer (rc %d): this host "
                               "has no usable sequencer" % opened)
        self.lib.snd_seq_set_client_name(self.handle, name.encode("utf-8"))
        caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ
        kinds = SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION
        self.port = self.lib.snd_seq_create_simple_port(
            self.handle, port_name.encode("utf-8"), caps, kinds)
        if self.port < 0:
            raise RuntimeError("cannot create the probe port (rc %d)" % self.port)
        self.client = self.lib.snd_seq_client_id(self.handle)

    def send_notes(self, count):
        """Send `count` note-ons TO SUBSCRIBERS - no destination address."""
        for index in range(count):
            event = self.event
            ctypes.memset(ctypes.byref(event), 0, ctypes.sizeof(event))
            event.type = SND_SEQ_EVENT_NOTEON
            event.queue = SND_SEQ_QUEUE_DIRECT
            event.source.client = self.client
            event.source.port = self.port
            event.dest.client = SND_SEQ_ADDRESS_SUBSCRIBERS
            event.dest.port = 0
            event.data.note.channel = 0
            event.data.note.note = 60 + (index % 12)
            event.data.note.velocity = 100
            written = self.lib.snd_seq_event_output(self.handle, ctypes.byref(event))
            if written < 0:
                raise RuntimeError("snd_seq_event_output failed (rc %d)" % written)
            drained = self.lib.snd_seq_drain_output(self.handle)
            if drained < 0:
                raise RuntimeError("snd_seq_drain_output failed (rc %d)" % drained)


def write_text(path, text):
    with open(path, "w") as handle:
        handle.write(text)


def read_text(path):
    try:
        with open(path) as handle:
            return handle.read().strip()
    except OSError:
        return None


def serve(probe, burst_file, hold_seconds):
    """Answer burst requests until killed or until the hold budget expires."""
    deadline = None if hold_seconds <= 0 else time.time() + hold_seconds
    answered = None
    while deadline is None or time.time() < deadline:
        request = read_text(burst_file) if burst_file else None
        if request and request.startswith("go:") and request != answered:
            count = int(request[3:])
            probe.send_notes(count)
            answered = "done:%d" % count
            write_text(burst_file, answered)
        time.sleep(POLL_SECONDS)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="the ALSA client name")
    parser.add_argument("--port", default="controller", help="the port name")
    parser.add_argument("--ready-file", required=True)
    parser.add_argument("--burst-file")
    parser.add_argument("--hold-ms", type=int, default=0,
                        help="0 (the default) means: run until killed")
    args = parser.parse_args(argv)

    try:
        probe = Probe(args.name, args.port)
    except (OSError, RuntimeError) as error:
        print("SKIPPED: %s" % error)
        return 77
    write_text(args.ready_file, "%d %d" % (probe.client, probe.port))
    print("PROBE client=%d port=%d name=%r port_name=%r"
          % (probe.client, probe.port, args.name, args.port), flush=True)
    serve(probe, args.burst_file, args.hold_ms / 1000.0)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
