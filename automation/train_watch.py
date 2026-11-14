#!/usr/bin/env python3
"""Train watcher (owner 2026-11-03: "have it monitored and pinged every time the train is down"). Runs every 5 minutes
(zene-train-watch.timer) and pings the owner (notify-me) for what the train's own fail() cannot report:

  - a train run that ended badly WITHOUT a "NOT PROMOTED" line (crashed, killed, OOM) - fail() pings the others,
    and every such failure also goes to the Sol queue (train_sol.py);
  - a train that has been running for more than RUNNING_ALERT_H hours;
  - the merge-train timer not active;
  - the train coming back up (a new fix base promoted after a failure);
  - the train postponed for POSTPONED_ALERT_H hours or more (2026-11-13): one ping per streak, and Sol gets the case.
It also restarts the Sol worker when failures are queued and no worker runs (e.g. after an OpenAI usage pause).
Each condition pings once per occurrence (state: merge/WATCH.json).   train_watch.py [--dry-run]
"""
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import merge_train as mt          # noqa: E402
import terra_priority
import train_sol                  # noqa: E402

STATE = mt.M / "WATCH.json"
RUNNING_ALERT_H = 4
REPEAT_BLOCKS = 3               # owner 2026-11-13: the same NOT PROMOTED reason this many times in a row = a loop
POSTPONED_ALERT_H = 6           # owner 2026-11-13: a train postponed this long pings the owner and goes to Sol
DRY = "--dry-run" in sys.argv


def ping(text):
    print("PING:", text)
    if not DRY:
        mt.notify(text)


def show(unit, *props):
    out = subprocess.run(["systemctl", "--user", "show", unit, "-p", ",".join(props)], capture_output=True, text=True).stdout
    return dict(l.split("=", 1) for l in out.splitlines() if "=" in l)


def usec(v):
    try:
        return int(v) / 1e6
    except (TypeError, ValueError):
        return 0.0


def last_log(pattern):
    """(epoch, line) of the last merge-train log line matching pattern."""
    try:
        lines = mt.LOG.read_text(errors="replace").splitlines()
    except OSError:
        return 0.0, ""
    year = datetime.date.today().year
    for line in reversed(lines):
        if re.search(pattern, line):
            m = re.match(r"\[(\d\d-\d\d \d\d:\d\d:\d\d)\]", line)
            if m:
                return datetime.datetime.strptime(f"{year}-{m.group(1)}", "%Y-%m-%d %H:%M:%S").timestamp(), line
            return 0.0, line
    return 0.0, ""


def postponed_streak():
    """(first epoch, latest reason) of the "train postponed" lines since the last train that actually ran or found
    nothing to merge; (0, "") when the latest run was not postponed."""
    try:
        lines = mt.LOG.read_text(errors="replace").splitlines()
    except OSError:
        return 0.0, ""
    year, since, reason = datetime.date.today().year, 0.0, ""
    for line in reversed(lines):
        if "train postponed" in line:
            m = re.match(r"\[(\d\d-\d\d \d\d:\d\d:\d\d)\]", line)
            if m:
                since = datetime.datetime.strptime(f"{year}-{m.group(1)}", "%Y-%m-%d %H:%M:%S").timestamp()
            reason = reason or line.split("] ", 1)[-1]
        elif re.search(r"accepted commits waiting for|no train due|NOT PROMOTED|promoted 050/", line):
            break
    return since, reason


def repeat_block_streak():
    """(count, first epoch, reason) of the consecutive NOT PROMOTED lines, since the last promotion, that give the same
    reason once train numbers, hashes and counts are ignored. Train 28 was refused for the same Terra reason every
    3 hours for a day; each ping looked new because it named a different merge count and hash."""
    try:
        lines = mt.LOG.read_text(errors="replace").splitlines()
    except OSError:
        return 0, 0.0, ""
    year, count, first, key, reason = datetime.date.today().year, 0, 0.0, None, ""
    for line in reversed(lines):
        if re.search(r"promoted 050/", line) and "NOT PROMOTED" not in line:
            break
        if "NOT PROMOTED" not in line:
            continue
        text = line.split("NOT PROMOTED", 1)[1].split(": ", 1)[-1]
        norm = re.sub(r"[0-9a-f]{8,}|\d+", "#", text)[:200]
        if key is None:
            key, reason = norm, text
        elif norm != key:
            break
        count += 1
        m = re.match(r"\[(\d\d-\d\d \d\d:\d\d:\d\d)\]", line)
        if m:
            first = datetime.datetime.strptime(f"{year}-{m.group(1)}", "%Y-%m-%d %H:%M:%S").timestamp()
    return count, first, reason


