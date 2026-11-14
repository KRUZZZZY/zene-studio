"""Register the owner-approved Rune category without changing existing task identities."""
import fcntl
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROG = Path.home() / 'Documents/AI_KOS_PROJECT/projects/zene-studio-program'
OWNER = 'you are the owner and long-term maintainer of this codebase, and its design is up to you'


def register():
    plans = PROG / 'plans'
    draft = json.loads((plans / 'rune-rework-tasks.json').read_text())
    units = draft['tasks']
    path = plans / 'queue-050.jsonl'
    with path.with_suffix('.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        original = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        old = {u['queue_id']: u for u in original}
        if len(old) != len(original):
            raise ValueError('Existing queue has duplicate identities; preserving it')
        new_ids = {u['queue_id'] for u in units}
        if len(new_ids) != len(units):
            raise ValueError('Rune draft has duplicate identities')
        for i, unit in enumerate(units, 1):
            identity = unit['queue_id']
            if identity in old and old[identity].get('category') != 'rune-ui':
                raise ValueError(f'{identity} already belongs to another task')
            if unit.get('difficulty') != 3 or unit.get('owner_difficulty') != 3:
                raise ValueError('Rune tasks must retain owner difficulty 3')
            if not set(unit.get('depends_on', [])).issubset(new_ids | old.keys()):
                raise ValueError(f'Unresolved dependency on {identity}')
            # Extend preparation/implementation context, preserving all original requirements.
            unit.update(category_sequence=i, review_process='terra-human-use',
                        interaction_contract='plans/RUNE-INTERACTION-CONTRACT.md',
                        terra_scenarios=[f'rune-{identity.lower()}'],
                        sources=['owner-2026-11-14', 'plans/RUNE-REWORK-SPEC.md',
                                 'plans/RUNE-INTERACTION-CONTRACT.md',
                                 'research/ui/RUNE-INTERACTION-RESEARCH-2026-11-14.md'],
                        member_ids=unit.get('member_ids', []), unverified_members=[], ungrouped=False,
                        existing_lane='', score=0,
                        why='Owner 2026-11-14: Rune UI rework is the top feature category, difficulty 3, with Terra human-use review.')
            unit['text'] = (OWNER + '.\nRead plans/RUNE-REWORK-SPEC.md and the interaction contract. '
                'Reference the existing Rune prototype; work only in the assigned isolated checkout. '
                'Q050-505 completes approved Phase 0 in the side project, with no product edits. '
                'Subsequent product integration requires its measured GO. '
                'Preserve existing release gates, issue budgets, model choices and resource limits.\n'
                'Terra owns human-use review; preserve independent code review and deterministic, '
                'platform, latency and real-engine/audio evidence. UNSURE or skipped checks are incomplete.\n'
                'Terra scenario: ' + unit['terra_human_scenario'] + '\n'
                'Deterministic proof: ' + str(unit['deterministic_test_proof']) + '\n'
                'Engine proof: ' + str(unit['real_engine_testability']) + '\n'
                'Review profile: plans/rune-review-scenarios.json, --task-id ' + identity)
        merged = units + [u for u in original if u['queue_id'] not in new_ids]
        temp = path.with_suffix('.tmp')
        temp.write_text(''.join(json.dumps(u, ensure_ascii=False) + '\n' for u in merged))
        temp.replace(path)
    return units, len(merged)


if __name__ == '__main__':
    tasks, count = register()
    print(f'Registered {len(tasks)} Rune tasks at the top of {count} total queue units')
