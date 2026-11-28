#!/usr/bin/env python3
"""Window-opens check for the Rune front end (stdlib only, runs the same on Linux, Windows, macOS).

Starts the binary with a real window and RUNE_READY_DIR pointing at an empty directory. Rune writes
`$RUNE_READY_DIR/<exe name>.<pid>` (one line of JSON: pid, bundle_id, ready_ms, rune) right after the
first window's on_ready returned, i.e. when the first frame is ready. That file is the "first frame
ready" report: the binary prints no such line on stdout or stderr.
(vendor/rune/crates/rune/src/platform/ready_signal.rs:1-8.)

Exit 0 only if the file appears within --timeout seconds with this process's pid and the process is
still alive --settle seconds later (a crash right after the first frame counts as a failure).
The process is then terminated. Prints one JSON object on stdout and writes it to --out if given.

  python ci/window_check.py --exe app/target/perf/zene-rune --timeout 90 --out ci-out/window.json
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time


def tail(path, n=2000):
    try:
        return pathlib.Path(path).read_text(errors='replace')[-n:]
    except OSError:
        return ''


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--timeout', type=float, default=90.0, help='seconds to wait for the ready file')
    ap.add_argument('--settle', type=float, default=1.0, help='seconds the process must stay alive after ready')
    ap.add_argument('--out', help='write the result JSON here')
    ap.add_argument('--log', help='stdout+stderr of the app go here (default: next to --out)')
    return ap.parse_args()


def ready_record(ready_dir, pid):
    """The ready file this pid wrote, or None while there is none (or it is still being written)."""
    for f in pathlib.Path(ready_dir).iterdir():
        try:
            data = json.loads(f.read_text())
        except (json.JSONDecodeError, OSError):
            continue
        if data.get('pid') == pid:
            return data
    return None


def wait_ready(proc, ready_dir, timeout, result):
    """Polls until the ready file appears, the process exits, or the timeout passes."""
    start = time.perf_counter()
    while time.perf_counter() - start < timeout:
        if proc.poll() is not None:
            result['reason'] = 'process exited before the first frame was reported'
            result['exit_code_if_died'] = proc.returncode
            return None
        ready = ready_record(ready_dir, proc.pid)
        if ready:
            return ready
        time.sleep(0.05)
    result['reason'] = f'no ready file within {timeout:.0f} s'
    return None


def check_alive(proc, ready, settle, result):
    result['ready_ms'] = ready.get('ready_ms')
    result['rune'] = ready.get('rune')
    time.sleep(settle)
    result['alive_after_ready'] = proc.poll() is None
    if result['alive_after_ready']:
        result['ok'] = True
    else:
        result['reason'] = 'process died within %.1f s after the first frame' % settle
        result['exit_code_if_died'] = proc.returncode


def stop(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(10)


def run(exe, args, log, result):
    with tempfile.TemporaryDirectory(prefix='rune-ready-') as ready_dir:
        env = dict(os.environ, RUNE_READY_DIR=ready_dir)
        start = time.perf_counter()
        with open(log, 'wb') as logf:
            proc = subprocess.Popen([exe], env=env, stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            try:
                ready = wait_ready(proc, ready_dir, args.timeout, result)
                if ready:
                    result['wall_ms'] = round((time.perf_counter() - start) * 1000)
                    check_alive(proc, ready, args.settle, result)
            finally:
                stop(proc)


def main():
    args = parse_args()
    exe = str(pathlib.Path(args.exe).resolve())
    log = args.log or (str(pathlib.Path(args.out).with_suffix('.app.log')) if args.out else os.devnull)
    result = {'exe': args.exe, 'ok': False, 'ready_ms': None, 'wall_ms': None, 'reason': None,
              'timeout_s': args.timeout, 'alive_after_ready': None, 'exit_code_if_died': None}
    if not os.path.exists(exe):
        result['reason'] = 'binary not found'
        return finish(result, args)
    run(exe, args, log, result)
    result['app_log_tail'] = tail(log) if not result['ok'] else ''
    return finish(result, args)


def finish(result, args):
    text = json.dumps(result, indent=2)
    print(text)
    if args.out:
        pathlib.Path(args.out).write_text(text + '\n')
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
