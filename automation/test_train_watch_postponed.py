"""Owner 2026-11-13: a train postponed for hours pings the owner once per streak and goes to Sol's persistent session."""
import datetime
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.argv = ["train_watch.py"]
import train_sol  # noqa: E402
import train_watch as W  # noqa: E402


def stamp(hours_ago):
    return (datetime.datetime.now() - datetime.timedelta(hours=hours_ago)).strftime("[%m-%d %H:%M:%S]")


class PostponedStreakTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = Path(self.tmp.name) / "merge-train.log"
        p = mock.patch.object(W.mt, "LOG", self.log)
        p.start()
        self.addCleanup(p.stop)

    def write(self, *lines):
        self.log.write_text("\n".join(lines) + "\n")

    def test_streak_starts_at_the_first_postponement_after_the_last_real_run(self):
        self.write(f"{stamp(20)} promoted 050/m0-train-27 @ abc: 40 merges, 3 features",
                   f"{stamp(14)} train postponed: Terra priority: waiting for completed buttons sweeps",
                   f"{stamp(11)} train postponed: Terra priority: waiting for completed buttons sweeps",
                   f"{stamp(1)} train postponed: only 30 GB free - train postponed")
        since, reason = W.postponed_streak()
        self.assertAlmostEqual((datetime.datetime.now().timestamp() - since) / 3600, 14, delta=0.1)
        self.assertIn("only 30 GB free", reason)          # the newest reason is the one reported

    def test_no_streak_after_a_real_run(self):
        self.write(f"{stamp(5)} train postponed: Terra priority: x",
                   f"{stamp(1)} 12 accepted commits waiting for 050/m0-train-27 (base is 9 h old)")
        self.assertEqual(W.postponed_streak(), (0.0, ""))

    def test_nothing_to_merge_ends_a_streak(self):
        self.write(f"{stamp(9)} train postponed: x", f"{stamp(2)} no train due (nothing to merge)")
        self.assertEqual(W.postponed_streak()[0], 0.0)


class SolPostponementTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.queue = Path(self.tmp.name) / "SOL-QUEUE.jsonl"
        for name, value in (("QUEUE", self.queue), ("kick", lambda: False), ("log", lambda msg: None)):
            p = mock.patch.object(train_sol, name, value)
            p.start()
            self.addCleanup(p.stop)

    def test_note_postponement_queues_a_postponed_job_for_the_next_train(self):
        train_sol.note_postponement("050/m0-train-27", "Terra priority: waiting", 14.2)
        job = json.loads(self.queue.read_text())
        self.assertEqual((job["kind"], job["train"], job["base"]), ("postponed", "050/m0-train-28", "050/m0-train-27"))
        self.assertIn("14 h", job["why"])

    def test_note_failure_still_ignores_postponements(self):
        train_sol.note_failure("050/m0-train-28", "050/m0-train-27", "train postponed: x")
        self.assertFalse(self.queue.exists())

    def test_a_postponed_job_is_answered_without_a_train_checkout(self):
        job = {"train": "050/m0-train-28", "base": "050/m0-train-27", "kind": "postponed", "why": "x", "at": "t"}
        answer = '```json\n{"urgent": [], "owner_decision": "relax nothing", "summary": "S."}\n```'
        with mock.patch.object(train_sol, "checkout") as checkout, \
                mock.patch.object(train_sol, "session", return_value=(answer, "thread")), \
                mock.patch.object(train_sol, "postponed_brief", return_value="brief"), \
                mock.patch.object(train_sol.reset_reserve, "paused", return_value=False), \
                mock.patch.object(train_sol.td, "luna_ok", return_value=True), \
                mock.patch.object(train_sol.mt, "notify") as notify, \
                mock.patch.object(train_sol, "RUNS", Path(self.tmp.name)):
            status, detail = train_sol.handle_postponed(job, [])
        checkout.assert_not_called()
        self.assertEqual(status, "done")
        self.assertIn("owner: relax nothing", detail)
        self.assertIn("Needs you: relax nothing", notify.call_args[0][0])


if __name__ == "__main__":
    unittest.main()


class RepeatBlockTests(PostponedStreakTests):
    def test_same_reason_with_different_hashes_counts_as_a_streak(self):
        self.write(f"{stamp(9)} NOT PROMOTED 050/m0-train-28: Terra priority: 1 findings still need accepted fixes: gui-x",
                   f"{stamp(6)} merged 38 onto 050/m0-train-28 (f3f1b99fd3)",
                   f"{stamp(5)} NOT PROMOTED 050/m0-train-28: Terra priority: 1 findings still need accepted fixes: gui-x",
                   f"{stamp(1)} NOT PROMOTED 050/m0-train-29: Terra priority: 1 findings still need accepted fixes: gui-x")
        count, first, reason = W.repeat_block_streak()
        self.assertEqual(count, 3)
        self.assertIn("gui-x", reason)

    def test_a_different_reason_or_a_promotion_ends_it(self):
        self.write(f"{stamp(9)} NOT PROMOTED 050/m0-train-28: gates that passed now fail: 3",
                   f"{stamp(1)} NOT PROMOTED 050/m0-train-28: Terra priority: 1 findings still need accepted fixes: gui-x")
        self.assertEqual(W.repeat_block_streak()[0], 1)
        self.write(f"{stamp(3)} NOT PROMOTED 050/m0-train-28: same", f"{stamp(2)} promoted 050/m0-train-28 @ abc: 40 merges",
                   f"{stamp(1)} NOT PROMOTED 050/m0-train-29: same")
        self.assertEqual(W.repeat_block_streak()[0], 1)
