"""Verified repairs must start a train even when ordinary candidates are empty."""
import json
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest import mock

import integration_jobs
import merge_train as MT
import train_sol as S


class ReachedBuildChecks(Exception):
    pass


class RepairsDueTests(unittest.TestCase):
    def exercise(self, rows, candidates=None, terra_block=None, dry=False):
        with tempfile.TemporaryDirectory() as folder, ExitStack() as stack:
            root = Path(folder)
            (root / 'FIX-BASE').write_text('050/m0-train-30\n')
            repairs = root / 'repairs.json'
            repairs.write_text(json.dumps(rows))
            stack.enter_context(mock.patch.object(MT, 'ROOT', root))
            stack.enter_context(mock.patch.object(S, 'REPAIRS', repairs))
            stack.enter_context(mock.patch.object(S.mt, 'git', return_value='current-sha'))
            stack.enter_context(mock.patch.object(MT, 'activate_terra_integrations'))
            stack.enter_context(mock.patch.object(integration_jobs, 'unsettled', return_value=[]))
            stack.enter_context(mock.patch.object(MT.terra_priority, 'exclusive', return_value=False))
            stack.enter_context(mock.patch.object(MT.terra_priority, 'gate', return_value=terra_block))
            stack.enter_context(mock.patch.object(MT, 'candidates', return_value=candidates or []))
            stack.enter_context(mock.patch.object(MT, 'freshness_gate', return_value=(None, None)))
            stack.enter_context(mock.patch.object(MT, 'log'))
            # Stop before disk checks or any real checkout/build mutation.
            stack.enter_context(mock.patch.object(MT, 'free_gb', side_effect=ReachedBuildChecks))
            return MT.run(dry=dry)

    def test_pending_current_base_repair_starts_without_candidates(self):
        with self.assertRaises(ReachedBuildChecks):
            self.exercise([{'status': 'pending', 'base': 'current-sha'}])

    def test_pending_repair_does_not_wait_for_ordinary_batch_threshold(self):
        with self.assertRaises(ReachedBuildChecks):
            self.exercise([{'status': 'pending', 'base': 'current-sha'}],
                          candidates=[{'cluster': 'one', 'branch': 'fix/one'}])

    def test_stale_and_completed_repairs_do_not_trigger_empty_train(self):
        for row in ({'status': 'pending', 'base': 'old-sha'},
                    {'status': 'promoted', 'base': 'current-sha'},
                    {'status': 'superseded', 'base': 'current-sha'}):
            with self.subTest(row=row):
                self.assertEqual(self.exercise([row]), 0)

    def test_empty_queue_does_not_trigger_train(self):
        self.assertEqual(self.exercise([]), 0)

    def test_terra_gate_still_blocks_pending_repairs(self):
        self.assertEqual(self.exercise([{'status': 'pending', 'base': 'current-sha'}],
                                       terra_block='waiting for sweeps'), 0)

    def test_dry_run_never_enters_build_checks(self):
        self.assertEqual(self.exercise([{'status': 'pending', 'base': 'current-sha'}], dry=True), 0)


if __name__ == '__main__':
    unittest.main()
