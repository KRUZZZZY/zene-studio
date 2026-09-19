#!/usr/bin/env python3
"""Crashbot instance pool — boot N headless instances, hold them, and REAP them.

L1 of verification/AGENT-CRASH-TESTING-PLAN.md. The launcher is the tree's own harness
(`tests/control_socket_harness.py`), imported in place: this module adds no second launch path.
What it adds is the pool the tree does not have:

  * a PID ledger in `<run-dir>/run.json` (pids, sockets, worlds, binary sha256 and version,
    spawn/exit times, artifacts). Every claim about a process here is a PID from that ledger,
    never a process name;
  * REAPING as a tested feature: `finally` in every caller, an `atexit` hook and a
    SIGTERM/SIGINT handler all funnel into `InstancePool.reap()`, which asks each instance to
    `control.quit` and `wait()`s for it (measured 0.16-0.24 s, exit 0). `SIGKILL` is the LAST
    RESORT and goes to one exact PID — never a pattern: `pkill -f` has already matched and
    killed a lane's own build on this box twice (tests/control-midi-reconnect.py:131-138);
  * the wave-start ORPHAN CHECK: `pgrep -a zene` exit 1 is the pass, and it means "no process
    NAMED zene exists anywhere" — a meaningful statement only on an otherwise idle machine.
    Foreign lanes drive the same release binary here, so `orphan_check()` reports both the
    name-scoped answer and the PID-scoped one, naming the foreign PIDs it saw;
  * artifacts: one directory per recorded event under `<run-dir>/artifacts/`, written at most
    ONCE per event. T1's acceptance turns on this: a deliberately SIGKILLed instance produces
    exactly ONE artifact, a clean run none.

Measured on this box (CRASH-TESTING-LIVEPROOF.md): socket connectable 0.06 s, engine ready
1.80 s single / 1.83 s wall for two concurrent, ~137 MB RSS, ~8% of one core idle, shutdown
0.16-0.24 s. `LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib` is MANDATORY (the binary exits 127
without it); the harness injects it from the binary's own path (`tests/control_vendor_libs.py`),
and `binary_identity()` proves the binary runs before an instance is spent on it.
"""

import atexit
import hashlib
import json
import os
import signal
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(os.path.dirname(HERE))
TESTS_DIR = os.path.join(TREE, "tests")
if TESTS_DIR not in sys.path:
    sys.path.insert(0, TESTS_DIR)

import control_socket_harness as H  # noqa: E402  (path set above; the tree's own harness)

DEFAULT_SCRATCH = "/tmp/crashbot-runs"
HARNESS_PATH = os.path.join(TESTS_DIR, "control_socket_harness.py")
MAX_INSTANCES = 4      # measured: 4 x ~137 MB = ~550 MB, ~32% of one core (plan L1)
_ACTIVE = []           # pools alive in this process, reaped by atexit / the signal handlers
_REAP_LOCK = threading.Lock()


def default_binary():
    """$ZENE_BINARY, else this tree's build, else the worktree's PARENT build.

    This lane's worktree (zene-030/wcrash) carries no build of its own; the release worktree
    beside it (its parent directory, zene-030) is where the built binary lives, and the
    resolution is recorded in every ledger as the resolved absolute path plus its sha256 -
    never as an assumption.
    """
    candidates = [os.environ.get("ZENE_BINARY"), os.path.join(TREE, "build", "zene"),
                  os.path.join(os.path.dirname(TREE), "build", "zene")]
    for candidate in candidates:
        if candidate and os.path.exists(candidate):
            return os.path.abspath(candidate)
    return os.path.abspath(candidates[1])


DEFAULT_BINARY = default_binary()


