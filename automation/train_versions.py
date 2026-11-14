"""Development identities for promoted trains; internal refs and release tags stay immutable."""
import fcntl
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LEDGER = ROOT / 'TRAIN-VERSIONS.json'


def identity(milestone, feature_train, revision, stage='alpha'):
    major, minor = milestone
    return {
        'milestone': f'{major}.{minor}', 'feature_train': feature_train,
        'revision': revision, 'stage': stage,
        'display_version': f'{major}.{minor}.{feature_train}.{revision}-{stage}',
        'version': f'{major}.{minor}.{feature_train}-{stage}.{revision}',
        'train_label': f'{feature_train}.{revision}',
    }


def current(train=None, sha=None):
    data = json.loads(LEDGER.read_text())
    row = data['history'][-1] if train is None else next(
        (r for r in data['history'] if r['train'] == train), None)
    if row is None or (sha is not None and row['sha'] != sha):
        raise ValueError(f'No promoted version for {train} at {sha}')
    return row


def feature_items(merged):
    """Feature-queue deliveries include completions/docs; bugs alone are maintenance."""
    return sorted({c['cluster'] for c in merged
                   if c['cluster'].startswith('Q050-') or c.get('branch', '').startswith('feat/050-')})


def promoted(train, base, sha, merged):
    """Advance only from the recorded promoted base, after the train's full validation."""
    with LEDGER.with_suffix('.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        data = json.loads(LEDGER.read_text())
        existing = next((r for r in data['history'] if r['train'] == train), None)
        if existing:
            if existing['sha'] != sha:
                raise ValueError('A promoted version cannot be reassigned to another commit')
            return existing
        previous = data['history'][-1]
        if previous['train'] != base:
            raise ValueError(f'Version base {previous["train"]} does not match {base}')
        features = feature_items(merged)
        feature_train = previous['feature_train'] + bool(features)
        revision = 0 if features else previous['revision'] + 1
        row = dict(identity(data['milestone'], feature_train, revision, data['stage']),
                   train=train, base=base, sha=sha, kind='feature' if features else 'maintenance',
                   feature_items=features, evidence='validated-promotion')
        data['history'].append(row)
        temp = LEDGER.with_suffix('.tmp')
        temp.write_text(json.dumps(data, indent=2) + '\n')
        temp.replace(LEDGER)
        return row


if __name__ == '__main__':
    print(json.dumps(current(), indent=2))
