#!/usr/bin/env python3
"""Daily review agent (owner 2026-11-13: "spawn an auto sonnet medium agent once a day, like 5 pm, to review the work that
was done during the day, and fix any problems it comes across ... inject more rules for testing").

  python3 daily_review.py            run now (the systemd timer zene-daily-review does this at 17:00)
  python3 daily_review.py --dry-run  write and print the brief only, start nothing
  python3 daily_review.py --smoke    start the agent with a one-line prompt to prove the launch path works

What it does: gathers the last 24 h of pipeline evidence (Luna rejections with their reasons, duet runs, lane outcomes,
driver errors) into a brief, then runs ONE Claude Sonnet 5.5 session at medium effort through tools/haiku/haiku-exec
(Landlock write-jail: the pipeline repo, /tmp, caches). The session may fix the PIPELINE (briefs, prompts, checkers,
duet settings, offline tests) and must report; it never touches the product trees, lane switches, the quota brakes or
running processes. Report: qwen/daily-review/YYYY-MM-DD.md; one Telegram line via notify-me.
"""
import datetime
import json
import prompt_context
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
OUT = ROOT / "daily-review"
PROG = Path.home() / "Documents/AI_KOS_PROJECT/projects/zene-studio-program"
HAIKU_EXEC = PROG / "tools/haiku/haiku-exec"
MODEL, EFFORT = "claude-sonnet-5-5", "medium"
TIMEOUT = 75 * 60


def since_iso(hours=24):
    return (datetime.datetime.now() - datetime.timedelta(hours=hours)).isoformat(timespec="seconds")


def jsonl(path):
    rows = []
    try:
        for line in Path(path).read_text().splitlines():
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    except OSError:
        pass
    return rows


def evidence(since):
    """A compact digest of the day; the agent opens the files for detail."""
    out = []
    rej = [r for r in jsonl(ROOT / "REJECTIONS.jsonl") if str(r.get("at", "")) >= since]
    out.append(f"## Luna rejections in the last 24 h: {len(rej)}")
    cats = Counter(b["category"] for r in rej for b in r.get("blocking") or [])
    out.append("by category: " + ", ".join(f"{k} {v}" for k, v in cats.most_common()) if cats else "none")
    for r in rej[:25]:
        for b in (r.get("blocking") or [])[:2]:
            out.append(f"- {r.get('cluster')} r{r.get('round')} [{r.get('model')}] {b['category']}: {b['what'][:260]}")
    duets = [d for d in jsonl(REPO / "duet-runs.jsonl") if d.get("type") == "duet_summary" and str(d.get("at", "")) >= since
             and "duet-eval" not in str(d.get("run_dir", ""))]       # the toy eval is not work
    out.append(f"\n## Duet runs in the last 24 h: {len(duets)}")
    for d in duets:
        roles = d.get("roles", {})
        out.append(f"- {d['at'][11:16]} {d['pairing']} {d['outcome']} ({d.get('reason', '')[:50]}) cycles {d.get('cycles')} "
                   f"{d.get('minutes')} min ${d.get('api_cost')} stopped(worker) {roles.get('worker', {}).get('stopped')} "
                   f"run {d.get('run_dir')}")
    lanes = [r for r in jsonl(ROOT / "CLAUDE-LANES.jsonl") if str(r.get("finished", "")) >= since]
    out.append(f"\n## Claude lane rounds finished in the last 24 h: {len(lanes)}")
    for r in lanes[:30]:
        out.append(f"- {r.get('key')} {r.get('model')} {r.get('outcome')} {r.get('minutes')} min")
    errs = Counter()
    for f in (ROOT / "state").glob("*.json"):
        try:
            if f.stat().st_mtime > datetime.datetime.fromisoformat(since).timestamp():
                o = str(json.loads(f.read_text()).get("outcome", ""))
                if o.lower().startswith(("driver-error", "build-failed", "no-result")):
                    errs[o[:60]] += 1
        except (OSError, ValueError):
            pass
    out.append(f"\n## Units that ended in driver-error, build-failed or no-result in the last 24 h: {sum(errs.values())}")
    out += [f"- {k}: {v}" for k, v in errs.most_common(8)]
    return "\n".join(out)


