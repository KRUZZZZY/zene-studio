"""One-time Git audit of promoted ancestry; never count abandoned train attempts."""
import json
import re
import subprocess
from pathlib import Path

import train_versions as V

REPO = Path.home() / 'Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-studio'
BASELINE = '050/features-2026-10-06'


def git(*args):
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()


def audit(latest):
    number = int(re.fullmatch(r'050/m0-train-(\d+)', latest)[1])
    latest_sha = git('rev-parse', latest)
    previous = git('rev-parse', BASELINE)
    history, feature_train, revision = [], 0, 0
    for i in range(1, number + 1):
        train = f'050/m0-train-{i}'
        sha = git('rev-parse', train)
        git('merge-base', '--is-ancestor', previous, sha)
        git('merge-base', '--is-ancestor', sha, latest_sha)
        lines = git('log', '--first-parent', '--format=%H%x09%s', f'{previous}..{sha}').splitlines()
        deliveries = [line for line in lines if '\t' in line and
                      line.split('\t', 1)[1].startswith(('Merge ', 'merge(')) and
                      re.search(r'feat/050-|Q050-', line.split('\t', 1)[1])]
        if deliveries:
            feature_train += 1
            revision = 0
        else:
            revision += 1
        row = dict(V.identity([0, 4], feature_train, revision), train=train, sha=sha,
                   base=BASELINE if i == 1 else f'050/m0-train-{i-1}',
                   kind='feature' if deliveries else 'maintenance',
                   feature_items=sorted(set(re.findall(r'Q050-[0-9]+(?:\.[0-9]+)*', '\n'.join(deliveries)))),
                   evidence='first-parent-promoted-ancestry', feature_merge_evidence=deliveries)
        history.append(row)
        previous = sha
    return {'schema': 1, 'milestone': [0, 4], 'stage': 'alpha',
            'milestone_name': 'Integrated DAW alpha', 'baseline': BASELINE,
            'baseline_sha': git('rev-parse', BASELINE), 'history': history}


if __name__ == '__main__':
    if V.LEDGER.exists():
        raise SystemExit('Version ledger already exists; refusing to replace promotion history')
    latest = (V.ROOT / 'FIX-BASE').read_text().strip()
    data = audit(latest)
    V.LEDGER.write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(data['history'][-1], indent=2))
