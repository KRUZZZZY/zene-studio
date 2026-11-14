import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

import feature_ladder as F
import queue_categories as C


class CategoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.policy = self.root / 'categories.json'
        self.policy.write_text(json.dumps({'categories': [{'id': 'rune-ui', 'title': 'Rune UI rework',
                                                          'order': 0, 'difficulty': 3}]}))
        p = mock.patch.object(C, 'POLICY', self.policy)
        p.start()
        self.addCleanup(p.stop)

    def test_category_is_before_ordinary_p0_without_reordering_other_work(self):
        rows = [{'queue_id': 'ordinary-p0', 'priority': 'P0', 'rank': 0},
                {'queue_id': 'ordinary-p1', 'priority': 'P1', 'rank': 0},
                {'queue_id': 'rune', 'category': 'rune-ui', 'priority': 'P0', 'rank': 999}]
        self.assertEqual([r['queue_id'] for r in sorted(rows, key=C.order)], ['rune', 'ordinary-p0', 'ordinary-p1'])

    def test_difficulty_three_is_preserved_and_routes_at_highest_percentile(self):
        row = C.annotate({'category': 'rune-ui'})
        self.assertEqual(row['difficulty_score'], 3)
        self.assertEqual(C.difficulty_pct(row), 1)
        self.assertEqual(row['difficulty_source'], 'owner-assigned')
        for value in (False, 0, 4, '3'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                C.difficulty({'owner_difficulty': value})

    def test_owner_difficulty_does_not_skip_preparation_or_dependencies(self):
        unit = {'queue_id': 'Q050-505', 'category': 'rune-ui'}
        with mock.patch.object(F, 'ROOT', self.root):
            self.assertEqual(F.status_of(unit, set(), set(), set(), {}, {}, {}, {}), 'awaiting-preparation')
            dependent = dict(unit, depends_on=['Q050-504'])
            self.assertEqual(F.status_of(dependent, set(), set(), set(), {}, {'packet_hash': 'p'}, {}, {}), 'waiting-for-dependency')
            self.assertEqual(F.status_of(unit, set(), set(), set(), {}, {'packet_hash': 'p'}, {}, {}), 'ready')

    def test_legacy_specs_keep_their_existing_hash(self):
        import hashlib
        unit = {'queue_id': 'Q050-001', 'title': 'Ordinary', 'goal': 'Goal', 'acceptance': ['Proof']}
        fields = {k: unit.get(k) for k in ('title', 'goal', 'acceptance', 'depends_on', 'text')}
        fields['id'] = unit['queue_id']
        expected = hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()
        self.assertEqual(F.spec_hash(unit), expected)
        self.assertNotEqual(F.spec_hash(dict(unit, category='rune-ui', owner_difficulty=3)), expected)

    def test_real_queue_rebuild_keeps_rune_first_and_does_not_fabricate_clef_rank(self):
        (self.root / 'plans').mkdir()
        (self.root / 'state').mkdir()
        (self.root / 'FIX-BASE').write_text('050/m0-train-30\n')
        units = [{'queue_id': 'Q050-001', 'title': 'Ordinary', 'priority': 'P0'},
                 {'queue_id': 'Q050-505', 'title': 'Rune', 'category': 'rune-ui', 'priority': 'P0', 'owner_difficulty': 3}]
        (self.root / 'plans/queue-050.jsonl').write_text(''.join(json.dumps(u) + '\n' for u in units))
        rank = {'tasks': {'Q050-001': {'packet_hash': 'packet', 'pct': .1, 'rank': 0}}}
        (self.root / 'TASK-DIFFICULTY.json').write_text(json.dumps(rank))
        tiers = [{'tier': name, 'below': cut} for name, cut in
                 [('haiku-max', .15), ('sonnet-high', .35), ('opus-medium', .6), ('opus-xhigh', 1.01)]]
        fq = SimpleNamespace(PROG=self.root, LINE=self.root / 'tree', split_stages=lambda identity: [])
        with mock.patch.object(F, 'ROOT', self.root), mock.patch.object(F, 'advance_states'), \
                mock.patch.object(F, 'packet_for', return_value={'packet_hash': 'packet', 'verdict': 'READY'}), \
                mock.patch.object(F, 'config', return_value={'tiers': tiers}), \
                mock.patch.object(F.subprocess, 'run', return_value=SimpleNamespace(stdout='')):
            F._rebuild(fq)
        queue = [json.loads(line) for line in (self.root / 'CLAUDE-QUEUE.jsonl').read_text().splitlines()]
        self.assertEqual(queue[0]['cluster'], 'Q050-505')
        self.assertEqual(queue[0]['difficulty_score'], 3)
        self.assertEqual(queue[0]['feature_tier'], 'opus-xhigh')
        self.assertEqual(queue[0]['rank_method'], 'owner-assigned')
        self.assertEqual(json.loads((self.root / 'TASK-DIFFICULTY.json').read_text()), rank)


if __name__ == '__main__':
    unittest.main()
