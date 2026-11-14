import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import train_backup as B


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.qwen = self.root / 'qwen'
        (self.qwen / 'merge').mkdir(parents=True)
        self.repo = self.root / 'source'
        self.repo.mkdir()
        self.git('init', '-q', '-b', 'base')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.invalid')
        (self.repo / 'code.txt').write_text('baseline\n')
        self.git('add', 'code.txt')
        self.git('commit', '-qm', 'baseline')
        self.base = self.git('rev-parse', 'HEAD')
        (self.repo / 'code.txt').write_text('repair\n')
        self.git('commit', '-qam', 'repair')
        self.sha = self.git('rev-parse', 'HEAD')
        self.ledger = {'baseline_sha': self.base, 'history': [
            {'train': '050/m0-train-1', 'base': 'baseline', 'sha': self.sha,
             'display_version': '0.4.0.1-alpha'}]}
        (self.qwen / 'TRAIN-VERSIONS.json').write_text(json.dumps(self.ledger))
        self.raw = b'99% tests passed, 1 tests failed out of 10\n'
        (self.qwen / 'merge/train-1-bar.log').write_bytes(self.raw)
        (self.qwen / 'merge/train-10-bar.log').write_text('different train\n')
        (self.qwen / 'merge/SOL-REPAIRS.json').write_text(json.dumps([
            {'train': '050/m0-train-1', 'base': self.base, 'commit': self.sha, 'status': 'pending'}]))
        (self.qwen / 'TRAIN-CI-STATUS.json').write_text(json.dumps({'at': 'changing timestamp', 'commits': {
            self.sha: {'status': 'failed', 'runs': [{'url': 'https://example.invalid/run/1'}]}}}))
        (self.qwen / 'train_versions.py').write_text('# recovery copy\n')
        (self.qwen / 'secret-provider-config.json').write_text('never include me\n')
        patch = mock.patch.multiple(B, ROOT=self.qwen, CLONE=self.repo, PROG=self.root / 'program')
        patch.start()
        self.addCleanup(patch.stop)

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], text=True).strip()

    def test_snapshot_pins_source_failure_evidence_and_unmerged_repair_patch(self):
        dest = self.root / 'snapshot'
        B.build_snapshot(dest)
        row = dest / 'trains/m0-train-1'
        self.assertEqual(json.loads((row / 'version.json').read_text())['sha'], self.sha)
        self.assertEqual(json.loads((row / 'ci.json').read_text())['status'], 'failed')
        self.assertIn('repair', (dest / f'repair-patches/{self.sha}.patch').read_text())
        self.assertFalse((row / 'evidence/train-10-bar.log').exists())
        self.assertTrue((dest / 'automation/train_versions.py').exists())
        self.assertFalse(list(dest.rglob('*provider*')))
        self.assertFalse((dest / '.github').exists())

    def test_compression_is_reproducible_and_original_bytes_can_be_verified(self):
        dest = self.root / 'snapshot'
        B.build_snapshot(dest)
        B.compress_evidence(dest)
        row = dest / 'trains/m0-train-1'
        index = json.loads((row / 'evidence-index.json').read_text())[0]
        packed = (row / index['file']).read_bytes()
        self.assertEqual(gzip.decompress(packed), self.raw)
        self.assertEqual(index['sha256_uncompressed'], hashlib.sha256(self.raw).hexdigest())
        self.assertFalse((row / 'evidence/train-1-bar.log').exists())
        self.assertEqual(packed, gzip.compress(self.raw, mtime=0))

    def test_scan_failure_prevents_git_staging_commit_and_push(self):
        with mock.patch.object(B, 'CACHE', self.root / 'cache'), \
                mock.patch.object(B, 'scan', side_effect=RuntimeError('secret found')), \
                mock.patch.object(B, 'sync_repo') as sync:
            with self.assertRaisesRegex(RuntimeError, 'secret found'):
                B.publish()
        sync.assert_not_called()

    def test_unchanged_snapshot_is_identical_despite_poll_timestamp(self):
        first, second = self.root / 'first', self.root / 'second'
        B.build_snapshot(first)
        state = self.qwen / 'TRAIN-CI-STATUS.json'
        data = json.loads(state.read_text())
        data['at'] = 'new poll time'
        state.write_text(json.dumps(data))
        B.build_snapshot(second)
        for path in first.rglob('*'):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())

    def test_missing_validation_history_fails_closed(self):
        (self.qwen / 'TRAIN-VERSIONS.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'promotion history'):
            B.build_snapshot(self.root / 'snapshot')

    def test_real_scanner_allows_only_proof_hashes_in_repair_records(self):
        dest = self.root / 'scan'
        path = dest / 'trains/m0-train-1/repairs.json'
        path.parent.mkdir(parents=True)
        digest = hashlib.sha256(b'public artifact hash').hexdigest()
        path.write_text(json.dumps({'proof_key': digest}))
        report = self.root / 'scan-report.json'
        actual_root = Path(B.__file__).parent
        with mock.patch.object(B, 'ROOT', actual_root):
            B.scan(dest, report)
            self.assertEqual(json.loads(report.read_text()), [])
            # A credential-shaped fixture elsewhere must still block publication.
            token = 'ghp_' + hashlib.sha256(b'not a real credential: regression fixture').hexdigest()[:36]
            (dest / 'credentials.txt').write_text('github_token=' + token + '\n')
            with self.assertRaises(RuntimeError):
                B.scan(dest, report)
        self.assertTrue(json.loads(report.read_text()))


if __name__ == '__main__':
    unittest.main()
