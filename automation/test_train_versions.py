import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import train_versions as V


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'versions.json'
        row = dict(V.identity([0, 4], 21, 1), train='train-30', sha='base-sha')
        self.path.write_text(json.dumps({'milestone': [0, 4], 'stage': 'alpha', 'history': [row]}))
        patch = mock.patch.object(V, 'LEDGER', self.path)
        patch.start()
        self.addCleanup(patch.stop)

    def test_repairs_advance_revision_only(self):
        row = V.promoted('train-31', 'train-30', 'new-sha', [{'cluster': 'ci-bug', 'branch': 'fix/ci'}])
        self.assertEqual(row['display_version'], '0.4.21.2-alpha')
        self.assertEqual(row['version'], '0.4.21-alpha.2')
        self.assertEqual(row['kind'], 'maintenance')

    def test_feature_delivery_starts_next_train_and_resets_revision(self):
        row = V.promoted('train-31', 'train-30', 'new-sha', [{'cluster': 'Q050-123', 'branch': 'integration/Q050-123'}])
        self.assertEqual(row['display_version'], '0.4.22.0-alpha')
        self.assertEqual(row['feature_items'], ['Q050-123'])

    def test_manual_feature_lanes_are_counted(self):
        row = V.promoted('train-31', 'train-30', 'new-sha', [{'cluster': 'L02', 'branch': 'feat/050-L02'}])
        self.assertEqual(row['train_label'], '22.0')

    def test_repeated_promotion_is_idempotent(self):
        first = V.promoted('train-31', 'train-30', 'new-sha', [])
        self.assertEqual(V.promoted('train-31', 'train-30', 'new-sha', []), first)
        self.assertEqual(len(json.loads(self.path.read_text())['history']), 2)

    def test_cannot_relabel_existing_commit_or_skip_a_promoted_base(self):
        before = self.path.read_text()
        for train, base, sha in [('train-30', 'train-29', 'changed-sha'), ('train-32', 'missing-base', 'new-sha')]:
            with self.assertRaises(ValueError):
                V.promoted(train, base, sha, [])
        self.assertEqual(self.path.read_text(), before)

    def test_unpromoted_or_wrong_sha_has_no_version(self):
        for train, sha in [('train-31', None), ('train-30', 'other-sha')]:
            with self.assertRaises(ValueError):
                V.current(train, sha)


if __name__ == '__main__':
    unittest.main()
