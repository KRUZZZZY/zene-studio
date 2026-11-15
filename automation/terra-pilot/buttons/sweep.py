#!/usr/bin/env python3
"""Every-button GUI sweep, run after each promoted train (owner 2026-10-30: "expand it so that every button has a
click and an expected outcome, and just have it run after each train then report the ones that dont work as bugs,
where luna then picks it up and figures out what the bug is, then it goes onto the queue, and if a bug is marked as
urgent then a haiku lane fixes it no matter what").

  sweep.py run --train NAME [--binary PATH] [--limit N]     the whole pipeline (merge_train.promote starts it)
  sweep.py inventory|expect|drive|judge|triage --train NAME   one stage, on the run folder of that train

Stages (all state in runs/<train>/; nothing is pushed, nothing in the product is instrumented):
  1. inventory - Zene (the pinned install from pin_latest.py) runs on Xvnc+xcb inside a PRIVATE D-Bus session with its
     own AT-SPI bus; inventory.py lists every clickable control of each VIEW (main window, each bottom tab, each
     main menu). Stubs for xdg-open/kde-open/gio sit first on PATH, so no click can open the owner's browser.
  2. expect    - a control new to expectations.json (keyed by view + role + name + tooltip) gets its expected outcome
     from Luna (read-only, reads the source): one sentence a screenshot can confirm, or skip=true for controls that
     quit the app, leave the machine, or cannot be judged from the screen. Cached across trains.
  3. drive     - scripted, no model: a fresh Zene for EACH control, view preparation, exact parent/identity and
     screen-bound checks, screenshot, click, screenshot, then liveness (process + control.ping) and new error lines.
     Unperformed/ambiguous clicks are incomplete coverage, never product passes or confirmed product defects.
  4. judge     - GPT 6.1 Terra looks at each before|after pair (the control boxed in red) against the expected
     outcome: PASS / FAIL / UNSURE. A crash, hang or missing control is a FAIL without asking.
  5. triage    - Luna (read-only, the train's tree) takes the FAILs in small batches and finds the cause in the code:
     real or not, title, file:line, repro, difficulty, and URGENT (crash/hang, lost work, or a core DAW action -
     play/stop/record/loop/seek/notes/tracks/save/open/undo - broken). Real ones are written to
     research/reviews/terra-gui/queue/{easy,needs_context,hard}.jsonl, which the fix loop reads like the patrol's
     queue; urgent ones also go to qwen/URGENT-BUGS.txt, which the haiku-urgent lane takes regardless of BUGS-HOLD,
     the lane cap, the 40% five-hour brake and FEATURES-ON.
The owner gets one Telegram line per sweep. Luna/Terra use the OpenAI plan: the sweep waits while qwenfix's Luna
limit pause (LUNA-LIMIT-UNTIL) is set.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path.home() / ".local/share/zene-bunny-fix/qwen"))
import prompt_context

HERE = Path(__file__).resolve().parent
PILOT = HERE.parent
ROOT = PILOT.parent / "qwen"
PROG = Path.home() / "Documents/AI_KOS_PROJECT/projects/zene-studio-program"
TREE = PROG / "zene-cachyos"                       # the train's tree (FIX-BASE) - Luna reads the source here
BINARY = Path.home() / ".local/bin/zene-studio-latest"
VENV_PY = PILOT / "venv/bin/python"
GUI = PILOT / "gui.py"
LUNA = PROG / "tools/luna/luna-exec"
QUEUE = PROG / "research/reviews/terra-gui"
EXPECT = HERE / "expectations.json"
URGENT = ROOT / "URGENT-BUGS.txt"
EXPECT_SCHEMA = 2
JUDGE_BATCH = 12
TRIAGE_BATCH = 4
SIZE = "1600x1000"

# A view: the clicks (role, name) that open it from a fresh start. Main-menu views are added at run time from the
# menu bar the inventory finds.
VIEWS = [("main", []),
         ("tab-mixer", [("page tab", "Mixer")]),
         ("tab-devices", [("page tab", "Devices")]),
         ("tab-automation", [("page tab", "Automation")]),
         ("tab-pattern", [("page tab", "Pattern Editor")]),
         ("view-mix", [("check box", "Mix")]),
         ("view-edit", [("check box", "Edit")])]
ERROR_LINE = re.compile(r"(?i)\b(error|assert|fatal|refused|segfault|abort|exception|failed)\b")
NOISE = re.compile(r"(?i)propagateSizeHints|does not support raise|kicker|wildcard call disconnects|"
                   r"jack server|JackShm|Cannot connect to server")


def log(run, msg):
    line = f"[{datetime.datetime.now():%m-%d %H:%M:%S}] {msg}"
    print(line, flush=True)
    with open(run / "sweep.log", "a") as f:
        f.write(line + "\n")


def notify(text):
    subprocess.run(["notify-me", text[:600]], capture_output=True)


def jsonl(path):
    try:
        return [json.loads(l) for l in Path(path).read_text().splitlines() if l.strip()]
    except OSError:
        return []


def write_jsonl(path, rows):
    tmp = Path(str(path) + ".tmp")
    tmp.write_text("".join(json.dumps(r) + "\n" for r in rows))
    tmp.replace(path)


def openai_wait(run):
    """Luna and Terra share the OpenAI plan: wait while qwenfix's Luna pause is set (up to 12 h, then give up)."""
    f = ROOT / "LUNA-LIMIT-UNTIL"
    for _ in range(72):
        try:
            until = datetime.datetime.fromisoformat(f.read_text().strip())
        except (OSError, ValueError):
            return True
        if until <= datetime.datetime.now():
            return True
        log(run, f"OpenAI plan paused until {until:%H:%M}; waiting")
        time.sleep(600)
    return False


