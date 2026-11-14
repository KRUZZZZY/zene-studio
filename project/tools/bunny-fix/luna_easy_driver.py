#!/usr/bin/env python3
"""Consume Clef's streaming easy queue with four isolated Luna-max sessions.

Classification is a suggestion: every delegate must reproduce the finding.
Never resets a tree or merges commits. Dirty/error trees pause for parent review.
STOP finishes active sessions; COMPLETE drains the queue and exits.
"""
import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


# Light driver (owner, 2026-10-08): Claude Haiku 5.5 through tools/haiku/haiku-exec, unless
# ~/.local/share/zene-bunny-fix/qwen/LIGHT-ENGINE says "luna" (GPT-6 Luna through codex). Read live.
LIGHT_DIR = Path.home() / ".local/share/zene-bunny-fix/qwen"
sys.path.insert(0, str(LIGHT_DIR))
import prompt_context
HAIKU_EXEC = Path(__file__).resolve().parents[1] / "haiku" / "haiku-exec"


def light_engine():
    try:
        return "luna" if (LIGHT_DIR / "LIGHT-ENGINE").read_text().strip() == "luna" else "haiku"
    except OSError:
        return "haiku"


def light_cmd(codex_cmd):
    """codex_cmd is the Luna command line; for Haiku the same arguments go to haiku-exec."""
    return codex_cmd if light_engine() == "luna" else [str(HAIKU_EXEC)] + codex_cmd[2:]


def rows(path):
    if not path.exists():
        return []
    result = []
    for line in path.read_text().splitlines():
        try:
            result.append(json.loads(line))
        except json.JSONDecodeError:
            # Writer may be midway through its last append. Revisit next poll.
            break
    return result


def select_pending(queue, done, busy):
    selected = []
    reserved = set(busy)
    for item in queue:
        key = str(item['cluster'])
        tree = item.get('worktree')
        if key in done or not tree or tree in reserved:
            continue
        selected.append(item)
        reserved.add(tree)
    return selected


def git(tree, *args):
    return subprocess.check_output(['/usr/bin/git', '-C', tree, *args], text=True).strip()


def prompt(item, program, tree):
    return f"""Implement one Clef-suggested EASY defect, verifying its premise first.
Read {program}/AGENTS.md and plans/DELEGATION-RULES.md before editing.
Work ONLY in {tree}; do not spawn agents or modify another tree. Never push,
fetch, merge, switch, stash, rewrite history, or run a full suite/gate sweep.
Current worktree includes unreviewed prior fixes: preserve them and re-check
whether this finding is already fixed. Clef is not proof.

Use targeted cmake --build build --target zene <AffectedTest> and affected
ctest -R from build/tests. PATH guards cap builds and enforce test scope.
Rebuild every affected plugin module if its shared headers changed.
Record UNPIPED exit codes and assert nonzero test counts. Write a deterministic
regression first, demonstrate RED on unfixed product code, then implement the
smallest correct fix and demonstrate GREEN plus the other executable cases.
No allocation, locks or unbounded growth on audio paths. Bounded atomics and
preallocated synchronization are allowed if correct and tested.
Never weaken tests, assertions, gates or ratchets. Preserve command schemas and
known product contracts. Register added fork sources/tests, inherited changes,
and regenerate all-sources manifest by its recipe when necessary.
Before committing require complexity-gate.sh --check and file-length-gate.sh
--check success. Explicit git add paths only. Commit each defect separately,
body naming original cluster, regression and actual red/green commands/exits.
Preserve evidence as Markdown transcripts under docs/luna-easy-fixes-2026-10-06/;
raw .log files are ignored: never weaken evidence gate to commit them.
Write docs/luna-easy-fixes-2026-10-06/cluster-{item['cluster']}.md with verdict
FIXED/ALREADY-FIXED/FALSE-POSITIVE/REWORK-NEEDED, source references, exact actual
checks and unverified limits. Commit report and leave clean.
If a blocker needs platform/dependency/design/concurrency work, report it without
guessing a fix; do not silently skip or fabricate a success. Read-only fff search
if available; otherwise bounded rg in source/tests is acceptable.

Pinned Clef finding and prior evidence (historical lines may have moved):
{json.dumps(item, ensure_ascii=False, indent=2)}
"""