def utc_now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def binary_identity(binary, timeout=60):
    """sha256 + `--version` of the binary, BEFORE any instance is spent on it.

    The version call is the cheap proof that the mandatory LD_LIBRARY_PATH is in place:
    without it the binary exits 127 with 'libwasmtime.so: cannot open shared object file'.
    """
    identity = {"path": os.path.abspath(binary), "sha256": None, "version": None,
                "version_exit": None, "wasmtime_lib_dir": None}
    if not os.path.exists(binary):
        return identity
    identity["sha256"] = sha256_of(binary)
    env = H.with_vendor_library_path(binary, dict(os.environ))
    identity["wasmtime_lib_dir"] = env.get("LD_LIBRARY_PATH")
    try:
        done = subprocess.run([binary, "--version"], capture_output=True, text=True,
                              env=env, timeout=timeout)
        identity["version"] = (done.stdout or done.stderr).strip()
        identity["version_exit"] = done.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        identity["version"] = "failed: %s" % error
        identity["version_exit"] = 127
    return identity


def box_state():
    """Load and core count: a verdict taken on a loaded box is not comparable (plan rule 4)."""
    state = {"nproc": os.cpu_count(), "loadavg": None}
    try:
        with open("/proc/loadavg") as handle:
            state["loadavg"] = handle.read().strip()
    except OSError:
        pass
    return state


def run_capture(argv, timeout=20):
    try:
        done = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"argv": argv, "exit": done.returncode,
                "stdout": done.stdout.strip(), "stderr": done.stderr.strip()}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"argv": argv, "exit": None, "stdout": "", "stderr": str(error)}


def pgrep_zene():
    """`pgrep -a zene`: exit 1 means no process named zene exists anywhere on this box.

    Exit 1 is the wave-start pass, and it is NOT a leak check for this pool: foreign lanes
    share the binary, so only the ledger's PIDs can be blamed.
    """
    result = run_capture(["pgrep", "-a", "zene"])
    matches = [line for line in result["stdout"].splitlines() if line.strip()]
    return {"command": "pgrep -a zene", "exit": result["exit"], "matches": matches,
            "clean_by_name": result["exit"] == 1,
            "note": ("exit 1 = no process named zene anywhere; with foreign lanes on the "
                     "same binary, name-scoped emptiness is not a leak proof - compare PIDs")}


def orphan_check(our_pids, label="wave start"):
    """The name-scoped answer AND the PID-scoped one, with foreign PIDs named.

    `our_pids_alive` is the check a reaper's leak proof uses and must be empty after a reap;
    `foreign_alive` is every zene process that is not ours, so a reader can tell "this box has
    no zene" from "this box has OTHER lanes' zene".
    """
    pids = [int(pid) for pid in our_pids]
    probe = pgrep_zene()
    seen, foreign = [], []
    for line in probe["matches"]:
        try:
            pid = int(line.split(None, 1)[0])
        except (ValueError, IndexError):
            continue
        seen.append(pid)
        if pid not in pids:
            foreign.append(line)
    return {"label": label, "at": utc_now(), "probe": probe, "our_pids": pids,
            "our_pids_alive": sorted(set(seen) & set(pids)),
            "foreign_alive": foreign, "foreign_count": len(foreign)}


class PoolInstance:
    """One ledger row: the harness Instance, its process, its pid, and what happened to it."""

    def __init__(self, name, instance, process):
        self.name = name
        self.instance = instance
        self.process = process
        self.pid = process.pid
        self.socket = instance.socket_path
        self.world = instance.tmp
        self.client = None
        self.spawned_at = utc_now()
        self.state = "spawned"
        self.exit = None
        self.death = None
        self.death_filed = False

    def alive(self):
        return self.process.poll() is None

    def row(self):
        row = {"name": self.name, "pid": self.pid, "socket": self.socket, "world": self.world,
               "spawned_at": self.spawned_at, "state": self.state}
        if self.exit is not None:
            row["exit"] = self.exit
        if self.death is not None:
            row["death"] = self.death
        return row


