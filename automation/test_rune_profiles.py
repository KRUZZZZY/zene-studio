import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

PATH = Path(__file__).resolve().parents[1] / 'terra-pilot/session/session.py'
SPEC = importlib.util.spec_from_file_location('rune_profile_session', PATH)
S = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(S)


class ProfileTests(unittest.TestCase):
    def test_task_filter_selects_only_applicable_terra_cases(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'profile.json'
            cases = [dict(id=x, task_ids=[task], title=x, do='Act', expect='Observe', core=True)
                     for x, task in [('one', 'Q050-505'), ('two', 'Q050-506')]]
            path.write_text(json.dumps({'profile': 'rune-ui', 'uses': cases}))
            profile, uses = S.profile_uses(path, 'Q050-506')
            self.assertEqual(profile, 'rune-ui')
            self.assertEqual([u['id'] for u in uses], ['two'])
            with self.assertRaises(ValueError):
                S.profile_uses(path, 'Q050-999')

    def test_invalid_or_duplicate_cases_cannot_be_counted_as_coverage(self):
        case = dict(id='one', title='One', do='Act', expect='Observe', core=True)
        for value in [{'profile': '../unsafe', 'uses': [case]}, {'profile': 'rune-ui', 'uses': []},
                      {'profile': 'rune-ui', 'uses': [case, case]},
                      {'profile': 'rune-ui', 'uses': [{'id': 'missing-proof'}]}]:
            with self.subTest(value=value), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / 'profile.json'
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    S.profile_uses(path)


if __name__ == '__main__':
    unittest.main()
