import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen')
spec = importlib.util.spec_from_file_location('watch_under_test', os.environ.get('WATCH_TEST_MODULE', str(Path(__file__).with_name('train_watch.py'))))
W = importlib.util.module_from_spec(spec)
spec.loader.exec_module(W)

class WatchHandoffTests(unittest.TestCase):
    def exercise(self, reported=False, dry=False, seen=False):
        import datetime
        import json
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'FIX-BASE').write_text('050/m0-train-24\n')
            log=root/'merge-train.log'
            log.write_text('rebuild budget exhausted\n')
            state=root/'WATCH.json'
            if seen:
                state.write_text(json.dumps({'seen_run':'123'}))
            svc={'ActiveState':'failed','Result':'exit-code','ExecMainStatus':'1',
                 'InactiveEnterTimestampMonotonic':'123', 'ExecMainStartTimestamp':'Wed 2026-11-04 14:28:49 GMT'}
            fail_t=datetime.datetime(2026,11,4,14,29).timestamp() if reported else 0
            with mock.patch.object(W,'ROOT',root), mock.patch.object(W,'STATE',state), mock.patch.object(W,'DRY',dry), \
                    mock.patch.object(W.mt,'LOG',log), mock.patch.object(W,'show',return_value=svc), \
                    mock.patch.object(W,'last_log',return_value=(fail_t,'')), mock.patch.object(W,'ping'), \
                    mock.patch.object(W.subprocess,'run',return_value=mock.Mock(returncode=0)), \
                    mock.patch.object(W.train_sol,'enabled',return_value=True), \
                    mock.patch.object(W.train_sol,'kick',return_value=False), \
                    mock.patch.object(W.train_sol,'note_failure') as queue:
                W.main()
                return queue.call_args_list

    def test_unreported_service_failure_reaches_sol(self):
        calls=self.exercise()
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0].args[:2],('050/m0-train-25','050/m0-train-24'))
        self.assertIn('merge-train.log',calls[0].args[2])

    def test_reported_failure_is_not_queued_twice(self):
        self.assertEqual(self.exercise(reported=True),[])

    def test_seen_run_is_not_queued_twice(self):
        self.assertEqual(self.exercise(seen=True),[])

    def test_dry_run_does_not_queue_sol(self):
        self.assertEqual(self.exercise(dry=True),[])

if __name__=='__main__': unittest.main()