class InstancePool:
    """Boot N instances through the tree's harness; hold the ledger; reap in every path."""

    def __init__(self, names=("i0",), binary=None, run_dir=None, run_id=None):
        if len(names) > MAX_INSTANCES:
            raise ValueError("the pool ceiling is %d instances (measured, plan L1): %r"
                             % (MAX_INSTANCES, list(names)))
        self.names = list(names)
        self.binary = os.path.abspath(binary or DEFAULT_BINARY)
        self.run_id = run_id or ("crashbot-%s" % time.strftime("%Y%m%d-%H%M%S"))
        self.run_dir = os.path.abspath(run_dir or os.path.join(DEFAULT_SCRATCH, self.run_id))
        self.ledger_path = os.path.join(self.run_dir, "run.json")
        self.artifact_dir = os.path.join(self.run_dir, "artifacts")
        self.lock = threading.Lock()
        self.instances = {}
        self.artifacts = []
        self.deliberate = []           # deliberate SIGKILLs, by exact PID, with their reason
        self.started_at = utc_now()
        self.reaping = None
        self.identity = binary_identity(self.binary)
        self.orphan_at_boot = None
        os.makedirs(self.run_dir, exist_ok=True)
        os.makedirs(self.artifact_dir, exist_ok=True)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, _value, _traceback):
        reason = "context exit"
        if exc_type is not None:
            reason = "context exit (exception: %s)" % exc_type.__name__
        self.reap(reason)
        return False

    # -- ledger --------------------------------------------------------------------------

    def ledger(self):
        return {"run_id": self.run_id, "tool": "tools/crashbot/pool.py",
                "started_at": self.started_at, "run_dir": self.run_dir, "binary": self.identity,
                "harness": {"path": HARNESS_PATH, "sha256": sha256_of(HARNESS_PATH)},
                "box": box_state(), "orphan_check_at_boot": self.orphan_at_boot,
                "instances": [rec.row() for rec in self.instances.values()],
                "deliberate": self.deliberate, "artifacts": self.artifacts,
                "reaping": self.reaping}

    def write_ledger(self):
        """Write run.json (temp + rename): a reader never sees half a ledger."""
        with self.lock:
            temp = self.ledger_path + ".tmp"
            with open(temp, "w") as handle:
                handle.write(json.dumps(self.ledger(), indent=2, default=str))
            os.replace(temp, self.ledger_path)

    # -- boot ----------------------------------------------------------------------------

    def boot(self):
        """Construct, spawn, connect and await every instance; record the orphan check."""
        if self.identity.get("version_exit") != 0:
            raise RuntimeError("the binary does not run: %s (version call exit %r, is "
                               "LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib set? the "
                               "harness injects it from the binary's own path)"
                               % (self.binary, self.identity.get("version_exit")))
        self.orphan_at_boot = orphan_check([], label="wave start (before boot)")
        self.write_ledger()
        for name in self.names:
            instance = H.Instance(self.binary)
            self.instances[name] = PoolInstance(name, instance, instance.spawn())
        self.write_ledger()
        for name in self.names:
            rec = self.instances[name]
            H.wait_for_socket(rec.instance, H.SOCKET_TIMEOUT)
            rec.client = H.Client(rec.instance.socket_path)
            rec.state = "connectable"
        self.write_ledger()
        for name in self.names:
            rec = self.instances[name]
            H.wait_ready(rec.instance, rec.client, H.Transcript())
            rec.state = "ready"
        self.write_ledger()
        return self

    def pids(self):
        return {name: rec.pid for name, rec in self.instances.items()}

    def alive_names(self):
        return [name for name, rec in self.instances.items() if rec.alive()]

    def client(self, name):
        return self.instances[name].client

    # -- artifacts -----------------------------------------------------------------------

    def _artifact_record(self, kind, index, target, rec, directory, detail, outcome):
        return {"index": index, "kind": kind, "target": target, "at": utc_now(),
                "run_id": self.run_id, "detail": detail, "outcome": outcome,
                "pid": rec.pid if rec else None,
                "signal": (rec.death or {}).get("signal") if rec else None,
                "exit_code": (rec.death or {}).get("exit_code") if rec else None,
                "instances": [row.row() for row in self.instances.values()],
                "orphan_check": orphan_check([row.pid for row in self.instances.values()],
                                             label="at artifact %d" % index)}

    def _copy_instance_logs(self, rec, directory):
        for label, source in (("stderr.log", rec.instance.stderr_path),
                              ("stdout.log", rec.instance.stdout_path)):
            try:
                with open(source, "rb") as src, open(os.path.join(directory, label), "wb") as dst:
                    dst.write(src.read())
            except OSError:
                pass

    def file_artifact(self, kind, target=None, detail=None, outcome=None):
        """File ONE artifact for ONE event; a second call for the same death is refused."""
        rec = self.instances.get(target)
        if rec is not None and rec.death_filed and kind == "instance_died":
            return None
        index = len(self.artifacts) + 1
        directory = os.path.join(self.artifact_dir, "%03d-%s" % (index, kind))
        os.makedirs(directory, exist_ok=True)
        record = self._artifact_record(kind, index, target, rec, directory, detail, outcome)
        if rec is not None:
            self._copy_instance_logs(rec, directory)
        with self.lock:
            with open(os.path.join(directory, "artifact.json"), "w") as handle:
                json.dump(record, handle, indent=2, default=str)
            self.artifacts.append({"index": index, "kind": kind, "target": target,
                                   "path": directory, "at": record["at"],
                                   "pid": record["pid"], "signal": record["signal"],
                                   "exit_code": record["exit_code"]})
            if rec is not None and kind == "instance_died":
                rec.death_filed = True
        self.write_ledger()
        return directory

    def _note_death(self, rec, detail):
        code = rec.process.returncode
        rec.death = {"exit_code": code, "signal": abs(code) if (code or 0) < 0 else None,
                     "signal_name": signal.Signals(-code).name if (code or 0) < 0 else None,
                     "detected_at": utc_now()}
        if rec.state != "killed":          # a deliberate kill keeps its own marker
            rec.state = "died"
        return self.file_artifact("instance_died", target=rec.name, detail=detail,
                                  outcome=rec.death)

    def collect_deaths(self, detail=None, names=None):
        """Detect instances that died since the last call and file EXACTLY one artifact each."""
        # `names` scopes the call to the caller's OWN instance: a pool-wide call from one worker
        # let a sibling's death stamp a surviving case `crash` (measured, T3 proof (b)).
        filed = []
        for rec in self.instances.values():
            if rec.alive() or rec.death_filed or (names is not None and rec.name not in names):
                continue
            path = self._note_death(rec, detail)
            filed.append({"name": rec.name, "pid": rec.pid, "artifact": path, **rec.death})
        return filed

    def sigkill(self, name, reason="deliberate SIGKILL"):
        """SIGKILL one instance BY ITS EXACT PID: the deliberate-kill path of acceptance (b),
        and the same call the reaper makes as a last resort. Never a name, never a pattern."""
        rec = self.instances[name]
        os.kill(rec.pid, signal.SIGKILL)
        rec.process.wait(timeout=30)
        rec.death = {"exit_code": rec.process.returncode, "signal": signal.SIGKILL,
                     "signal_name": "SIGKILL", "detected_at": utc_now(), "reason": reason}
        rec.state = "killed"
        record = {"name": name, "pid": rec.pid, "signal": "SIGKILL", "reason": reason,
                  "at": utc_now()}
        with self.lock:
            self.deliberate.append(record)
        self.write_ledger()
        return record

    # -- reaping -------------------------------------------------------------------------

    def _ask_quit(self, rec, row, quit_timeout):
        started = time.perf_counter()
        try:
            reply = rec.client.call(900000 + rec.pid % 1000, "control.quit",
                                    timeout=min(quit_timeout, 30))
            row["quit_ok"] = reply.get("ok")
        except (H.Timeout, OSError, ValueError) as error:
            row["quit_ok"] = "no reply: %s" % str(error).splitlines()[0][:120]
        row["quit_s"] = round(time.perf_counter() - started, 3)

    def _await_exit(self, rec, row, quit_timeout):
        if rec.process.poll() is not None:
            row.update({"exited": True, "exit_code": rec.process.returncode})
            return
        exited, code, waited = rec.instance.wait_for_exit(quit_timeout)
        row.update({"exited": exited, "exit_code": code, "wait_s": round(waited, 3)})

    def _hard_kill(self, rec, row):
        # LAST RESORT, exact PID only: control.quit did not stop it inside the bound.
        os.kill(rec.pid, signal.SIGKILL)
        rec.process.wait(timeout=30)
        row["hard_kill"] = True
        row["exit_code"] = rec.process.returncode

    def _record_and_close(self, rec, row):
        if rec.state not in ("killed", "died"):
            rec.state = "reaped"
        if rec.death is None and (row["exit_code"] or 0) != 0:
            rec.death = {"exit_code": row["exit_code"], "signal": None, "detected_at": utc_now()}
            rec.state = "died"
        row["socket_gone"] = not os.path.exists(rec.socket)
        if rec.client is not None:
            try:
                rec.client.close()
            except OSError:
                pass
        rec.instance.close()                     # rmtree of the harness's own /tmp world
        row["world_gone"] = not os.path.exists(rec.world)
        row["state"] = rec.state

    def _reap_one(self, rec, quit_timeout=H.QUIT_TIMEOUT):
        row = {"pid": rec.pid, "quit_ok": None, "quit_s": None, "exited": False,
               "exit_code": None, "wait_s": None, "hard_kill": False, "socket_gone": None,
               "world_gone": None}
        if rec.client is not None and rec.alive():
            self._ask_quit(rec, row, quit_timeout)
        self._await_exit(rec, row, quit_timeout)
        if rec.process.poll() is None:
            self._hard_kill(rec, row)
        self._record_and_close(rec, row)
        return row

    def reap(self, reason="finally", emergency=False):
        """Reap every instance: control.quit -> wait() -> SIGKILL by exact PID only if needed.

        Idempotent and safe to call from a signal handler: after the first call later calls are
        no-ops (a second reap would report phantom leaks).
        """
        with _REAP_LOCK:
            if self.reaping is not None:
                return self.reaping
            self.reaping = {"reason": reason, "at": utc_now(), "emergency": emergency,
                            "instances": {}}
        for name, rec in self.instances.items():
            try:
                self.reaping["instances"][name] = self._reap_one(rec)
            except Exception as error:                          # never leave a sibling behind
                self.reaping["instances"][name] = {"pid": rec.pid,
                                                   "reap_error": "%s: %s" % (type(error).__name__,
                                                                             error)}
        self.reaping["our_pids_alive_after"] = [pid for pid in self.pids().values()
                                                if os.path.exists("/proc/%d" % pid)]
        self.reaping["orphan_check_after"] = orphan_check([], label="after reap")
        self.write_ledger()
        return self.reaping


