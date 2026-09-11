#!/usr/bin/env python3
"""Headless integration test for Zene Studio's agent control socket (SPEC A12/A16).

Starts the real `lmms` binary with `--control-socket <path>` under
QT_QPA_PLATFORM=offscreen, then drives it from an EXTERNAL client over the
AF_UNIX socket with line-delimited JSON-RPC. It asserts:

  * the socket file is created with mode 0600 and is unlinked on exit;
  * control.ping / control.version / control.commands_list answer;
  * the full flow open -> read mixer -> set a channel volume -> render -> save;
  * typed error paths (not_found, invalid_args);
  * control.undo / control.redo reverse a recorded mutating command.

Usage: QT_QPA_PLATFORM=offscreen python3 control-socket-integration.py <lmms> <project.mmp>
Exit code 0 only when every assertion passed.
"""

import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

CONNECT_TIMEOUT = 30.0
ENGINE_TIMEOUT = 120.0
RESPONSE_TIMEOUT = 60.0
RENDER_TIMEOUT = 240.0

TRANSCRIPT = []


def record(direction, payload):
    TRANSCRIPT.append("%s %s" % (direction, payload))


def fail(message, process=None, log_path=None):
    print("\nFAIL: %s" % message)
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    if process is not None:
        try:
            process.kill()
        except OSError:
            pass
    if log_path and os.path.exists(log_path):
        print("\n---- app log ----")
        with open(log_path, "r", errors="replace") as handle:
            print(handle.read()[-8000:])
    sys.exit(1)


