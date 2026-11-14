#!/usr/bin/env python3
"""ControlSessionSdkPipeSmoke: the TypeScript SDK over its own named-pipe transport.

This is the SDK's own Windows transport path. The native ControlNamedPipeSmoke proves the
protocol over a pipe, not the TypeScript SDK: this launcher starts a real zene on a control
path, lets the SDK's ZeneClient connect to that path, and drives it (connect, version, read,
edit, event, unsubscribe, close) through tools/zene-session-sdk/test/pipe-smoke.ts.

The transport is chosen by the SHAPE OF THE PATH (control_pipe_client.make_client): on
Windows the path is \\\\.\\pipe\\<name> and the SDK's connect() opens that pipe; on POSIX
the path is a unix socket and the identical assertions run over the socket. That proves the
file's logic but NOT the Windows half: the Windows half is proved only on hosted CI.

Exit 77 (skip) when there is no zene binary. A hang is a failure, never a skip.

Usage: session-sdk-pipe-smoke.py <zene-binary> <node> <sdk-dir>
"""

import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time

from control_pipe_client import Failure, Timeout, make_client

START_BOUND = 120    # the first control.ping waits for Engine::init
REPLY_BOUND = 30     # one reply on a live connection
CONNECT_BOUND = 60   # the listener appearing after launch
EXIT_BOUND = 60      # the instance exiting after control.quit
SDK_BOUND = 300      # the TypeScript run as a whole

DUMMY_DEVICE = "Dummy (no sound output)"


def config_xml(workingdir, audiodev=DUMMY_DEVICE):
    """The minimal headless config (as control_socket_harness.config_xml)."""
    settings = [
        '  <app configured="1"/>',
        '  <audioengine audiodev="%s"/>' % audiodev,
        '  <paths workingdir="%s"/>' % workingdir,
    ]
    return ('<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            + "\n".join(settings) + "\n"
            '</lmmsconfig>\n')


def ok(text):
    print("PASS: %s" % text)