def run_one(item, args):
    tree = item['worktree']
    cluster = str(item['cluster'])
    out = args.campaign / 'runtime' / cluster
    out.mkdir(parents=True, exist_ok=True)
    record = {'cluster': item['cluster'], 'worktree': tree,
              'started': datetime.datetime.now().isoformat(),
              'head_before': git(tree, 'rev-parse', 'HEAD')}
    if git(tree, 'status', '--porcelain'):
        record.update(rc=None, blocked='dirty tree before session')
        return record
    env = dict(os.environ)
    home = Path.home()
    runtime = home / '.local/share/zene-bunny-fix/bunny'
    env.update(PATH=str(runtime / 'shims') + ':' + env['PATH'],
               BUNNY_JOBS='5', BUNNY_LOCKDIR=str(runtime / 'locks'),
               CCACHE_DIR=str(home / '.cache/ccache-bunny'), CCACHE_MAXSIZE='25G',
               CCACHE_BASEDIR=str(args.program), CCACHE_NOHASHDIR='true',
               QT_QPA_PLATFORM='offscreen',
               PKG_CONFIG_PATH=f'{home}/.local/zene-deps/root/usr/lib/pkgconfig:{home}/.local/zene-deps/root/usr/lib/x86_64-linux-gnu/pkgconfig',
               LD_LIBRARY_PATH=f'{home}/.local/zene-deps/root/usr/lib' + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else ''))
    env.pop('LMMS_PLUGIN_DIR', None)
    brief = prompt(item, args.program, tree)
    brief = prompt_context.stamp(brief)
    (out / 'brief.md').write_text(brief)
    command = light_cmd(['codex', 'exec', '-m', 'gpt-6-luna', '-c', 'model_reasoning_effort="max"',
                         '-c', 'approval_policy="never"', '-s', 'danger-full-access',
                         '-C', tree, '--ephemeral', '-o', str(out / 'final.md'), '-'])
    record['model'] = 'gpt-6-luna' if light_engine() == 'luna' else 'claude-haiku-5-5'
    with (out / 'session.log').open('w') as log:
        record['rc'] = subprocess.run(command, input=brief, text=True,
                                     stdout=log, stderr=subprocess.STDOUT, env=env).returncode
    record.update(head_after=git(tree, 'rev-parse', 'HEAD'),
                  status_after=git(tree, 'status', '--porcelain'),
                  finished=datetime.datetime.now().isoformat())
    return record


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--campaign', required=True, type=Path)
    ap.add_argument('--program', required=True, type=Path)
    args = ap.parse_args()
    state = args.campaign / 'runtime'
    state.mkdir(parents=True, exist_ok=True)
    done = set()
    paused = set()
    for path in state.glob('*.json'):
        record = json.loads(path.read_text())
        if 'cluster' not in record or 'worktree' not in record:
            continue  # Supervisor/bootstrap records are not defect sessions.
        done.add(str(record['cluster']))
        if record.get('rc') != 0 or record.get('status_after') or record.get('blocked'):
            paused.add(record['worktree'])
    active = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        while True:
            for future, item in list(active.items()):
                if not future.done():
                    continue
                try:
                    record = future.result()
                except Exception as error:
                    record = dict(cluster=item['cluster'], worktree=item['worktree'],
                                  rc=None, blocked=str(error))
                (state / f"{item['cluster']}.json").write_text(json.dumps(record, indent=2) + '\n')
                done.add(str(item['cluster']))
                if record.get('rc') != 0 or record.get('status_after') or record.get('blocked'):
                    paused.add(item['worktree'])
                print(json.dumps(record), flush=True)
                del active[future]
            stopping = (args.campaign / 'STOP').exists()
            free = shutil.disk_usage(args.program).free / 1e9
            if free < 25:
                stopping = True
            queue = rows(args.campaign / 'queue/easy.jsonl')
            # Correct reviewed regressions before taking more original hypotheses.
            queue.sort(key=lambda i: (str(i['cluster']) != 'review-2fa142d5b',
                                      not str(i['cluster']).startswith('review-')))
            reservation = args.campaign / 'RESERVED.json'
            reserved = set(json.loads(reservation.read_text())) if reservation.exists() else set()
            busy = paused | reserved | {i['worktree'] for i in active.values()}
            available = select_pending(queue, done, busy)
            if not stopping:
                for item in available[:4 - len(active)]:
                    print(f"START cluster={item['cluster']} tree={item['worktree']}", flush=True)
                    active[pool.submit(run_one, item, args)] = item
            unfinished = [i for i in queue if str(i['cluster']) not in done]
            # A failed/dirty tree remains paused. Once classification is final,
            # remaining items owned by paused trees are terminal unattempted work,
            # not a reason to idle forever. External reservations still wait.
            terminal_blocked = unfinished and all(
                not i.get('worktree') or i['worktree'] in paused for i in unfinished)
            if not active and (stopping or ((args.campaign / 'COMPLETE').exists()
                                           and (not unfinished or terminal_blocked))):
                terminal = {'finished': datetime.datetime.now().isoformat(),
                            'stopped': stopping, 'classification_complete': (args.campaign / 'COMPLETE').exists(),
                            'completed_session_keys': sorted(done),
                            'paused_worktrees': sorted(paused),
                            'unattempted': unfinished, 'free_GB': free}
                temp = state / 'driver-terminal.tmp'
                temp.write_text(json.dumps(terminal, indent=2) + '\n')
                temp.replace(args.campaign / 'DRIVER-TERMINAL.json')
                print(f'DRAINED done={len(done)} paused={len(paused)} free_GB={free:.1f}', flush=True)
                break
            time.sleep(10)


if __name__ == '__main__':
    main()
