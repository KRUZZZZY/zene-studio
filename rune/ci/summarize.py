#!/usr/bin/env python3
"""Builds ci-out/summary.json for one Rune CI job from the step outcomes and the logs the steps left.

Exit 1 if any check failed, so the job ends red after every step has had its turn (stdlib only).

  python ci/summarize.py --dir ci-out --os ubuntu --build "$B" --test "$T" --snap "$S" --window "$W" [--min-tests 26]

Each outcome is the `steps.<id>.outcome` string (success, failure, cancelled, skipped).

A failed check is also printed as one `error: rune <os>: ...` line (and one per failed test), because the
KADS CI watch builds a failure's identity from the `error:` lines of the failed step's log; with them, two
different Rune failures queue two different units and the same failure seen again queues none.
"""
import argparse
import json
import os
import pathlib
import re
import sys

CHECKS = ('build', 'test', 'snap', 'window')


def read(path):
    try:
        return pathlib.Path(path).read_text(errors='replace')
    except OSError:
        return ''


def load_json(path):
    try:
        return json.loads(read(path))
    except json.JSONDecodeError:
        return None


def seconds(out_dir, name):
    text = read(out_dir / f'{name}.seconds').strip()
    return int(text) if text.isdigit() else None


def test_counts(log):
    """Totals over every `test result:` line of a cargo test log, plus the names of failed tests."""
    results = re.findall(r'test result: \w+\. (\d+) passed; (\d+) failed; (\d+) ignored', log)
    passed, failed, ignored = (sum(int(r[i]) for r in results) for i in range(3))
    failed_names = sorted(set(re.findall(r'^test (\S+) \.\.\. FAILED', log, re.M)))
    return {'passed': passed, 'failed': failed, 'ignored': ignored, 'failed_tests': failed_names}


def gpu_lines(out_dir):
    """Which GPU Rune drew with (RUNE_GPU_LOG=1 prints it; vendor/rune/crates/rune/src/gpu/pick.rs:306-318)."""
    lines = []
    for name in ('snap.log', 'window.app.log'):
        for line in read(out_dir / name).splitlines():
            if line.startswith('rune: GPU') or 'a surface draws with' in line:
                lines.append(line.strip())
    return lines[:4]


def build_summary(args, out_dir):
    tests = test_counts(read(out_dir / 'test.log'))
    snap = load_json(out_dir / 'snap-check.json') or {}
    window = load_json(out_dir / 'window.json') or {}
    tests_ok = args.test == 'success' and tests['failed'] == 0 and tests['passed'] >= args.min_tests
    summary = {
        'os': args.os,
        'runner_os': os.environ.get('RUNNER_OS'),
        'runner_arch': os.environ.get('RUNNER_ARCH'),
        'commit': os.environ.get('GITHUB_SHA'),
        'rune_rev': os.environ.get('RUNE_REV'),
        'build': {'ok': args.build == 'success', 'step_outcome': args.build, 'profile': 'perf',
                  'seconds': seconds(out_dir, 'build')},
        'tests': dict(tests, ok=tests_ok, step_outcome=args.test, min_expected=args.min_tests,
                      seconds=seconds(out_dir, 'test')),
        'snap': {'ok': args.snap == 'success' and bool(snap.get('ok')), 'step_outcome': args.snap,
                 'png_bytes': snap.get('bytes'), 'reason': snap.get('reason'), 'seconds': seconds(out_dir, 'snap')},
        'window': {'ok': args.window == 'success' and bool(window.get('ok')), 'step_outcome': args.window,
                   'ready_ms': window.get('ready_ms'), 'wall_ms': window.get('wall_ms'),
                   'reason': window.get('reason')},
        'gpu': gpu_lines(out_dir),
    }
    extra = load_json(out_dir / 'snap-wgpu-check.json')
    if extra is not None:  # macOS only: the same snap with RUNE_GPU_BACKEND=wgpu; informational, not gating
        summary['extra_snap_wgpu_metal'] = {'ok': bool(extra.get('ok')), 'png_bytes': extra.get('bytes')}
    summary['all_ok'] = all(summary[k]['ok'] for k in ('build', 'tests', 'snap', 'window'))
    return summary


def error_lines(summary):
    """One `error:` line per failed check (and per failed test), for the log the CI watch reads."""
    name = summary['os']
    tests = summary['tests']
    detail = {
        'build': f"cargo build --profile perf: step {summary['build']['step_outcome']}",
        'tests': (f"cargo test: {tests['passed']} passed, {tests['failed']} failed "
                  f"(at least {tests['min_expected']} must pass); step {tests['step_outcome']}"),
        'snap': f"headless --snap: {summary['snap']['reason'] or 'step ' + summary['snap']['step_outcome']}",
        'window': f"window check: {summary['window']['reason'] or 'step ' + summary['window']['step_outcome']}",
    }
    lines = [f'error: rune {name}: {detail[k]}' for k in detail if not summary[k]['ok']]
    lines += [f'error: rune {name}: test failed: {t}' for t in tests['failed_tests']]
    return lines


def write_step_summary(summary, path):
    def mark(ok):
        return 'pass' if ok else 'FAIL'
    tests, snap, window = summary['tests'], summary['snap'], summary['window']
    rows = [
        f'### rune {summary["os"]}\n\n| check | result | detail |\n|---|---|---|\n',
        f'| build (perf) | {mark(summary["build"]["ok"])} | {summary["build"]["seconds"]} s |\n',
        f'| cargo test | {mark(tests["ok"])} | {tests["passed"]} passed, {tests["failed"]} failed |\n',
        f'| headless snap | {mark(snap["ok"])} | {snap["png_bytes"]} bytes {snap["reason"] or ""} |\n',
        f'| window opens | {mark(window["ok"])} | ready_ms {window["ready_ms"]} {window["reason"] or ""} |\n',
        f'\nGPU: `{"; ".join(summary["gpu"][:2]) or "not reported"}`\n\n',
    ]
    with open(path, 'a') as f:
        f.write(''.join(rows))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='ci-out')
    ap.add_argument('--os', required=True)
    for check in CHECKS:
        ap.add_argument(f'--{check}', default='skipped')
    ap.add_argument('--min-tests', type=int, default=26)
    args = ap.parse_args()
    out_dir = pathlib.Path(args.dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    summary = build_summary(args, out_dir)
    (out_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    for line in error_lines(summary):
        print(line)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        write_step_summary(summary, os.environ['GITHUB_STEP_SUMMARY'])
    return 0 if summary['all_ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