class Client:
    """A minimal JSON-RPC client over whichever transport make_client chose."""

    def __init__(self, transport):
        self.transport = transport
        self.next_id = 1

    def call(self, cmd, args=None, timeout_s=REPLY_BOUND):
        request_id = self.next_id
        self.next_id += 1
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": 1}
        self.transport.send((json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8"))
        line = self.transport.read_line(timeout_s)
        try:
            reply = json.loads(line)
        except ValueError:
            raise Failure("the reply to %s is not JSON: %r" % (cmd, line[:200]))
        if reply.get("id") != request_id:
            raise Failure("the reply to %s carries id %r, expected %r"
                          % (cmd, reply.get("id"), request_id))
        return reply

    def ok_call(self, cmd, args=None, timeout_s=REPLY_BOUND):
        reply = self.call(cmd, args, timeout_s=timeout_s)
        if reply.get("ok") is not True:
            raise Failure("%s was refused: %s" % (cmd, json.dumps(reply)[:300]))
        return reply.get("result") or {}

    def close(self):
        self.transport.close()


class Instance:
    """One zene process with its output captured to files under tmp."""

    def __init__(self, zene, tmp, path):
        self.zene = zene
        self.tmp = tmp
        self.path = path
        self.out_path = os.path.join(tmp, "zene.out")
        self.err_path = os.path.join(tmp, "zene.err")
        self.process = None
        self.out = None
        self.err = None

    def spawn(self):
        dirs = {name: os.path.join(self.tmp, name) for name in
                ("home", "xdg-config", "xdg-data", "userprofile", "appdata", "localappdata")}
        for path in dirs.values():
            os.makedirs(path, exist_ok=True)
        cfg = os.path.join(self.tmp, "zene.xml")
        with open(cfg, "w", encoding="utf-8") as handle:
            handle.write(config_xml(os.path.join(self.tmp, "workspace")))
        env = dict(os.environ)
        env.update({
            "HOME": dirs["home"],
            "XDG_CONFIG_HOME": dirs["xdg-config"],
            "XDG_DATA_HOME": dirs["xdg-data"],
            "USERPROFILE": dirs["userprofile"],
            "APPDATA": dirs["appdata"],
            "LOCALAPPDATA": dirs["localappdata"],
            "QT_QPA_PLATFORM": "offscreen",
            "LMMS_STEM_OFFLINE": "1",
        })
        self.out = open(self.out_path, "wb")
        self.err = open(self.err_path, "wb")
        self.process = subprocess.Popen(
            [self.zene, "--config", cfg, "--control-socket", self.path],
            cwd=self.tmp, env=env, stdin=subprocess.DEVNULL, stdout=self.out, stderr=self.err)

    def alive(self):
        return self.process is not None and self.process.poll() is None

    def log(self):
        text = ""
        for path in (self.out_path, self.err_path):
            try:
                with open(path, "rb") as handle:
                    text += handle.read().decode("utf-8", "replace")
            except OSError:
                pass
        return text

    def wait_for_exit(self, bound):
        try:
            return True, self.process.wait(timeout=bound)
        except subprocess.TimeoutExpired:
            return False, None

    def close(self):
        if self.alive():
            self.process.kill()
            self.process.wait(timeout=30)
        for handle in (self.out, self.err):
            if handle is not None:
                handle.close()


def wait_ready(instance):
    """The listener appearing is not the instance being ready: poll, bounded."""
    deadline = time.time() + CONNECT_BOUND
    last = None
    while time.time() < deadline:
        if not instance.alive():
            raise Failure("the instance exited (code %s) before its control path was usable; "
                          "log:\n%s" % (instance.process.returncode, instance.log()[-4000:]))
        try:
            return make_client(instance.path, 5.0)
        except (Failure, Timeout, OSError) as exc:
            last = exc
            time.sleep(0.1)
    raise Timeout("the control path %s was never connectable within %ds (%s); log:\n%s"
                  % (instance.path, CONNECT_BOUND, last, instance.log()[-4000:]))


def path_gone(path):
    """POSIX: the socket file is gone. Windows: nothing answers on the pipe any more."""
    if os.name == "nt":
        try:
            probe = make_client(path, 3.0)
        except (Failure, Timeout, OSError):
            return True
        probe.close()
        return False
    return not os.path.exists(path)


def run(zene, node, sdk, path, tmp):
    problems = []
    instance = Instance(zene, tmp, path)
    client = None
    quitter = None
    try:
        instance.spawn()
        client = wait_ready(instance)
        ok("the instance listens on %s and a client can connect to it" % path)

        transcript = Client(client)
        result = transcript.ok_call("control.ping", timeout_s=START_BOUND)
        if result.get("pong") is not True or result.get("proto") != 1:
            raise Failure("control.ping answered %s, expected pong with proto 1"
                          % json.dumps(result)[:300])
        ok("control.ping answers (pong, proto 1)")
        # A pong is not readiness: engine commands are refused engine_starting until
        # control.ping reports engine_ready. Poll, bounded by START_BOUND.
        deadline = time.time() + START_BOUND
        while not result.get("engine_ready"):
            if time.time() > deadline:
                raise Timeout("the engine never reported engine_ready within %ds; log tail:\n%s"
                              % (START_BOUND, instance.log()[-4000:]))
            if not instance.alive():
                raise Failure("the instance exited (code %s) before the engine was ready; log tail:\n%s"
                              % (instance.process.returncode, instance.log()[-4000:]))
            time.sleep(0.2)
            result = transcript.ok_call("control.ping", timeout_s=REPLY_BOUND)
        ok("the engine reports engine_ready")
        client.close()
        client = None

        command = [node, "--experimental-strip-types", "--disable-warning=ExperimentalWarning",
                   "test/pipe-smoke.ts", path]
        try:
            completed = subprocess.run(command, cwd=sdk, timeout=SDK_BOUND)
        except subprocess.TimeoutExpired:
            raise Failure("the SDK pipe smoke did not finish within %ds; log tail:\n%s"
                          % (SDK_BOUND, instance.log()[-4000:]))
        if completed.returncode != 0:
            raise Failure("the SDK pipe smoke exited %d; log tail:\n%s"
                          % (completed.returncode, instance.log()[-4000:]))
        ok("the TypeScript SDK ran its pipe smoke over %s (see its PASS lines above)" % path)

        quitter = make_client(path, CONNECT_BOUND)
        reply = Client(quitter).call("control.quit", {"save": False}, timeout_s=REPLY_BOUND)
        if reply.get("ok") is not True:
            raise Failure("control.quit was refused: %s" % json.dumps(reply)[:300])
        ok("control.quit answers")
        quitter.close()
        quitter = None

        exited, code = instance.wait_for_exit(EXIT_BOUND)
        if not exited:
            raise Failure("the instance did not exit within %ds of control.quit; log:\n%s"
                          % (EXIT_BOUND, instance.log()[-4000:]))
        ok("the instance exits after control.quit (code %s)" % code)
        if "FATAL: the normal shutdown did not finish" in instance.log():
            raise Failure("the shutdown guard fired: the normal shutdown did not finish")
        if not path_gone(path):
            raise Failure("the control path %s is still there after the instance exited" % path)
        ok("the control path is gone after the instance exits")
    except Exception as exc:
        problems.append(str(exc))
        print("FAIL: %s" % exc)
    finally:
        for handle in (client, quitter):
            if handle is not None:
                handle.close()
        instance.close()
    return problems


def main(argv):
    if len(argv) < 4:
        print("usage: session-sdk-pipe-smoke.py <zene-binary> <node> <sdk-dir>")
        return 2
    zene = os.path.abspath(argv[1])
    if not os.path.exists(zene):
        print("SKIP: nothing to drive: %s does not exist" % zene)
        return 77
    if os.path.exists(argv[2]):
        node = os.path.abspath(argv[2])
    else:
        node = shutil.which(argv[2]) or argv[2]
    sdk = os.path.abspath(argv[3])
    tmp = tempfile.mkdtemp(prefix="zene-sdk-pipe-smoke-")
    try:
        if os.name == "nt":
            path = "\\\\.\\pipe\\zene-sdk-smoke-%d-%d" % (os.getpid(), random.randrange(100000))
        else:
            path = os.path.join(tmp, "zene.sock")
        print("control path under test: %s" % path)
        problems = run(zene, node, sdk, path, tmp)
        print("\n================ SUMMARY ================")
        print("ControlSessionSdkPipeSmoke: %s" % ("FAIL" if problems else "PASS"))
        for item in problems:
            print("  - %s" % item)
        return 1 if problems else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
