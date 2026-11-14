import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen')
spec=importlib.util.spec_from_file_location('sol_repair_under_test',os.environ.get('SOL_TEST_MODULE',str(Path(__file__).with_name('train_sol.py'))))
S=importlib.util.module_from_spec(spec)
spec.loader.exec_module(S)

class RepairTests(unittest.TestCase):
    def git(self,*args):
        r=subprocess.run(['git','-C',str(self.repo),*args],capture_output=True,text=True)
        if r.returncode: self.fail(r.stderr)
        return r.stdout.strip()

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.repo=self.root/'tree'; self.repo.mkdir()
        self.git('init','-q','-b','base')
        self.git('config','user.name','Test'); self.git('config','user.email','test@example.invalid')
        (self.repo/'tests').mkdir()
        (self.repo/'tests/upstream-modifications.txt').write_text('baseline\n')
        self.git('add','tests/upstream-modifications.txt');self.git('commit','-qm','base')
        self.base=self.git('rev-parse','HEAD')
        self.git('checkout','-qb','failed')
        (self.repo/'failed-candidate-only.txt').write_text('feature from the failed train\n')
        self.git('add','failed-candidate-only.txt'); self.git('commit','-qm','failed feature')
        self.source=self.git('rev-parse','HEAD')
        self.runs=self.root/'runs';self.runs.mkdir()
        self.patches=mock.patch.multiple(S,WT=self.repo,TRAIN_WT=self.repo,RUNS=self.runs,
             SCRATCH=self.root/'scratch',REPAIRS=self.root/'repairs.json',REPAIR_LOCK=self.root/'repairs.lock')
        self.patches.start();self.addCleanup(self.patches.stop)
        self.job={'base':'base','train':'failed'}
        self.repair={'summary':'declare vendor source','checks':[]}

    def queue(self):
        p=self.repo/'tests/upstream-modifications.txt';p.write_text(p.read_text()+'vendor.c\tprovenance\n')
        with mock.patch.object(S,'verify_repair',return_value=[{'exit':0}]) as check:
            row=S.queue_repair(self.job,self.repair,self.source)
        check.assert_called_once()
        return row

    def test_verified_repair_is_preserved_and_queued(self):
        row=self.queue()
        self.assertEqual(row['base'],self.base)
        self.assertEqual(row['source_head'],self.source)
        self.assertEqual(self.git('rev-parse',row['commit']+'^'),self.source)
        self.assertEqual(self.git('diff-tree','--no-commit-id','--name-only','-r',row['commit']), 'tests/upstream-modifications.txt')
        self.assertEqual(json.loads(S.REPAIRS.read_text())[0]['status'],'pending')
        self.assertEqual(self.git('status','--porcelain'),'')

    def test_next_train_gets_only_repair_not_failed_candidate(self):
        self.queue()
        self.git('checkout','-qb','next','base')
        (self.repo/'new-selection.txt').write_text('new selected feature\n')
        self.git('add','new-selection.txt');self.git('commit','-qm','new selection')
        with mock.patch.object(S.mt,'git',side_effect=self.git),mock.patch.object(S,'log'):
            S.apply_repairs('base','next')
        self.assertIn('vendor.c', (self.repo/'tests/upstream-modifications.txt').read_text())
        self.assertFalse((self.repo/'failed-candidate-only.txt').exists())
        self.assertTrue((self.repo/'new-selection.txt').exists())

    def test_failed_verification_does_not_queue_and_keeps_work(self):
        p=self.repo/'tests/upstream-modifications.txt';p.write_text('repair\n')
        with mock.patch.object(S,'verify_repair',side_effect=RuntimeError('gate failed')):
            with self.assertRaisesRegex(RuntimeError,'gate failed'):
                S.queue_repair(self.job,self.repair,self.source)
        self.assertFalse(S.REPAIRS.exists())
        self.assertEqual(p.read_text(),'repair\n')
        self.assertNotEqual(self.git('rev-parse','HEAD'),self.source)

    def test_source_change_requires_targeted_ctest(self):
        with self.assertRaisesRegex(ValueError,'targeted ctest'):
            S.verify_repair({'checks':[]},['src/core/Fix.cpp'])

    def test_zero_test_matches_are_an_error(self):
        argv,cwd=S.check_command({'kind':'ctest','regex':'^RealTest$'})
        self.assertIn('--no-tests=error',argv)
        self.assertEqual(cwd,self.repo/'build/tests')
        self.assertIn('-j1',argv)

    def test_command_is_structured_and_cannot_be_shell_text(self):
        with self.assertRaises(ValueError):S.check_command({'kind':'shell','command':'touch /tmp/unchecked'})
        with self.assertRaises(ValueError):S.check_command({'kind':'ctest','regex':''})
        with self.assertRaises(ValueError):S.check_command({'kind':'gate','script':'/tmp/unchecked.sh'})

    def test_existing_declarations_supported_but_gate_rules_not_rewritten(self):
        (self.repo/'tests/example-gate.sh').write_text('exit 0\n')
        with self.assertRaisesRegex(ValueError,'gate enforcement'):
            S.queue_repair(self.job,self.repair,self.source)
        self.assertFalse(S.REPAIRS.exists())

    def test_conflicting_repair_aborts_cleanly_and_is_retained(self):
        self.queue(); self.git('checkout','-qb','next','base')
        (self.repo/'tests/upstream-modifications.txt').write_text('conflicting contents\n')
        self.git('add','tests/upstream-modifications.txt'); self.git('commit','-qm','conflict')
        with mock.patch.object(S.mt,'git',side_effect=self.git):
            with self.assertRaisesRegex(RuntimeError,'preserved'): S.apply_repairs('base','next')
        self.assertEqual(self.git('status','--porcelain'),'')
        self.assertEqual(json.loads(S.REPAIRS.read_text())[0]['status'],'pending')

    def test_promoted_repair_is_not_applied_again(self):
        self.queue()
        with mock.patch.object(S.mt,'git',side_effect=self.git):S.promoted_repairs('base','next')
        self.assertEqual(json.loads(S.REPAIRS.read_text())[0]['status'],'promoted')
        with mock.patch.object(S.mt,'git',return_value=self.base),mock.patch.object(S.subprocess,'run') as run:
            S.apply_repairs('base','next')
        run.assert_not_called()

    def test_wrong_base_does_not_apply_old_repair(self):
        self.queue()
        with mock.patch.object(S.mt,'git',return_value='other-base'),mock.patch.object(S.subprocess,'run') as run:
            S.apply_repairs('base','next')
        run.assert_not_called()

    def test_repair_after_failed_check_includes_all_corrections(self):
        p=self.repo/'tests/upstream-modifications.txt';p.write_text('first repair\n')
        with mock.patch.object(S,'verify_repair',side_effect=RuntimeError('gate failed')):
            with self.assertRaises(RuntimeError):S.queue_repair(self.job,self.repair,self.source)
        (self.repo/'tests/evidence-gate-exempt.txt').write_text('vendor-data.h\tcoefficients\n')
        with mock.patch.object(S,'verify_repair',return_value=[]):row=S.queue_repair(self.job,self.repair,self.source)
        self.assertEqual(self.git('rev-parse',row['commit']+'^'),self.source)
        self.assertEqual(set(row['paths']),{'tests/upstream-modifications.txt','tests/evidence-gate-exempt.txt'})

    def test_same_model_effort_and_session_are_preserved(self):
        cmd=S.codex_cmd(self.root/'out.md','persistent-thread')
        self.assertIn('persistent-thread',cmd)
        self.assertIn('gpt-6.1-sol',cmd)
        self.assertIn('model_reasoning_effort="medium"',cmd)
        self.assertIn('model_auto_compact_token_limit=254000',cmd)

    def test_checkout_preserves_existing_repair_tree(self):
        trees=self.root/'repairs';old=self.repo
        marker=old/'unfinished.txt';marker.write_text('keep me')
        with mock.patch.object(S,'REPAIR_TREES',trees),mock.patch.object(S.td,'CLONE',self.repo):
            new=S.checkout('failed')
        self.assertNotEqual(new,old)
        self.assertEqual(marker.read_text(),'keep me')
        self.assertTrue((new/'.git').exists())

    def test_source_verification_rebuilds_before_running_ctest(self):
        checks={"checks":[{"kind":"ctest","regex":"^Regression$","targets":["zene","Regression"]}]}
        with mock.patch.object(S,'jail_prefix',return_value=[]),mock.patch.object(S,'repair_env',return_value={}), \
                mock.patch.object(S.subprocess,'run',return_value=mock.Mock(returncode=0)) as run:
            proof=S.verify_repair(checks,['src/Fix.cpp'])
        commands=[c.args[0] for c in run.call_args_list]
        self.assertEqual([c[0] for c in commands],['bash','bash','cmake','ctest'])
        self.assertIn('Regression',commands[2])
        self.assertIn('-j12',commands[2])
        self.assertEqual(len(proof),4)

    def test_failed_build_does_not_run_stale_test_binary(self):
        checks={"checks":[{"kind":"ctest","regex":"^Regression$","targets":["Regression"]}]}
        with mock.patch.object(S,'jail_prefix',return_value=[]),mock.patch.object(S,'repair_env',return_value={}), \
                mock.patch.object(S.subprocess,'run',side_effect=[mock.Mock(returncode=0),mock.Mock(returncode=0),mock.Mock(returncode=1)]) as run:
            with self.assertRaisesRegex(RuntimeError,'verification failed'):S.verify_repair(checks,['src/Fix.cpp'])
        self.assertEqual(len(run.call_args_list),3)
        self.assertEqual(run.call_args_list[-1].args[0][0],'cmake')

    def test_actual_zero_test_ctest_is_rejected(self):
        (self.repo/'build/tests').mkdir(parents=True)
        (self.repo/'build/tests/CTestTestfile.cmake').write_text('')
        argv,cwd=S.check_command({'kind':'ctest','regex':'^Missing$'})
        result=subprocess.run(argv,cwd=cwd,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)

    def test_handle_commits_verified_model_edits_and_restarts_train(self):
        def model_edits(prompt,out):
            (self.repo/'tests/upstream-modifications.txt').write_text('baseline\nvendor.c\tprovenance\n')
            return '```json\n'+json.dumps({'repair':self.repair,'summary':'applied repair'})+'\n```','same-thread'
        with mock.patch.object(S,'checkout'),mock.patch.object(S.td,'luna_ok',return_value=True), \
                mock.patch.object(S,'brief',return_value='apply the repair'),mock.patch.object(S,'session',side_effect=model_edits), \
                mock.patch.object(S,'verify_repair',return_value=[{'exit':0}]),mock.patch.object(S,'log'), \
                mock.patch.object(S.mt,'notify'):
            original_run=subprocess.run
            def run(argv,**kwargs):
                if argv[0]=='systemctl':return subprocess.CompletedProcess(argv,0)
                return original_run(argv,**kwargs)
            with mock.patch.object(S.subprocess,'run',side_effect=run) as commands:
                status,detail=S.handle(self.job,[])
        self.assertEqual(status,'done')
        self.assertIn('verified and queued',detail)
        self.assertTrue(S.REPAIRS.exists())
        self.assertTrue(any(c.args[0][:3]==['systemctl','--user','start'] for c in commands.call_args_list))

    def test_failed_verification_returns_to_same_session_for_correction(self):
        prompts=[]
        def model_edits(prompt,out):
            prompts.append(prompt)
            p=self.repo/'tests/upstream-modifications.txt'
            p.write_text('corrected repair\n' if len(prompts)>1 else 'first repair\n')
            return '```json\n'+json.dumps({'repair':self.repair})+'\n```','same-thread'
        original_run=subprocess.run
        def run(argv,**kwargs):
            return subprocess.CompletedProcess(argv,0) if argv[0]=='systemctl' else original_run(argv,**kwargs)
        with mock.patch.object(S,'checkout'),mock.patch.object(S.td,'luna_ok',return_value=True), \
                mock.patch.object(S,'brief',return_value='first prompt'),mock.patch.object(S,'session',side_effect=model_edits), \
                mock.patch.object(S,'verify_repair',side_effect=[RuntimeError('gate failed'),[{'exit':0}]]), \
                mock.patch.object(S,'log'),mock.patch.object(S.mt,'notify'),mock.patch.object(S.subprocess,'run',side_effect=run):
            status,detail=S.handle(self.job,[])
        self.assertEqual(status,'done')
        self.assertEqual(len(prompts),2)
        self.assertIn('gate failed',prompts[1])
        self.assertEqual((self.repo/'tests/upstream-modifications.txt').read_text(),'corrected repair\n')

    def test_conflicting_pending_repair_can_be_replaced_without_deleting_history(self):
        first=self.queue()
        p=self.repo/'tests/upstream-modifications.txt';p.write_text(p.read_text()+'second.c\tcorrected provenance\n')
        repair={**self.repair,'supersedes':[first['id']]}
        with mock.patch.object(S,'verify_repair',return_value=[]):second=S.queue_repair(self.job,repair,self.source)
        rows=json.loads(S.REPAIRS.read_text())
        self.assertEqual(rows[0]['status'],'superseded')
        self.assertEqual(rows[0]['superseded_by'],second['id'])
        self.assertEqual(rows[1]['status'],'pending')
        self.assertEqual(self.git('rev-parse',first['branch']),first['commit'])

    def test_invalid_supersession_never_replaces_existing_repair(self):
        self.queue()
        before=S.REPAIRS.read_text()
        with mock.patch.object(S,'verify_repair',return_value=[]):
            with self.assertRaisesRegex(ValueError,'not pending'):
                S.queue_repair(self.job,{**self.repair,'supersedes':['unknown']},self.source)
        self.assertEqual(S.REPAIRS.read_text(),before)

    def test_exclusions_use_the_accepted_cluster_for_custom_branch_names(self):
        branch='fix/custom-branch'
        output='```json\n'+json.dumps({'exclude':[{'branch':branch,'why':'confirmed regression'}]})+'\n```'
        original_run=subprocess.run
        def run(argv,**kwargs):
            return subprocess.CompletedProcess(argv,0) if argv[0]=='systemctl' else original_run(argv,**kwargs)
        with mock.patch.object(S,'checkout'),mock.patch.object(S.td,'luna_ok',return_value=True), \
                mock.patch.object(S,'brief',return_value='fix it'),mock.patch.object(S,'session',return_value=(output,'thread')), \
                mock.patch.object(S.td,'merges',return_value=[{'branch':branch,'merge':'fake-merge'}]), \
                mock.patch.object(S.mt,'accepted_rows',return_value=[{'branch':branch,'cluster':'actual-issue'}]), \
                mock.patch.object(S.td,'git',return_value='commit'),mock.patch.object(S.mt,'port_job') as port, \
                mock.patch.object(S,'mark_urgent',return_value=[]),mock.patch.object(S,'log'),mock.patch.object(S.mt,'notify'), \
                mock.patch.object(S.subprocess,'run',side_effect=run):
            status,detail=S.handle(self.job,[])
        self.assertEqual(status,'done')
        self.assertEqual(port.call_args.args[0]['cluster'],'actual-issue')

if __name__=='__main__':unittest.main()