def main():
    st = json.loads(STATE.read_text()) if STATE.exists() else {}
    now = datetime.datetime.now().timestamp()
    base = (ROOT / "FIX-BASE").read_text().strip()
    if not DRY:
        terra_priority.sync(base)
    svc = show("zene-merge-train.service", "ActiveState", "Result", "ExecMainStatus",
               "ExecMainStartTimestampMonotonic", "ExecMainStartTimestamp", "InactiveEnterTimestampMonotonic")
    started = svc.get("ExecMainStartTimestamp", "")

    # 1. a finished run that failed without fail()'s own ping
    run_id = svc.get("InactiveEnterTimestampMonotonic", "")
    if svc.get("ActiveState") in ("inactive", "failed") and run_id and run_id != st.get("seen_run"):
        st["seen_run"] = run_id
        if svc.get("Result") not in ("success", "", None):
            fail_t, _ = last_log(r"NOT PROMOTED")
            start_t = datetime.datetime.strptime(started, "%a %Y-%m-%d %H:%M:%S %Z").timestamp() if started else 0.0
            if fail_t < start_t - 5:
                tail = " | ".join(l[:160] for l in mt.LOG.read_text(errors="replace").splitlines()[-3:])
                ping(f"Merge train crashed ({svc.get('Result')}, exit {svc.get('ExecMainStatus')}) without a verdict; "
                     f"fix base stays {base}. Last log: {tail}")
                if train_sol.enabled() and not DRY:
                    number = int(re.search(r"train-(\d+)$", base).group(1)) + 1
                    train_sol.note_failure(
                        f"050/m0-train-{number}", base,
                        f"service failed without a train verdict ({svc.get('Result')}, "
                        f"exit {svc.get('ExecMainStatus')}); last log: {tail}; "
                        f"see {mt.M / ('train-' + str(number) + '-bar.log')}; orchestration log {mt.LOG}")
                st["down_since"] = st.get("down_since") or now
            else:
                st["down_since"] = st.get("down_since") or now

    # 2. a train running far too long
    if svc.get("ActiveState") in ("active", "activating"):
        run_start = svc.get("ExecMainStartTimestampMonotonic")
        up = subprocess.run(["cat", "/proc/uptime"], capture_output=True, text=True).stdout.split()[0]
        hours = (float(up) - usec(run_start)) / 3600 if run_start else 0
        if hours > RUNNING_ALERT_H and st.get("long_run_told") != run_start:
            ping(f"Merge train has been running for {hours:.1f} h (started {started}); it may be stuck.")
            st["long_run_told"] = run_start

    # 3. the timer itself
    timer_on = subprocess.run(["systemctl", "--user", "is-active", "--quiet", "zene-merge-train.timer"]).returncode == 0
    if not timer_on and not st.get("timer_off_told"):
        ping("Merge train timer is not active: no trains will run until it is started again.")
        st["timer_off_told"] = True
    elif timer_on:
        st.pop("timer_off_told", None)

    # 4. back up: a new fix base promoted
    if st.get("base") and base != st["base"]:
        if st.get("down_since"):
            hrs = (now - st["down_since"]) / 3600
            ping(f"Merge train is back up: {base} promoted (it was down {hrs:.1f} h since the last failure).")
        st.pop("down_since", None)
    st["base"] = base
    fail_t, _ = last_log(r"NOT PROMOTED")
    if fail_t and fail_t > st.get("last_fail_seen", 0):
        # a failure newer than the last one seen (the base change above already cleared an older down period)
        st["last_fail_seen"] = fail_t
        st["down_since"] = st.get("down_since") or fail_t

    # 5. postponed for hours (owner 2026-11-13: Terra's gate held train 28 for a day and nobody was told)
    since, reason = postponed_streak()
    if since and (now - since) / 3600 >= POSTPONED_ALERT_H:
        if st.get("postponed_told") != since:
            hours = (now - since) / 3600
            ping(f"Merge train postponed for {hours:.0f} h (fix base {base}): {reason[:300]}. Sol is looking at it.")
            if train_sol.enabled() and not DRY:
                train_sol.note_postponement(base, reason, hours)
            st["postponed_told"] = since
    elif not since:
        st.pop("postponed_told", None)

    # 5b. the same refusal again and again
    count, first, reason = repeat_block_streak()
    if count >= REPEAT_BLOCKS:
        if st.get("repeat_told") != first:
            ping(f"Merge train refused {count} times in a row for the same reason (fix base {base}): {reason[:300]}. "
                 "Retrying will not change it; something upstream needs fixing.")
            st["repeat_told"] = first
    else:
        st.pop("repeat_told", None)

    # 6. queued Sol work with no worker
    if train_sol.enabled() and not DRY and train_sol.kick():
        mt.log("sol: watcher restarted the Sol worker for queued failures")

    STATE.write_text(json.dumps(st, indent=1) + "\n")


if __name__ == "__main__":
    main()
