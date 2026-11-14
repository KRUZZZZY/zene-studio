"""Append-only, secret-scanned GitHub records for promoted Zene trains.

An isolated branch contains evidence and automation, with no workflow files. Product
source stays on its exact ci/* SHA branch. Never stages the live workspace/index.
"""
import fcntl
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
PROG = Path.home() / 'Documents/AI_KOS_PROJECT/projects/zene-studio-program'
CLONE = PROG / 'zene-studio'
CACHE = Path.home() / '.cache/zene-train-records'
REMOTE = 'https://github.com/KRUZZZZY/zene-studio.git'
BRANCH = 'ci/train-records'
STATUS = ROOT / 'TRAIN-BACKUP-STATUS.json'
AUTOMATION = ('train_backup.py', 'test_train_backup.py', 'train_versions.py',
              'bootstrap_train_versions.py', 'test_train_versions.py', 'train_ci.py',
              'train_sol.py', 'train_watch.py', 'merge_train.py', 'pin_latest.py',
              'test_merge_train_repairs_due.py', 'test_train_sol_repairs.py',
              'test_train_failure_handoff.py', 'test_train_watch_handoff.py',
              'test_train_watch_postponed.py', 'gitleaks.toml', 'backup-gitleaks.toml')
DOCS = ('VERSIONING-AND-MILESTONES.md', 'SPEC-050.md')


def command(args, cwd=None, timeout=180):
    result = subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{args[0]} exited {result.returncode}: '
                           + result.stderr.decode(errors='replace')[-800:])
    return result.stdout


def git(*args, cwd=None):
    return command(['git', *args], CLONE if cwd is None else cwd).decode().strip()


def read(path, default):
    return json.loads(path.read_text()) if path.exists() else default


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def evidence_files(number):
    # Exact train prefix excludes train-3 vs train-30 and preserves source logs.
    return sorted(p for p in (ROOT / 'merge').glob(f'train-{number}-*') if p.is_file())


def build_snapshot(dest):
    ledger = read(ROOT / 'TRAIN-VERSIONS.json', {})
    if not ledger.get('history'):
        raise ValueError('No validated promotion history; refusing an empty backup')
    repairs = read(ROOT / 'merge/SOL-REPAIRS.json', [])
    ci = read(ROOT / 'TRAIN-CI-STATUS.json', {}).get('commits', {})
    bugs = [json.loads(line) for line in (ROOT / 'CI-BUG-QUEUE.jsonl').read_text().splitlines() if line.strip()] \
        if (ROOT / 'CI-BUG-QUEUE.jsonl').exists() else []
    write_json(dest / 'versions.json', ledger)
    for filename in AUTOMATION:
        source = ROOT / filename
        if source.exists():
            target = dest / 'automation' / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    for filename in DOCS:
        source = PROG / 'plans' / filename
        if source.exists():
            target = dest / 'docs' / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    for source in (ROOT.parent / 'systemd/zene-train-backup.service',
                   ROOT.parent / 'systemd/zene-train-backup.timer',
                   ROOT.parent / 'status/zene_status.py'):
        if source.exists():
            target = dest / 'automation' / source.relative_to(ROOT.parent)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    copied_patches = set()
    for row in ledger['history']:
        train, sha = row['train'], row['sha']
        if not re.fullmatch(r'050/m0-train-\d+', train) or not re.fullmatch(r'[0-9a-f]{40}', sha):
            raise ValueError('Invalid pinned train identity')
        number = train.rsplit('-', 1)[-1]
        folder = dest / 'trains' / train.split('/')[-1]
        folder.mkdir(parents=True, exist_ok=True)
        write_json(folder / 'version.json', row)
        write_json(folder / 'source.json', {'repository': REMOTE, 'sha': sha,
                   'url': f'https://github.com/KRUZZZZY/zene-studio/tree/{sha}'})
        parent = next((r['sha'] for r in ledger['history'] if r['train'] == row['base']), ledger['baseline_sha'])
        changes = git('log', '--first-parent', '--format=%H%x09%s', f'{parent}..{sha}')
        (folder / 'changes.tsv').write_text(changes + '\n')
        entry = ci.get(sha, {})
        write_json(folder / 'ci.json', {k: entry[k] for k in
                   ('branch', 'submitted_at', 'status', 'runs', 'test_evidence') if k in entry})
        relevant = [r for r in repairs if r.get('base') == sha or r.get('train') == train]
        write_json(folder / 'repairs.json', relevant)
        write_json(folder / 'ci-bugs.json', [r for r in bugs if r.get('ci_source_sha') == sha])
        evidence = []
        for source in evidence_files(number):
            payload = source.read_bytes()
            target = folder / 'evidence' / source.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
            evidence.append({'file': f'evidence/{source.name}.gz', 'bytes': len(payload),
                             'sha256_uncompressed': hashlib.sha256(payload).hexdigest()})
        write_json(folder / 'evidence-index.json', evidence)
        for repair in relevant:
            commit = repair.get('commit')
            if not commit or commit in copied_patches:
                continue
            if not re.fullmatch(r'[0-9a-f]{40}', commit):
                raise ValueError('Invalid repair commit')
            patch = command(['git', 'format-patch', '--stdout', '--binary', '-1', commit], CLONE)
            target = dest / 'repair-patches' / f'{commit}.patch'
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(patch)
            copied_patches.add(commit)
    (dest / 'README.md').write_text(
        '# Zene Studio train records\n\n'
        'Append-only snapshots of promoted-train identities, first-parent changes, CI links, '
        'local evidence, repair records and the automation that maintains them. '
        'A snapshot is evidence of what ran, not a green-CI or release certificate.\n\n'
        'Start with versions.json and trains/<internal-train>/version.json. '
        'ci.json links the product source branch and hosted CI. Evidence logs are gzip-compressed; '
        'evidence-index.json records SHA-256 hashes of the original bytes. Missing evidence stays missing. '
        'Repair patches preserve unmerged deltas and original commit IDs; inspect repairs.json for '
        'base/source IDs and verification. Restoring a queue requires review against the current base.\n\n'
        'Git history preserves earlier snapshots when CI or repair status changes. '
        'Product source history is separately backed by ci/m0-train-N-<SHA> branches; '
        'this records branch deliberately has no GitHub workflows. '
        'Automation files are recovery copies; restore selected files without replacing live queues blindly.\n\n'
        'Every update is scanned for secrets before compression and again before push. '
        'No credentials, provider configuration or agent conversations are collected.\n')
    return ledger