BRIEF = """You are the DAILY REVIEWER of the Zene Studio fix/feature pipeline (a Claude Sonnet 5.5 session at medium effort,
started by a timer at 17:00). Today is {today}. Your job: find why work failed or was sent back in the last 24 hours,
find the patterns, and fix the PIPELINE so the same failure is less likely tomorrow. You work in {repo}, a git repo.

## Evidence for the last 24 hours (a digest; open the files for detail)
{evidence}

Where the facts live: qwen/REJECTIONS.jsonl (every Luna rejection with category and evidence), duet-runs.jsonl and
qwen/runs/<unit>/duet/*/calls.jsonl (per-call kills and costs), qwen/runs/<unit>/ (brief.md, session logs, final.md,
record.json), qwen/CLAUDE-LANES.jsonl, qwen/BENCHMARK.md, qwen/state/*.json (unit outcomes). Earlier analysis and what was
already changed: qwen/daily-review/*.md and git log in this repo.

## What to do
1. Read the evidence. Group failures by cause, not by unit. A pattern needs at least 3 instances (or 2 plus a clear
   mechanism); do not act on one-offs. Do not re-fix what an earlier daily report or a git commit already fixed: check.
2. For each pattern, make the smallest change that would have prevented it, in this order of preference:
   a) a mechanical check in qwen/submit_check.py (WARN, or FAIL only when the check cannot be wrong), with a test in
      qwen/test_submit_check.py;
   b) a sharper rule or example in the briefs the workers get (qwen/features_queue.py / qwenfix.py templates, the
      "Definition of done" text) or in duet/prompts/*.md; add testing rules here when failures show tests proving nothing;
   c) a setting in duet/duet.json or a limit in the duet code, with an offline test.
   Cite the evidence (unit ids, counts) in the commit message. Keep each change small and reversible.
3. Run the offline tests for everything you touched (python3 -m pytest -q in duet/ and qwen/ for the files involved)
   and show they pass. Never weaken or delete a test.
4. Commit locally with `git add <only the files you changed>` (the repo has many modified files that are not yours: never
   `git add -A`, never commit them). End the message with the line
   `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
5. Write the report to {report} (markdown): what failed today (counts, causes), what you changed (files, why, evidence),
   what you saw but left alone and why, and anything the owner must decide. Facts only, with file:line or run-id for
   each claim; say what you could not verify. First line: `CHANGED n files` or `NO CHANGE`.

## Hard limits
- Edit only inside {repo} (pipeline code, prompts, checkers, duet config and tests). Never edit the product trees
  (zene-*/ worktrees, the product clone), never run a build or a full test suite, and use no more than ~60 tool calls.
- Do not change qwen/LANES.json lane enablement or tiers, quota brakes or caps (usage monitor, LUNA-CAP, CLAUDE-ACCOUNT-LIMITS),
  systemd units, or routing/model choices: report those as recommendations. Owner rules in force: features only, no bug
  lanes, Luna is the only reviewer (never Sonnet or Haiku reviews), no Haiku coders, never remove tests/gates.
- Never kill a process, never delete files you did not create, never send messages (the timer notifies the owner).
- Changes that raise the cost of a worker run, or that slow every unit, need a measured reason in the report.
- If the day was quiet or no pattern qualifies, make no change and say so: that is a good outcome.
"""


def main():
    dry, smoke = "--dry-run" in sys.argv, "--smoke" in sys.argv
    OUT.mkdir(exist_ok=True)
    today = datetime.date.today().isoformat()
    report = OUT / f"{today}.md"
    brief = BRIEF.format(today=today, repo=REPO, evidence=evidence(since_iso()), report=report)
    (OUT / f"{today}.brief.md").write_text(brief)
    if dry:
        print(brief)
        return 0
    if report.exists() and not smoke:
        print(f"daily-review: {report} already exists; not running twice today")
        return 0
    env = dict(os.environ, HAIKU_MODEL=MODEL, HAIKU_EFFORT=EFFORT, HAIKU_FALLBACK="", HAIKU_LEAN="1",
               HAIKU_TOOLS="Read,Edit,Write,Bash,Grep,Glob", HAIKU_AUTOCOMPACT="off", HAIKU_ACCOUNT="personal")
    prompt = "Reply with the single word OK." if smoke else brief
    prompt = prompt_context.stamp(prompt)
    log = OUT / f"{today}{'.smoke' if smoke else ''}.session.log"
    try:
        with open(log, "w") as lf:
            rc = subprocess.run([sys.executable, str(HAIKU_EXEC), "-C", str(REPO), "-s", "danger-full-access",
                                 "-o", str(OUT / f"{today}.final.txt"), "-"], input=prompt, text=True, env=env,
                                stdout=lf, stderr=subprocess.STDOUT, timeout=TIMEOUT).returncode
    except subprocess.TimeoutExpired:
        rc = 124
    if smoke:
        print("smoke rc", rc, "log", log)
        return rc
    first = report.read_text().splitlines()[0] if report.exists() and report.read_text().strip() else None
    if rc == 2:
        msg = "daily review hit the Claude usage limit; nothing ran today"
    elif first is None:
        msg = f"daily review ended (rc {rc}) without a report; see {log}"
    else:
        msg = f"daily review: {first[:120]} - {report}"
    subprocess.run(["notify-me", msg], capture_output=True)
    print(msg)
    return 0 if first else 1


if __name__ == "__main__":
    sys.exit(main())