def install_signal_handlers():
    """SIGTERM/SIGINT reap every live pool, then die of the signal that arrived."""

    def handler(signum, _frame):
        for pool in list(_ACTIVE):
            try:
                pool.reap("signal %d" % signum, emergency=True)
            except Exception:                                    # a handler must not throw
                pass
        signal.signal(signum, signal.SIG_DFL)
        os.kill(os.getpid(), signum)

    for signum in (signal.SIGTERM, signal.SIGINT):
        try:
            signal.signal(signum, handler)
        except ValueError:
            pass                                                 # not the main thread


def register(pool):
    """Make `pool` reapable by atexit and by the signal handlers (called per boot)."""
    if pool not in _ACTIVE:
        _ACTIVE.append(pool)


def _at_exit():
    for pool in list(_ACTIVE):
        try:
            pool.reap("atexit")
        except Exception:
            pass


atexit.register(_at_exit)
install_signal_handlers()


def main(argv):
    """`pool.py --orphan-check` — the wave-start check, demonstrable on its own."""
    if "--orphan-check" in argv:
        print(json.dumps(orphan_check([], label="manual wave-start check"), indent=2))
        return 0
    print("usage: python3 tools/crashbot/pool.py --orphan-check")
    print("(the pool is driven by runner.py; this CLI exists for the wave-start check)")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