def scan(folder, report):
    command(['gitleaks', 'dir', str(folder), '--redact=100', '--no-banner',
             '--ignore-gitleaks-allow', '--config', str(ROOT / 'backup-gitleaks.toml'),
             '--report-format', 'json', '--report-path', str(report)], timeout=300)


def compress_evidence(folder):
    for source in folder.glob('trains/*/evidence/*'):
        source.with_name(source.name + '.gz').write_bytes(gzip.compress(source.read_bytes(), mtime=0))
        source.unlink()


def sync_repo():
    repo = CACHE / 'repo'
    if not (repo / '.git').exists():
        repo.mkdir(parents=True, exist_ok=True)
        git('init', '-q', '-b', BRANCH, cwd=repo)
        git('remote', 'add', 'origin', REMOTE, cwd=repo)
    if git('status', '--porcelain', cwd=repo):
        raise RuntimeError('Backup checkout is dirty; preserving it')
    remote = git('ls-remote', '--heads', 'origin', f'refs/heads/{BRANCH}', cwd=repo)
    if remote:
        git('fetch', '--quiet', 'origin', f'refs/heads/{BRANCH}', cwd=repo)
        has_head = subprocess.run(['git', 'rev-parse', '--verify', 'HEAD'], cwd=repo, capture_output=True).returncode == 0
        if has_head:
            git('merge', '--ff-only', 'FETCH_HEAD', cwd=repo)
        else:
            git('switch', '-C', BRANCH, 'FETCH_HEAD', cwd=repo)
    return repo


def publish():
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE / 'backup.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return {'status': 'busy'}
        with tempfile.TemporaryDirectory(prefix='snapshot-', dir=CACHE) as tmp:
            folder = Path(tmp)
            ledger = build_snapshot(folder)
            scan(folder, CACHE / 'secret-scan.json')
            compress_evidence(folder)
            repo = sync_repo()
            # Copy only our explicit snapshot tree. Never removes other Git history.
            shutil.copytree(folder, repo, dirs_exist_ok=True)
            paths = sorted(str(p.relative_to(folder)) for p in folder.rglob('*') if p.is_file())
            git('add', '--', *paths, cwd=repo)
            if git('diff', '--cached', '--name-only', cwd=repo):
                latest = ledger['history'][-1]
                git('-c', 'user.name=Zene train backup', '-c', 'user.email=noreply@zene.invalid',
                    'commit', '-qm', f"Record {latest['display_version']} ({latest['train']}); evidence and CI status", cwd=repo)
            commit = git('rev-parse', 'HEAD', cwd=repo)
            command(['gitleaks', 'git', '--log-opts', f'{commit}^!', '--redact=100', '--no-banner',
                     '--ignore-gitleaks-allow', '--config', str(ROOT / 'backup-gitleaks.toml'),
                     '--report-format', 'json', '--report-path', str(CACHE / 'commit-scan.json')], cwd=repo, timeout=300)
            git('push', 'origin', f'HEAD:refs/heads/{BRANCH}', cwd=repo)
            return {'status': 'backed-up', 'branch': BRANCH, 'commit': commit,
                    'train': ledger['history'][-1]['train'], 'trains': len(ledger['history']),
                    'url': f'https://github.com/KRUZZZZY/zene-studio/tree/{BRANCH}'}


def main():
    try:
        result = publish()
    except Exception as exc:
        result = {'status': 'blocked', 'error': str(exc)[-1200:]}
    result['checked_at'] = time.strftime('%Y-%m-%dT%H:%M:%S')
    temp = STATUS.with_suffix('.tmp')
    write_json(temp, result)
    temp.replace(STATUS)
    print(json.dumps(result))
    return 1 if result['status'] == 'blocked' else 0


if __name__ == '__main__':
    raise SystemExit(main())
