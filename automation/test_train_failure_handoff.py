import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen')
spec = importlib.util.spec_from_file_location('handoff_under_test', os.environ.get('TRAIN_TEST_MODULE', str(Path(__file__).with_name('merge_train.py'))))
MT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MT)

class FailureHandoffTests(unittest.TestCase):
    def exercise(self, outcomes, dry=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'FIX-BASE').write_text('050/m0-train-24\n')
            with mock.patch.object(MT, 'ROOT', root), mock.patch.object(MT, 'run', side_effect=outcomes) as run, \
                    mock.patch.object(MT, 'log'), mock.patch.object(MT, 'git'), \
                    mock.patch.object(MT, 'gitm', return_value=['git', 'no-op']), \
                    mock.patch.object(MT.subprocess, 'run'), mock.patch.object(MT, 'fail', return_value=1) as fail:
                rc = MT.execute_train(False, dry, False)
                return rc, run.call_args_list, fail.call_args_list

    def test_exhausted_rebuilds_handoff_latest_failure_once(self):
        rc, runs, failures = self.exercise([MT.RebuildWithout('first'), MT.RebuildWithout('second')])
        self.assertEqual(rc, 1)
        self.assertEqual(len(runs), 2)
        self.assertTrue(runs[1].args[0])
        self.assertEqual(len(failures), 1)
        new, base, reason = failures[0].args
        self.assertEqual((new, base), ('050/m0-train-25', '050/m0-train-24'))
        self.assertIn('second', reason)
        self.assertIn('rebuild', reason)
        self.assertIn('train-25-bar.log', reason)

    def test_unexpected_exception_handoffs_once(self):
        rc, _, failures = self.exercise([RuntimeError('snapshot broke')])
        self.assertEqual(rc, 1)
        self.assertEqual(len(failures), 1)
        self.assertIn('snapshot broke', failures[0].args[2])

    def test_successful_rebuild_does_not_report_failure(self):
        rc, runs, failures = self.exercise([MT.RebuildWithout('first'), 0])
        self.assertEqual(rc, 0)
        self.assertEqual(len(runs), 2)
        self.assertEqual(failures, [])

    def test_normal_failure_is_not_reported_twice(self):
        rc, _, failures = self.exercise([1])
        self.assertEqual(rc, 1)
        self.assertEqual(failures, [])

    def test_failed_dry_run_does_not_queue_repair(self):
        rc, _, failures = self.exercise([RuntimeError('git unavailable')], dry=True)
        self.assertEqual(rc, 1)
        self.assertEqual(failures, [])

    def test_failure_uses_enabled_sol_handoff(self):
        sol = mock.Mock()
        sol.enabled.return_value = True
        with mock.patch.dict(sys.modules, {'train_sol': sol}), mock.patch.object(MT, 'git'), \
                mock.patch.object(MT, 'log'), mock.patch.object(MT, 'notify'):
            self.assertEqual(MT.fail('050/m0-train-25', '050/m0-train-24', 'rebuilds exhausted'), 1)
        sol.note_failure.assert_called_once_with('050/m0-train-25', '050/m0-train-24', 'rebuilds exhausted')

if __name__ == '__main__':
    unittest.main()