class Client:
    """One request and one response per line, the wire contract of ControlServer."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(RESPONSE_TIMEOUT)
        self.sock.connect(path)
        self.buffer = b""

    def call(self, request_id, cmd, args=None, proto=1):
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        record("->", raw)
        self.sock.sendall(raw.encode("utf-8") + b"\n")
        reply = self._read_line()
        record("<-", reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def raw_call(self, raw):
        record("->", raw.decode("utf-8", "replace").rstrip("\n"))
        self.sock.sendall(raw)
        reply = self._read_line()
        record("<-", reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def _read_line(self):
        deadline = time.time() + RESPONSE_TIMEOUT
        while b"\n" not in self.buffer:
            if time.time() > deadline:
                raise TimeoutError("no response line before the timeout")
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the server closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def typed_error(reply, expected_id, expected_kind):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not False:
        raise AssertionError("expected ok=false, got %r" % reply)
    error = reply.get("error") or {}
    if error.get("kind") != expected_kind:
        raise AssertionError("expected error.kind=%r, got %r" % (expected_kind, error))
    if not error.get("message"):
        raise AssertionError("typed error carries no message: %r" % reply)
    return error


def ok_result(reply, expected_id):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not True:
        raise AssertionError("expected ok=true, got %r" % reply)
    return reply.get("result") or {}


def wait_for_socket(path, process, log_path):
    deadline = time.time() + CONNECT_TIMEOUT
    while time.time() < deadline:
        if process.poll() is not None:
            fail("the app exited before the socket was usable (exit %s)" % process.returncode,
                 process, log_path)
        if os.path.exists(path):
            try:
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                probe.settimeout(1.0)
                probe.connect(path)
                probe.close()
                return
            except (ConnectionRefusedError, FileNotFoundError, OSError):
                pass
        time.sleep(0.1)
    fail("the control socket %s never became connectable" % path, process, log_path)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    project = os.path.abspath(sys.argv[2])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1
    if not os.path.exists(project):
        print("FAIL: no fixture project at %s" % project)
        return 1

    tmp = tempfile.mkdtemp(prefix="zctl-", dir="/tmp")
    socket_path = os.path.join(tmp, "zene.sock")
    workspace = os.path.join(tmp, "workspace")
    os.makedirs(workspace)
    config_path = os.path.join(tmp, "lmmsrc.xml")
    # `audiodev` must be exactly AudioDummy::name(): with no sound card in CI the
    # app otherwise falls back to the dummy device and MainWindow puts up a modal
    # "Audio device setup failed" dialog before the event loop starts.
    with open(config_path, "w") as handle:
        handle.write(
            '<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            '  <app configured="1"/>\n'
            '  <audioengine audiodev="Dummy (no sound output)"/>\n'
            '  <paths workingdir="%s"/>\n'
            '</lmmsconfig>\n' % workspace
        )

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = tmp
    env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
    env["XDG_DATA_HOME"] = os.path.join(tmp, "data")
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)

    log_path = os.path.join(tmp, "app.log")
    log_file = open(log_path, "wb")
    process = subprocess.Popen(
        [binary, "--config", config_path, "--control-socket", socket_path],
        stdout=log_file, stderr=subprocess.STDOUT, env=env, cwd=tmp,
    )

    try:
        wait_for_socket(socket_path, process, log_path)

        mode = stat.S_IMODE(os.stat(socket_path).st_mode)
        if mode != 0o600:
            fail("socket mode is 0o%o, expected 0o600" % mode, process, log_path)

        client = Client(socket_path)

        # --- handshake -----------------------------------------------------
        ping = {}
        deadline = time.time() + ENGINE_TIMEOUT
        while time.time() < deadline:
            ping = client.call(1, "control.ping")
            if ping.get("ok") and (ping.get("result") or {}).get("engine_ready"):
                break
            if process.poll() is not None:
                fail("the app exited while waiting for the engine", process, log_path)
            time.sleep(0.2)
        if not ping.get("ok") or not (ping.get("result") or {}).get("engine_ready"):
            fail("the engine never became ready (last ping: %r)" % ping, process, log_path)

        version = ok_result(client.call(2, "control.version"), 2)
        if not version.get("version"):
            fail("control.version returned no version string", process, log_path)

        schema = ok_result(client.call(3, "control.commands_list"), 3)
        if schema.get("count", 0) < 19:
            fail("control.commands_list reports only %s commands" % schema.get("count"), process, log_path)

        # --- typed error paths --------------------------------------------
        typed_error(client.call(4, "control.no_such_command"), 4, "not_found")
        typed_error(client.call(5, "transport.seek", {"ticks": "not-a-number"}), 5, "invalid_args")
        typed_error(client.call(6, "mixer.set_volume", {"channel": "ch-9999", "volume": 0.5}), 6, "not_found")
        typed_error(client.call(7, "control.version", proto=99), 7, "refused")
        # mixer.set_pan is an honest refusal: this tree has no pan on a mixer channel.
        typed_error(client.call(8, "mixer.set_pan", {"channel": "ch-0", "pan": 0.5}), 8, "refused")
        # the master channel cannot be removed.
        typed_error(client.call(9, "mixer.remove_channel", {"channel": "ch-0"}), 9, "refused")

        # --- the full flow: open -> mixer -> set volume -> render -> save ---
        opened = ok_result(client.call(10, "project.open", {"path": project}), 10)
        if not opened.get("file"):
            fail("project.open did not report the loaded file", process, log_path)

        mixer_before = ok_result(client.call(11, "mixer.get_state"), 11)
        channels = mixer_before.get("channels", [])
        if not channels:
            fail("the fixture project loaded no mixer channels", process, log_path)

        # The fixture ships one (master) channel; make a second one to address.
        added = ok_result(client.call(12, "mixer.add_channel"), 12)
        target = added.get("channel")
        if not target or not target.startswith("ch-"):
            fail("mixer.add_channel returned %r" % target, process, log_path)
        original_volume = 1.0

        set_volume = ok_result(client.call(13, "mixer.set_volume", {"channel": target, "volume": 0.5}), 13)
        if abs(float(set_volume.get("volume", -1)) - 0.5) > 1e-6:
            fail("mixer.set_volume did not report the new fader value: %r" % set_volume, process, log_path)

        mixer_after = ok_result(client.call(14, "mixer.get_state"), 14)
        changed = [c for c in mixer_after.get("channels", []) if c.get("id") == target]
        if not changed or abs(float(changed[0].get("volume", -1)) - 0.5) > 1e-6:
            fail("mixer.get_state did not read back the new fader value", process, log_path)

        # --- SPEC A16: the recorded change is reversible through the journal
        undone = ok_result(client.call(15, "control.undo"), 15)
        if not undone.get("undone"):
            fail("control.undo reported nothing undone after mixer.set_volume", process, log_path)
        mixer_undone = ok_result(client.call(16, "mixer.get_state"), 16)
        restored = [c for c in mixer_undone.get("channels", []) if c.get("id") == target]
        if not restored or abs(float(restored[0].get("volume")) - float(original_volume)) > 1e-6:
            fail("control.undo did not restore the previous fader value", process, log_path)
        redone = ok_result(client.call(17, "control.redo"), 17)
        if not redone.get("redone"):
            fail("control.redo reported nothing redone", process, log_path)
        mixer_redone = ok_result(client.call(18, "mixer.get_state"), 18)
        reapplied = [c for c in mixer_redone.get("channels", []) if c.get("id") == target]
        if not reapplied or abs(float(reapplied[0].get("volume")) - 0.5) > 1e-6:
            fail("control.redo did not reapply the fader value", process, log_path)

        out_wav = os.path.join(tmp, "render.wav")
        rendered = ok_result(client.call(19, "render.render", {"out": out_wav, "format": "wav"}), 19)
        if rendered.get("path") != out_wav:
            fail("render.render reported %r" % rendered, process, log_path)
        if not rendered.get("sha256"):
            fail("render.render returned no sha256", process, log_path)
        if int(rendered.get("frames", 0)) <= 0:
            fail("render.render produced %r frames" % rendered.get("frames"), process, log_path)
        if not os.path.exists(out_wav) or os.path.getsize(out_wav) == 0:
            fail("render.render left no audio at %s" % out_wav, process, log_path)

        saved_path = os.path.join(tmp, "saved.mmp")
        saved = ok_result(client.call(20, "project.save", {"path": saved_path}), 20)
        if saved.get("file") != saved_path or not os.path.exists(saved_path):
            fail("project.save did not write %s" % saved_path, process, log_path)

        state = ok_result(client.call(21, "project.get_state"), 21)
        if not state.get("file"):
            fail("project.get_state returned no file", process, log_path)

        # --- transactions were recorded for the mutating commands ---------
        transactions = ok_result(client.call(22, "control.transactions"), 22).get("transactions", [])
        commands_recorded = {t.get("command"): t for t in transactions}
        for expected in ("mixer.set_volume", "mixer.add_channel", "project.open", "project.save"):
            if expected not in commands_recorded:
                fail("no transaction recorded for %s" % expected, process, log_path)
        if not commands_recorded["mixer.set_volume"].get("reversible"):
            fail("mixer.set_volume was not recorded as reversible", process, log_path)
        if commands_recorded["mixer.add_channel"].get("reversible"):
            fail("mixer.add_channel must honestly report itself as not reversible", process, log_path)

        # --- shutdown unlinks the socket ----------------------------------
        client.call(23, "control.quit")
        client.close()
        deadline = time.time() + 30.0
        while time.time() < deadline and process.poll() is None:
            time.sleep(0.1)
        if process.poll() is None:
            fail("the app did not exit after control.quit", process, log_path)
        if os.path.exists(socket_path):
            fail("the control socket file was not unlinked on exit", process, log_path)
        if process.returncode != 0:
            fail("the app exited with %s" % process.returncode, process, log_path)
    finally:
        log_file.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        shutil.rmtree(tmp, ignore_errors=True)

    print("PASS: control socket integration (offscreen, external client)")
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