# ------------------------------------------------------------------------------------------------- the session
class Session:
    """A private D-Bus session with its own AT-SPI bus, inside which Zene runs on Xvnc (gui.py)."""

    def __init__(self, run, binary):
        self.run, self.binary = run, binary
        self.state = run / "gui-state.json"
        self.env = dict(os.environ)
        stubs = run / "stubs"
        stubs.mkdir(exist_ok=True)
        for name in ("xdg-open", "kde-open", "kde-open5", "gio", "firefox", "zen-browser", "sensible-browser"):
            p = stubs / name
            p.write_text(f'#!/bin/sh\necho "$(date +%T) {name} $*" >> "{run}/opened-urls.log"\nexit 0\n')
            p.chmod(0o755)
        self.env.update(PATH=f"{stubs}:{self.env.get('PATH', '')}", BROWSER=str(stubs / "xdg-open"),
                        TERRA_GUI_STATE=str(self.state), XDG_CURRENT_DESKTOP="", KDE_FULL_SESSION="")
        self.env.pop("LMMS_PLUGIN_DIR", None)
        self.dbus = None

    def __enter__(self):
        out = subprocess.run(["dbus-daemon", "--session", "--fork", "--print-address=1", "--print-pid=1",
                              "--nopidfile"], capture_output=True, text=True, check=True).stdout.split()
        self.env["DBUS_SESSION_BUS_ADDRESS"], self.dbus = out[0], int(out[1])
        a11y = dict(self.env)
        a11y.pop("DISPLAY", None)
        self.helpers = [subprocess.Popen(["/usr/lib/at-spi-bus-launcher", "--launch-immediately"], env=a11y,
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)]
        time.sleep(2)
        self.helpers.append(subprocess.Popen(["/usr/lib/at-spi2-registryd"], env=a11y, stdout=subprocess.DEVNULL,
                                             stderr=subprocess.DEVNULL, start_new_session=True))
        time.sleep(1)
        return self

    def __exit__(self, *exc):
        self.stop()
        for p in self.helpers:
            p.terminate()
        if self.dbus:
            try:
                os.kill(self.dbus, signal.SIGTERM)
            except ProcessLookupError:
                pass

    def gui(self, *args, timeout=60):
        result = subprocess.run([str(VENV_PY), str(GUI), *map(str, args)], env=self.env, capture_output=True,
                                text=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError(f"GUI command {args[0]} failed: {(result.stderr or result.stdout)[-500:]}")
        return result

    def start(self):
        self.stop()
        r = self.gui("start", self.binary, "--size", SIZE, timeout=120)
        if r.returncode != 0:
            raise RuntimeError(f"zene did not start: {(r.stderr or r.stdout)[-300:]}")
        time.sleep(8)                     # past the splash and the startup toasts
        self.info = json.loads(self.state.read_text())
        self.log_mark = self.log_size()

    def stop(self):
        if self.state.exists():
            self.gui("stop", timeout=30)

    def alive(self):
        try:
            os.kill(self.info["pid"], 0)
        except (ProcessLookupError, KeyError, AttributeError):
            return False
        return self.ping()

    def ping(self, timeout=5):
        try:
            s = socket.socket(socket.AF_UNIX)
            s.settimeout(timeout)
            s.connect(self.info["socket"])
            s.sendall(b'{"id":1,"cmd":"control.ping","args":{}}\n')
            data = s.recv(4096)
            s.close()
            return b'"ok"' in data
        except (OSError, KeyError):
            return False

    def log_size(self):
        try:
            return os.path.getsize(self.info["log"])
        except (OSError, KeyError):
            return 0

    def new_errors(self):
        try:
            with open(self.info["log"], errors="replace") as f:
                f.seek(self.log_mark)
                text = f.read()
        except (OSError, KeyError):
            return []
        self.log_mark = self.log_size()
        return [l.strip()[:300] for l in text.splitlines() if ERROR_LINE.search(l) and not NOISE.search(l)][:8]

    def inventory(self, out):
        r = subprocess.run(["/usr/bin/python3", str(HERE / "inventory.py"), str(out)], env=self.env,
                           capture_output=True, text=True, timeout=180)
        if r.returncode != 0:
            raise RuntimeError(f"inventory failed: {(r.stderr or r.stdout)[-300:]}")
        return [c for c in jsonl(out) if c["showing"] and c["w"] > 2 and c["h"] > 2]

    def click(self, c):
        self.gui("click", c["x"], c["y"])

    def shot(self, path):
        self.gui("shot", path)


def find(controls, role, name):
    return next((c for c in controls if c["role"] == role and c["name"].lower() == name.lower()), None)


def click_problem(c):
    """SHOWING in AT-SPI does not mean its centre is inside the virtual screen."""
    width, height = map(int, SIZE.split('x'))
    if not c.get('showing', False):
        return 'control is not showing'
    if not c.get('enabled', False):
        return 'control is disabled in this setup; a prerequisite scenario is needed'
    if c.get('w', 0) <= 2 or c.get('h', 0) <= 2:
        return 'control has no usable click area'
    if not (0 <= c['x'] < width and 0 <= c['y'] < height):
        return f"control centre ({c['x']}, {c['y']}) is outside the {SIZE} screen"
    return None


def target_identity(c):
    # Ignore only the application and document title, which change with build and dirty state.
    return (c['role'], c['name'], c.get('description', ''), tuple(c.get('path', '').split(' > ')[2:]))


def prepare(s, prep, tmp):
    """Apply a view's preparation clicks, re-locating each control in a fresh inventory."""
    for role, name in prep:
        c = find(s.inventory(tmp), role, name)
        if c is None or click_problem(c):
            return False
        s.click(c)
        time.sleep(0.8)
    return True


# ------------------------------------------------------------------------------------------------- stages
def stage_inventory(run, binary):
    rows, seen = [], set()
    with Session(run, binary) as s:
        s.start()
        base = s.inventory(run / "inv-main.jsonl")
        views = list(VIEWS)
        for c in base:
            if c["role"] in ("menu item", "menu") and c["y"] < 40 and c["name"]:
                views.append((f"menu-{c['name'].lower()}", [(c["role"], c["name"])]))
        for view, prep in views:
            if view != "main":
                s.start()
                if not prepare(s, prep, run / "inv-tmp.jsonl"):
                    log(run, f"view {view}: could not open it ({prep})")
                    rows.append({"view": view, "id": f"{view}|open", "role": "view", "name": view, "missing": True,
                                 "prep": prep})
                    continue
            for c in (base if view == "main" else s.inventory(run / f"inv-{view}.jsonl")):
                key = f"{c['role']}|{c['name']}|{c['description']}|{c['path']}"
                if key in seen:
                    continue             # tested once, in the first view it shows in
                seen.add(key)
                rows.append({**c, "view": view, "prep": prep, "id": f"{view}|{c['id']}"})
    write_jsonl(run / "controls.jsonl", rows)
    log(run, f"inventory: {len(rows)} controls in {len(views)} views")
    return rows


def expect_key(c):
    return f"{c['view']}|{c['role']}|{c['name']}|{c.get('description', '')}"


def stage_expect(run):
    cache = json.loads(EXPECT.read_text()) if EXPECT.exists() else {}
    todo = [c for c in jsonl(run / "controls.jsonl") if not c.get("missing")
            and cache.get(expect_key(c), {}).get('schema') != EXPECT_SCHEMA]
    for i in range(0, len(todo), 60):
        if not openai_wait(run):
            break
        batch = todo[i:i + 60]
        listing = "\n".join(json.dumps({"key": expect_key(c), "view": c["view"], "role": c["role"], "name": c["name"],
                                        "tooltip": c.get("description", ""), "where": c["path"][-120:]})
                            for c in batch)
        out = run / f"expect-{i // 60}.md"
        out.unlink(missing_ok=True)
        prompt = (
            "You are writing a GUI test checklist for Zene Studio (a DAW; Qt C++ source in this tree). For EACH control "
            "below, read the source as needed (grep its name/tooltip) and state what a user should SEE right after one "
            "left click on it, starting from a fresh project with the named view open. One plain sentence a screenshot "
            "can confirm (a panel opens, a toggle lights, a dialog titled X appears, the playhead moves, a track is "
            "added...). Set skip=true (with why) only for a control that quits or restarts the app, opens something "
            "outside the app (a web page, a file manager), starts a long job, or whose effect cannot be seen on screen. "
            "Read the current default state: a selected tab/tool can remain selected and a toggle may turn OFF. "
            "Do not demand a visible change for an already-selected idempotent action. An empty project can lack "
            "a selection, marker, effect or clip needed by the action; describe the actual refusal/setup requirement, "
            "not an impossible success. Pop-outs are separate windows and can overlap the main window on this "
            "virtual screen without a window manager; do not require new placement or decorations. "
            "Keep the intended feature contract: do not rewrite an expectation to endorse an implementation bug. "
            "Do not run commands that change anything. Reply with ONLY one JSON object per line: "
            '{"key": <key as given>, "expected": "...", "skip": false, "why": ""}\n\nCONTROLS:\n' + listing)
        prompt = prompt_context.stamp(prompt)
        r = subprocess.run([str(LUNA), "-C", str(TREE), "-o", str(out), "--read-only", prompt],
                           capture_output=True, text=True, timeout=3600)
        got = 0
        for line in (out.read_text() if out.exists() else "").splitlines():
            line = line.strip().strip("`")
            if line.startswith("{"):
                try:
                    row = json.loads(line)
                except ValueError:
                    continue
                if row.get("key") in {expect_key(c) for c in batch}:
                    cache[row["key"]] = {"expected": row.get("expected", ""), "skip": bool(row.get("skip")),
                                         "why": row.get("why", ""), "at": f"{datetime.date.today()}",
                                         "schema": EXPECT_SCHEMA}
                    got += 1
        log(run, f"expect: batch {i // 60}: {got}/{len(batch)} expectations (luna rc {r.returncode})")
        EXPECT.write_text(json.dumps(cache, indent=1) + "\n")
    return cache


def boxed_pair(before, after, c, out):
    from PIL import Image, ImageDraw
    a, b = Image.open(before).convert("RGB"), Image.open(after).convert("RGB")
    for im in (a, b):
        d = ImageDraw.Draw(im)
        x0, y0 = c["x"] - c["w"] // 2 - 3, c["y"] - c["h"] // 2 - 3
        d.rectangle([x0, y0, x0 + c["w"] + 6, y0 + c["h"] + 6], outline=(255, 0, 0), width=3)
    pair = Image.new("RGB", (a.width * 2 + 20, a.height), (255, 255, 255))
    pair.paste(a, (0, 0))
    pair.paste(b, (a.width + 20, 0))
    pair.save(out)  # keep small checked/LED states legible


def stage_drive(run, binary, limit=None):
    cache = json.loads(EXPECT.read_text()) if EXPECT.exists() else {}
    controls = [c for c in jsonl(run / "controls.jsonl")]
    shots = run / "shots"
    shots.mkdir(exist_ok=True)
    write_jsonl(run / 'verdicts.jsonl', [])  # verdicts from an older drive do not describe these screenshots
    write_jsonl(run / 'needs-retest.jsonl', [])
    results, done = [], 0
    with Session(run, binary) as s:
        for n, c in enumerate(controls):
            if limit and done >= limit:
                break
            common = {"id": c["id"], "view": c["view"], "name": c["name"]}
            if c.get("missing"):
                results.append({**common, "outcome": "missing",
                                "detail": f"the view could not be opened ({c['prep']})"})
                continue
            exp = cache.get(expect_key(c), {})
            if exp.get('schema') != EXPECT_SCHEMA or not (exp.get('expected') or exp.get('skip')):
                results.append({**common, "outcome": "not-exercised",
                                "detail": "no current expectation; rerun expect"})
                continue
            if exp.get("skip"):
                results.append({**common, "outcome": "skipped", "detail": exp.get("why", "")})
                continue
            # Escape does not close floated panels or restore layout, selection, toggles and dialogs.
            # Each expectation describes a fresh project: enforce that for EVERY control.
            s.start()
            if not prepare(s, c["prep"], run / "inv-tmp.jsonl"):
                results.append({**common, "outcome": "not-exercised",
                                "detail": "view preparation could not be performed; no target click attempted"})
                continue
            live = [x for x in s.inventory(run / "inv-tmp.jsonl") if target_identity(x) == target_identity(c)]
            base = shots / f"{n:04d}"
            if len(live) != 1:
                results.append({**common, "outcome": "not-exercised",
                                "detail": f"expected one exact target in its parent, found {len(live)}"})
                s.shot(f"{base}-setup.png")
                continue
            target = live[0]
            problem = click_problem(target)
            if problem:
                results.append({**common, "outcome": "not-exercised", "detail": problem,
                                "target": target, "expected": exp.get("expected", "")})
                s.shot(f"{base}-setup.png")
                continue
            s.shot(f"{base}-before.png")
            s.new_errors()
            try:
                s.click(target)
            except RuntimeError as error:
                if s.alive():
                    results.append({**common, 'outcome': 'not-exercised',
                                    'detail': f'input command failed: {error}'})
                    continue
                # A crash can tear down VNC during the input command. Keep the crash evidence below.
            time.sleep(1.0)
            alive = s.alive()
            if alive:
                s.shot(f"{base}-after.png")
            errors = s.new_errors()
            row = {**common, "role": c["role"], "tooltip": c["description"],
                   "x": target["x"], "y": target["y"], "expected": exp.get("expected", ""),
                   "target_before": target, "isolated": True,
                   "outcome": "clicked" if alive else "crash-or-hang", "errors": errors,
                   "before": f"{base}-before.png", "after": f"{base}-after.png" if alive else None}
            if alive:
                boxed_pair(row["before"], row["after"], target, f"{base}-pair.png")
                row["pair"] = f"{base}-pair.png"
            else:
                row["log_tail"] = Path(s.info["log"]).read_text(errors="replace")[-3000:] if s.info.get("log") else ""
            results.append(row)
            done += 1
            if done % 25 == 0:
                log(run, f"drive: {done} controls clicked")
                write_jsonl(run / "results.jsonl", results)
    write_jsonl(run / "results.jsonl", results)
    log(run, f"drive: {done} clicked, {sum(r['outcome'] == 'crash-or-hang' for r in results)} crash/hang, "
             f"{sum(r['outcome'] == 'missing' for r in results)} missing, {sum(r['outcome'] == 'skipped' for r in results)} skipped")
    return results


def stage_judge(run):
    results = jsonl(run / "results.jsonl")
    verdicts = {v["id"]: v for v in jsonl(run / "verdicts.jsonl")}
    for r in results:
        if r["outcome"] in ("crash-or-hang", "missing"):
            verdicts[r["id"]] = {"id": r["id"], "verdict": "FAIL", "saw": r["outcome"] + ": " + str(r.get("detail") or "")}
        elif r['outcome'] == 'not-exercised' or (r['outcome'] == 'clicked' and not r.get('expected')):
            verdicts[r['id']] = {'id': r['id'], 'verdict': 'UNSURE',
                                 'saw': 'NOT-TESTED: ' + r.get('detail', 'missing expectation')}
    todo = [r for r in results if r["outcome"] == "clicked" and r["id"] not in verdicts and r.get("expected")]
    for i in range(0, len(todo), JUDGE_BATCH):
        if not openai_wait(run):
            break
        batch = todo[i:i + JUDGE_BATCH]
        lines = "\n".join(f"IMAGE {k + 1}: id={r['id']!r}; control={r['role']} {r['name']!r} (tooltip {r['tooltip']!r}); "
                          f"expected after one click: {r['expected']}; initial accessible state: {r.get('target_before', {})}"
                          + (f"; Zene logged: {r['errors']}" if r["errors"] else "")
                          for k, r in enumerate(batch))
        prompt = ("Each attached image is Zene Studio BEFORE (left) and AFTER (right) one left click on the control "
                  "boxed in red. For each, decide whether the AFTER screen shows the expected outcome. PASS = clearly "
                  "yes; FAIL = clearly not (nothing happened, the wrong thing happened, an error appeared, the layout "
                  "broke); UNSURE = the screenshot cannot tell. A control already selected may correctly stay selected; "
                  "a toggle may switch off. Judge small state changes using the full-resolution image. Pop-out means "
                  "a separate window, not necessarily new placement or decorations (there is no window manager). "
                  "A log line is evidence to investigate, not alone proof of failure. Do not call unchanged pixels "
                  "a FAIL if the expectation is idempotent or the image cannot resolve the state. Reply with "
                  'ONLY one JSON object per line: {"id": ..., "verdict": "PASS|FAIL|UNSURE", "saw": "<what the AFTER '
                  'screen shows, one sentence>"}\n\n' + lines)
        out = run / f"judge-{i // JUDGE_BATCH}.md"
        out.unlink(missing_ok=True)
        prompt = prompt_context.stamp(prompt)
        cmd = ["codex", "exec", "-m", "gpt-6.1-terra", "-c", 'model_reasoning_effort="medium"', "-s", "read-only",
               "--skip-git-repo-check", "--ephemeral", "-o", str(out)]
        for r in batch:
            cmd += ["-i", r["pair"]]
        subprocess.run(cmd + ["--", prompt], capture_output=True, text=True, timeout=1800, stdin=subprocess.DEVNULL)
        ids = {r["id"] for r in batch}
        for line in (out.read_text() if out.exists() else "").splitlines():
            line = line.strip().strip("`")
            if line.startswith("{"):
                try:
                    v = json.loads(line)
                except ValueError:
                    continue
                if v.get("id") in ids:
                    verdicts[v["id"]] = v
        write_jsonl(run / "verdicts.jsonl", list(verdicts.values()))
    write_jsonl(run / "verdicts.jsonl", list(verdicts.values()))
    counts = {k: sum(v.get("verdict") == k for v in verdicts.values()) for k in ("PASS", "FAIL", "UNSURE")}
    log(run, f"judge: {counts}")
    return verdicts


def duplicate_of(title, file, line, revision):
    """Owner 2026-11-13: one defect surfaced through several controls ('Undo' and 'Undo History...' after a fresh load)
    became two findings whose fixes conflicted and deadlocked the train. Same title + file + line on the same revision
    is the same defect: queue it once and fold the other controls into it."""
    title, file = str(title or "").strip(), str(file or "").strip()
    if not title or not file or not line:
        return None
    for tier in ("easy", "needs_context", "hard"):
        for r in jsonl(QUEUE / f"queue/{tier}.jsonl"):
            if (r.get("revision") == revision and str(r.get("title", "")).strip() == title
                    and str(r.get("file", "")).strip() == file and r.get("line") == line):
                return r.get("cluster")
    return None


def stage_triage(run, train, how="one left click on the control, from a fresh project with its view open,",
                 verdicts=("FAIL",), force_real=False):
    """force_real: the expectation is the owner's spec (session.py), so a mismatch is queued even if Luna disagrees;
    Luna's doubt goes into the row for the fixer."""
    results = {r["id"]: r for r in jsonl(run / "results.jsonl")}
    fails = [{**results.get(v["id"], {"id": v["id"]}), "saw": v.get("saw", "")}
             for v in jsonl(run / "verdicts.jsonl") if v.get("verdict") in verdicts
             and results.get(v['id'], {}).get('outcome') != 'not-exercised']
    (QUEUE / "queue").mkdir(parents=True, exist_ok=True)
    revision = subprocess.run(["git", "-C", str(TREE), "rev-parse", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
    if not re.fullmatch(r'[0-9a-f]{40,64}', revision):
        raise ValueError('Triage requires a verified source revision')
    # A control can fail again after its accepted repair. Only the same revision
    # is already triaged; historical controls and duplicates cannot hide regressions.
    known = {r.get("control_id") for q in ("easy", "needs_context", "hard")
             for r in jsonl(QUEUE / f"queue/{q}.jsonl") if r.get('revision') == revision}
    known |= {r.get("control_id") for r in jsonl(QUEUE / "duplicates.jsonl") if r.get('revision') == revision}
    fails = [f for f in fails if f["id"] not in known]
    pending = {f['id']: f for f in fails}
    pending_path = run / 'triage-pending.jsonl'
    write_jsonl(pending_path, list(pending.values()))
    added, urgent = [], []
    for i in range(0, len(fails), TRIAGE_BATCH):
        if not openai_wait(run):
            raise RuntimeError(f'Terra triage allowance unavailable; findings retained in {pending_path}')
        batch = fails[i:i + TRIAGE_BATCH]
        evidence = "\n\n".join(json.dumps({k: f.get(k) for k in ("id", "view", "role", "name", "tooltip", "expected",
                                                                  "outcome", "saw", "errors")})
                               + (f"\nLOG TAIL:\n{f['log_tail'][-1500:]}" if f.get("log_tail") else "")
                               for f in batch)
        out = run / f"triage-{i // TRIAGE_BATCH}.md"
        out.unlink(missing_ok=True)
        prompt = (
            f"These Zene Studio controls failed an automated GUI check on train {train} (revision {revision[:10]}): {how} "
            "did not give the expected outcome. "
            "For EACH, investigate the source in this tree (read-only: do not edit or build) and find what is wrong. "
            "Decide: is it a real defect in Zene? The screenshot shows what actually happened: code that looks wired up is "
            "NOT evidence that it works. real=false ONLY if (a) the expected outcome was wrong - cite the source that "
            "shows the intended behaviour differs - or (b) the test provably did not perform the action (the control "
            "was off-screen, needed scrolling, or needs state a fresh project lacks); say which, with why. If "
            "the test provably did not perform the intended action, set test_valid=false and explain "
            "invalid_test_reason. Such cases need a corrected retest, even for an owner-specified expected outcome; "
            "do not invent a product defect. A product refusal of an actually performed valid action is still "
            "test_valid=true. Do not call a real display-requirement refusal a test failure merely because the "
            "visible app has a control socket. "
            "real: a one-line title, the file and line most responsible, the root cause, exact repro steps, difficulty "
            "(easy = a local fix in one place; needs_context = needs more investigation; hard = design-level), and "
            "urgent=true ONLY for a crash or hang, lost or corrupted work, or a core DAW action broken (play, stop, "
            "record, loop, seek, adding/editing notes or tracks, save, open, undo) - with why. Reply with ONLY one "
            'JSON object per line: {"id": ..., "real": true, "title": "...", "file": "src/...", "line": 123, '
            '"root_cause": "...", "repro": "...", "difficulty": "easy", "urgent": false, "urgent_why": "", '
            '"not_real_why": "", "test_valid": true, "invalid_test_reason": ""}\n\nFAILURES:\n' + evidence)
        prompt = prompt_context.stamp(prompt)
        result = subprocess.run([str(LUNA), "-C", str(TREE), "-o", str(out), "--read-only", prompt],
                                capture_output=True, text=True, timeout=3600)
        (run / f'triage-{i // TRIAGE_BATCH}.runner.log').write_text(
            (getattr(result, 'stdout', '') or '') + (getattr(result, 'stderr', '') or ''))
        if result.returncode:
            raise RuntimeError(f'Terra triage failed (rc {result.returncode}); findings retained in {pending_path}; '
                               f'see triage-{i // TRIAGE_BATCH}.runner.log')
        by_id = {f["id"]: f for f in batch}
        answered = set()
        for line in (out.read_text() if out.exists() else "").splitlines():
            line = line.strip().strip("`")
            if not line.startswith("{"):
                continue
            try:
                v = json.loads(line)
            except ValueError:
                continue
            f = by_id.get(v.get("id"))
            if f is None or f['id'] in answered:
                continue
            answered.add(f['id'])
            with open(QUEUE / "decisions.jsonl", "a") as dj:
                dj.write(json.dumps({"at": datetime.datetime.now().isoformat(timespec="seconds"), "train": train,
                                     "control_id": f["id"], **v}) + "\n")
            if v.get('test_valid') is False:
                with open(run / 'needs-retest.jsonl', 'a') as pending:
                    pending.write(json.dumps({'id': f['id'], 'train': train,
                                              'reason': v.get('invalid_test_reason') or v.get('not_real_why'),
                                              'decision': v}) + '\n')
                continue
            if not v.get("real") and not force_real:
                continue
            slug = re.sub(r"[^a-z0-9]+", "-", f"{f['view']}-{f.get('name', '')}".lower()).strip("-")[:40]
            cluster = f"gui-{datetime.date.today():%m%d}-{slug}-{revision[:12]}"
            row = {"pass": 0, "finding_id": cluster, "cluster": cluster, "control_id": f["id"],
                   "title": v.get("title", f"GUI: {f.get('name')} does not work"), "file": v.get("file", ""),
                   "line": v.get("line") or 0,
                   "text": (f"GUI SWEEP FAILURE ({train}): {v.get('title', '')}\n  control: {f.get('role')} "
                            f"{f.get('name')!r} in view {f.get('view')} (tooltip {f.get('tooltip')!r})\n  expected: "
                            f"{f.get('expected')}\n  observed: {f.get('saw')}\n  root cause (Luna): "
                            f"{v.get('root_cause', '')}"
                            + (f"\n  Luna doubted it ({v.get('not_real_why', '')}) but the owner's expected use says otherwise"
                               if not v.get("real") else "") + f"\n  where: {v.get('file')}:{v.get('line')}\n  repro: "
                            f"{v.get('repro', '')}\n  zene log: {f.get('errors')}\n  evidence: {run}"),
                   "lane": "gui", "worktree": str(TREE), "revision": revision, "original_revision": revision[:9],
                   "angle": "gui-sweep", "report_evidence": [str(run / 'results.jsonl'), f.get("pair") or ""],
                   "assignments": [], "prior_review_evidence": [], "realness": "confirmed_repro",
                   "resolved_path": v.get("file", ""), "luna_difficulty": v.get("difficulty", "needs_context"),
                   "model": "gui sweep: Terra judge + Luna triage", "difficulty": v.get("difficulty", "needs_context"),
                   "context_required": v.get("difficulty") != "easy", "prior_disposition": "confirmed_repro",
                   "members": [cluster], "urgent": bool(v.get("urgent")), "urgent_why": v.get("urgent_why", "")}
            twin = duplicate_of(row["title"], row["file"], row["line"], revision)
            if twin:
                with open(QUEUE / "duplicates.jsonl", "a") as d:
                    d.write(json.dumps({"at": datetime.datetime.now().isoformat(timespec="seconds"), "train": train,
                                        "revision": revision,
                                        "control_id": f["id"], "duplicate_of": twin, "title": row["title"],
                                        "file": row["file"], "line": row["line"]}) + "\n")
                log(run, f"triage: {f['id']} folded into {twin} (same title, file and line)")
                continue
            tier = v.get("difficulty") if v.get("difficulty") in ("easy", "needs_context", "hard") else "needs_context"
            with open(QUEUE / f"queue/{tier}.jsonl", "a") as q:
                q.write(json.dumps(row) + "\n")
            added.append(row)
            if row["urgent"]:
                urgent.append(row)
                with open(URGENT, "a") as u:
                    u.write(f"{cluster}\t{row['title'][:120]}\n")
        for identity in answered:
            pending.pop(identity, None)
        write_jsonl(pending_path, list(pending.values()))
        missing = set(by_id) - answered
        if missing:
            raise RuntimeError(f'Terra triage missing verdict for {sorted(missing)}; findings retained in {pending_path}')
    log(run, f"triage: {len(added)} bugs queued ({len(urgent)} urgent) from {len(fails)} failures")
    return added, urgent


def incomplete_checks(results, verdicts):
    return [r['id'] for r in results if r['outcome'] != 'skipped'
            and (r['outcome'] == 'not-exercised' or verdicts.get(r['id'], {}).get('verdict')
                 not in ('PASS', 'FAIL'))]


def run_all(train, binary, limit=None):
    run = HERE / "runs" / train.split("/")[-1]
    run.mkdir(parents=True, exist_ok=True)
    log(run, f"sweep of {train} with {binary}")
    try:
        stage_inventory(run, binary)
        stage_expect(run)
        results = stage_drive(run, binary, limit)
        verdicts = stage_judge(run)
        added, urgent = stage_triage(run, train)
    except Exception as e:
        log(run, f"sweep aborted: {e!r}")
        notify(f"GUI sweep of {train} aborted: {e!r}"[:300])
        return 1
    fails = sum(v.get("verdict") == "FAIL" for v in verdicts.values())
    passed = sum(v.get("verdict") == "PASS" for v in verdicts.values())
    incomplete = incomplete_checks(results, verdicts)
    (run / "SUMMARY.md").write_text(
        f"# GUI sweep {train} ({datetime.date.today()})\n\n- {len(results)} controls, {passed} pass, {fails} fail, "
        f"{sum(v.get('verdict') == 'UNSURE' for v in verdicts.values())} unsure, "
        f"{sum(r['outcome'] == 'skipped' for r in results)} skipped.\n- {len(added)} bugs queued "
        f"({len(urgent)} urgent): " + ", ".join(r["cluster"] for r in added) + "\n"
        + f"- Incomplete checks (not product passes): {len(incomplete)}: {', '.join(incomplete)}\n")
    notify(f"GUI sweep {train.split('/')[-1]}: {passed} buttons pass, {fails} fail -> {len(added)} bugs queued"
           + (f", {len(urgent)} URGENT (haiku-urgent lane): " + "; ".join(r['title'][:60] for r in urgent[:3])
              if urgent else "") + ".")
    # A coverage gap must not silently satisfy the next-train sweep gate.
    return 2 if incomplete or jsonl(run / 'needs-retest.jsonl') or limit else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["run", "inventory", "expect", "drive", "judge", "triage"])
    ap.add_argument("--train", default=(ROOT / "FIX-BASE").read_text().strip())
    ap.add_argument("--binary", default=str(BINARY))
    ap.add_argument("--limit", type=int, default=None, help="click at most N controls (trials)")
    a = ap.parse_args()
    if a.stage == "run":
        sys.path.insert(0, str(Path.home() / '.local/share/zene-bunny-fix/qwen'))
        import terra_priority
        sys.exit(terra_priority.sweep_run('buttons', a.train, run_all, a.binary, a.limit))
    run_dir = HERE / "runs" / a.train.split("/")[-1]
    run_dir.mkdir(parents=True, exist_ok=True)
    {"inventory": lambda: stage_inventory(run_dir, a.binary), "expect": lambda: stage_expect(run_dir),
     "drive": lambda: stage_drive(run_dir, a.binary, a.limit), "judge": lambda: stage_judge(run_dir),
     "triage": lambda: stage_triage(run_dir, a.train)}[a.stage]()
