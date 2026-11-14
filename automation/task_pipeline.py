"""Read-only Luna preparation -> cached local Clef pairwise ranking -> queues.

One coordinator owns preparation/rank state. Missing evidence stays explicit;
no fabricated rankings, skipped QA requirements or prescribed product fixes.
"""
import datetime
import contextlib
import fcntl
import hashlib
import json
import prompt_context
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import feature_ladder as F
import queue_categories

ROOT = Path(__file__).resolve().parent
PROG = Path.home() / "Documents/AI_KOS_PROJECT/projects/zene-studio-program"
TREE = PROG / "zene-task-prep"
QA = ["Prove every acceptance criterion through its user-visible GUI/agent path.",
      "Build the affected targets and run tests from build/tests; zero tests is an error.",
      "For changed behavior, deterministic regression fails without the fix and passes with it.",
      "Exercise ordinary, boundary, invalid-input and failure paths; document supported portability paths.",
      "Preserve save/load, undo, compatibility and real-time contracts; no audio-thread allocation or locking.",
      "Run unchanged quality gates, register source files and retain exact commands, logs and exit codes.",
      "Independent QA verifies the evidence; never weaken assertions, tolerances, tests or gates."]
PROMPT = """Prepare ONE Zene Studio task. Read-only: do not edit, run builds or tests, commit, or implement.
Read the task, relevant project rules/contracts, the named code and nearby tests in this checkout.
Return ONLY JSON with: synopsis (concise user-visible problem/outcome), evidence (observed facts),
likely_cause (hypothesis, or null for a feature; distinguish uncertainty from verified evidence),
code_areas ([{path, line_start, line_end, reason}]), qa_requirements ([strings]),
constraints ([strings]), verdict (READY, ALREADY_DONE, NOT_A_BUG, NEEDS_CLARIFICATION or SPLIT),
and questions ([strings]). Code areas must be verified existing tracked paths with real line numbers.
For a feature describe required behavior and likely relevant code, not a likely bug.
Include all acceptance criteria and the supplied QA requirements. Identify likely defects but do NOT
prescribe a fix, patch, algorithm, design or implementation approach. The implementer owns the solution.
Use an independently testable scope. If evidence is insufficient, say so; never invent file pointers.
Implementation design, algorithms, naming and test-framework choices belong to the implementer and are NOT
reasons to block preparation. Use NEEDS_CLARIFICATION only for an actual contradictory/missing user-visible
requirement; otherwise use READY and state non-blocking uncertainty. For a split stage scope only that stage.
This is bounded task preparation, not exhaustive research. Use at most EIGHT tool calls and at most FOUR
code areas. Prefer the runner-verified source excerpts below; inspect only the missing relevant functions/tests.
Stop searching and emit the JSON before the tool budget runs out. If uncertainty remains, state it in evidence
and questions. Do not attempt to solve the feature. All acceptance/mandatory QA is preserved by the runner.
Use read-only file inspection commands only. Keep the JSON under 10,000 characters.
"""


SCHEMA = {"type":"object", "additionalProperties":False,
    "required":["synopsis","evidence","likely_cause","code_areas","qa_requirements","constraints","verdict","questions"],
    "properties":{
      "synopsis":{"type":"string"},"evidence":{"type":"string"},"likely_cause":{"type":["string","null"]},
      "code_areas":{"type":"array","maxItems":4,"items":{"type":"object","additionalProperties":False,
        "required":["path","line_start","line_end","reason"],"properties":{"path":{"type":"string"},
        "line_start":{"type":"integer","minimum":1},"line_end":{"type":"integer","minimum":1},"reason":{"type":"string"}}}},
      "qa_requirements":{"type":"array","items":{"type":"string"}},"constraints":{"type":"array","items":{"type":"string"}},
      "verdict":{"type":"string","enum":["READY","ALREADY_DONE","NOT_A_BUG","NEEDS_CLARIFICATION","SPLIT"]},
      "questions":{"type":"array","items":{"type":"string"}}}}


def seed_context(unit, base):
    identity = str(unit.get("queue_id") or unit["cluster"])
    brief = PROG / 'research/reviews/feature-briefs' / (identity.split('.')[0] + '.md')
    try:
        old = brief.read_text()
    except OSError:
        old = ''
    pointers = re.findall(r'((?:src|include|plugins|tests)/[A-Za-z0-9_./+-]+\.(?:cpp|h|hpp|py)):(\d+)', old)
    if unit.get('file') and unit.get('line'):
        pointers.insert(0,(unit['file'],str(unit['line'])))
    out, seen = [], set()
    for path,line in pointers:
        if path in seen or len(out) >= 4:
            continue
        seen.add(path)
        try:
            lines = git_show(base,path).splitlines()
            first=max(1,int(line)-8);last=min(len(lines),first+45)
            if first <= last:
                out.append({'path':path,'line_start':first,'line_end':last,
                    'source':'\n'.join(f'{n}: {lines[n-1]}' for n in range(first,last+1))})
        except (subprocess.CalledProcessError,ValueError):
            continue
    return out


def validate_packet(unit, result, base, git_show):
    if not isinstance(result, dict) or not result.get("synopsis") or not result.get("qa_requirements") or len(result["qa_requirements"]) < len(unit.get("acceptance") or []):
        raise ValueError("Preparation lacks synopsis or QA requirements")
    if result.get("verdict") not in {"READY", "ALREADY_DONE", "NOT_A_BUG", "NEEDS_CLARIFICATION", "SPLIT"}:
        raise ValueError("Preparation has no valid scope verdict")
    if any(k in result for k in ("fix", "suggested_fix", "approach", "patch", "solution")):
        raise ValueError("Preparation must not prescribe the fix")
    areas = result.get("code_areas") or []
    if not areas:
        raise ValueError("Preparation has no verified code pointers")
    for area in areas:
        path = str(area["path"])
        if Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError("Code pointers must stay in the checkout")
        source = git_show(base, path)
        first, last = area["line_start"], area["line_end"]
        if isinstance(first, bool) or not isinstance(first, int) or not isinstance(last, int) or not 1 <= first <= last <= len(source.splitlines()):
            raise ValueError(f"Invalid source line range: {path}")
        area["source_sha256"] = hashlib.sha256(source.encode()).hexdigest()
    identity = str(unit.get("queue_id") or unit["cluster"])
    packet = {**result, "id": identity, "base": base, "spec_hash": F.spec_hash(unit),
              "acceptance": unit.get("acceptance") or [], "mandatory_qa": QA,
              "prepared_by": "gpt-6-luna/max", "prepared_at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    if queue_categories.difficulty(unit) is not None:
        packet.update(owner_difficulty=queue_categories.difficulty(unit), category=unit.get('category'),
                      review_process=unit.get('review_process'), interaction_contract=unit.get('interaction_contract'),
                      terra_scenarios=unit.get('terra_scenarios'))
    packet["packet_hash"] = hashlib.sha256(json.dumps(packet, sort_keys=True).encode()).hexdigest()
    return packet


def git_show(base, path):
    return subprocess.check_output(["git", "-C", str(TREE), "show", f"{base}:{path}"], text=True, errors="replace")


def checkout(base):
    if not TREE.exists():
        subprocess.run(["git", "-C", str(PROG / "zene-studio"), "worktree", "add", "--detach", str(TREE), base], check=True)
    if subprocess.check_output(["git", "-C", str(TREE), "status", "--porcelain"], text=True).strip():
        raise RuntimeError("Preparation checkout is dirty; preserving it")
    subprocess.run(["git", "-C", str(TREE), "checkout", "--quiet", "--detach", base], check=True)


def preparation_command(output):
    # Existing Luna runner supplies the enforced read-only Landlock scope.
    return [str(PROG / "tools/luna/luna-exec"), "-C", str(TREE),
            "-o", str(output), "--effort", "max", "--read-only", "-"]


def prepare(unit):
    import qwenfix as Q
    import autoscale
    import reset_reserve
    if ((ROOT / "LUNA-HOLD").exists() or Q.paused_until_in(ROOT / "LUNA-LIMIT-UNTIL")
            or reset_reserve.paused("codex", None)):
        F.write(ROOT / "TASK-PREPARATION-STATUS.json", {"at": time.time(), "status": "waiting-for-luna-allowance", "model": "gpt-6-luna"})
        return False
    if autoscale.ram_starts(1.5, autoscale.knobs()) < 1:
        return False
    base = (ROOT / "FIX-BASE").read_text().strip()
    checkout(base)
    identity = str(unit.get("queue_id") or unit["cluster"])
    logs = ROOT / "task-preparation" / identity
    logs.mkdir(parents=True, exist_ok=True)
    task = {k: unit.get(k) for k in ("queue_id", "cluster", "kind", "title", "goal", "acceptance", "depends_on", "text", "file", "line", "area", "member_ids", "category", "owner_difficulty", "review_process", "interaction_contract", "terra_scenarios")}
    policy = f"\nCurrent owner policy (2026-11-10): this checkout {TREE} is a read-only copy of promoted {base}. Claude features may proceed through the ladder while the Haiku/DeepSeek/free pool repairs Terra; all Terra promotion gates remain. Do not block on older task paths or the superseded feature pause.\n"
    prompt = prompt_context.stamp(PROMPT + policy + "\nRequired JSON schema:\n" + json.dumps(SCHEMA) + "\nTask:\n" + json.dumps(task) + "\nMandatory QA:\n" + json.dumps(QA) + "\nRunner-verified source excerpts:\n" + json.dumps(seed_context(unit,base)))
    F.write(ROOT / "TASK-PREPARATION-STATUS.json", {"at": time.time(), "status": "preparing", "task": identity, "model": "gpt-6-luna"})
    env = dict(os.environ, LUNA_MODEL="gpt-6-luna")
    # Do not resume a Claude session when switching provider. Each bounded task
    # writes a fresh final artifact, separate from runner progress output.
    output = logs / "luna-result.json"
    output.unlink(missing_ok=True)
    result = subprocess.run(preparation_command(output), input=prompt, cwd=TREE, env=env,
                            capture_output=True, text=True, timeout=900)
    (logs / f"attempt-{time.time_ns()}.log").write_text(result.stdout + result.stderr)
    (logs / "latest.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        if Q.note_luna_limit(logs / "latest.log", ROOT / "LUNA-LIMIT-UNTIL"):
            return False
        raise RuntimeError(f"Luna preparation failed; see {logs / 'latest.log'}")
    text = output.read_text().strip()
    text = re.sub(r"^```(?:json)?\s*|\s*```$", "", text)
    packet = validate_packet(unit, json.loads(text), base, git_show)
    out = ROOT / "task-packets"
    out.mkdir(exist_ok=True)
    F.write(out / f"{identity}.json", packet)
    F.write(ROOT / "TASK-PREPARATION-STATUS.json", {"at": time.time(), "status": "prepared", "task": identity, "model": "gpt-6-luna"})
    return True


def comparison_text(packet):
    # All acceptance and QA criteria survive; only narrative verbosity is reduced.
    return {"synopsis": packet["synopsis"], "evidence": packet.get("evidence"),
            "likely_cause": packet.get("likely_cause"), "code_areas": packet["code_areas"],
            "acceptance": packet["acceptance"], "qa_requirements": packet["qa_requirements"],
            "mandatory_qa": packet["mandatory_qa"], "constraints": packet.get("constraints", [])}


def insert_rank(order, identity, packets, compare):
    lo, hi = 0, len(order)
    while lo < hi:
        mid = (lo + hi) // 2
        if compare(packets[identity], packets[order[mid]]) > 0.5:
            lo = mid + 1
        else:
            hi = mid
    order.insert(lo, identity)


def rank_packets():
    import autoscale
    if autoscale.gaming():
        F.write(ROOT / "TASK-RANKING-STATUS.json", {"at":time.time(),"status":"waiting-for-gpu"})
        return False  # preserve the owner's interactive GPU headroom
    sys.path.insert(0, str(PROG / "tools/luna-patrol"))
    import clef_client as C
    import urllib.request
    metadata = json.load(urllib.request.urlopen("http://127.0.0.1:11434/api/tags", timeout=5))
    digest = next((m["digest"] for m in metadata.get("models", []) if m["name"].split(":")[0] == C.MODEL), None)
    if not digest:
        raise RuntimeError("Clef model unavailable; tasks remain unranked")
    old = F.read(ROOT / "TASK-DIFFICULTY.json")
    cache = C.Cache(ROOT / "task-comparisons.jsonl")
    packets = {p.stem: F.read(p) for p in (ROOT / "task-packets").glob("*.json")}
    packets = {k: v for k, v in packets.items() if v.get('owner_difficulty') is None and
               v.get("verdict") in ("READY", "ALREADY_DONE", "NOT_A_BUG", "NEEDS_CLARIFICATION", "SPLIT")}
    minimum = int(F.config().get("minimum_rank_cohort", 8))
    if len(packets) < minimum:
        F.write(ROOT / "TASK-RANKING-STATUS.json", {"at":time.time(),"status":"waiting-for-comparison-cohort","prepared":len(packets),"minimum":minimum})
        return False
    order = [k for k in old.get("order", []) if k in packets and old.get("digest") == digest
             and (old.get("tasks", {}).get(k) or {}).get("packet_hash") == packets[k]["packet_hash"]]

    def one(a, b):
        key = digest + ":" + a["packet_hash"] + ":" + b["packet_hash"]
        value = cache.get(key)
        if value is None:
            answer = C.post({"A": comparison_text(a), "B": comparison_text(b)}, {"harder": {"type": "choice",
                "instructions": "Which task is harder to implement correctly, including ALL acceptance and QA requirements? Judge reasoning, concurrency, DSP, compatibility and cross-subsystem complexity, not urgency or prose length. Neither packet prescribes a solution.",
                "criteria": {"A": "A is harder", "B": "B is harder"}}})
            value = float(answer["harder"]["probabilities"]["A"])
            if not 0 <= value <= 1:
                raise ValueError("Invalid Clef comparison")
            cache.put(key, value)
        return value

    def compare(a, b):
        return (one(a, b) + 1 - one(b, a)) / 2  # cancel presentation bias

    pending = [identity for identity in packets if identity not in order]
    if pending and len(packets) > 1:
        from triage_new import GpuLease
        lease = GpuLease(ROOT)
    else:
        lease = contextlib.nullcontext()
    progress = {"status": "ranking", "total": len(packets), "remaining": len(pending)}
    F.write(ROOT / "TASK-RANKING-STATUS.json", {"at": time.time(), **progress})
    with lease:
        for index, identity in enumerate(pending):
            insert_rank(order, identity, packets, compare)
            F.write(ROOT / "TASK-RANKING-STATUS.json", {"at": time.time(), **progress,
                    "remaining": len(pending) - index - 1, "last_task": identity})
    tasks = {identity: {"rank": n, "pct": n / max(len(order) - 1, 1), "packet_hash": packets[identity]["packet_hash"]}
             for n, identity in enumerate(order)}
    F.write(ROOT / "TASK-DIFFICULTY.json", {"at": time.strftime("%Y-%m-%dT%H:%M:%S"), "digest": digest,
            "method": "clef-pairwise-enriched-v1", "order": order, "tasks": tasks})
    return True


def cycle():
    import features_queue as fq
    F.rebuild(fq)
    inventory = [json.loads(line) for line in (ROOT / "FEATURE-INVENTORY.jsonl").read_text().splitlines()]
    legacy = {row["queue_id"]: row.get("rank", 999999) for row in (json.loads(line) for line in
              (PROG / "research/reviews/clef-difficulty-2026-10-13/delegation.jsonl").read_text().splitlines() if line.strip())}
    pending = sorted((r for r in inventory if r["queue_status"] == "awaiting-preparation"),
                     key=lambda r: (*queue_categories.order(r)[:3], legacy.get(r.get("split_of", r["cluster"]), 999999)))
    import qwenfix as Q
    bugs = []
    for item in Q.load_queue():
        identity = str(item['cluster'])
        state = F.read(ROOT / 'state' / f'{identity}.json')
        ledger = F.read(ROOT / 'issues' / f'{identity}.json')
        status = state.get('outcome') or ledger.get('status') or ('prepared' if F.packet_for(item) else 'awaiting-preparation')
        bugs.append({**item, 'kind': 'bug', 'queue_status': status})
    path = ROOT / 'BUG-INVENTORY.jsonl'
    temp = path.with_suffix('.tmp')
    temp.write_text(''.join(json.dumps(row) + '\n' for row in bugs));temp.replace(path)
    # Urgent CI/reproduction packets can precede ordinary feature scoping; others
    # use idle preparation capacity after the ready feature inventory is populated.
    urgent = [r for r in bugs if r.get('urgent') and r['queue_status'] == 'awaiting-preparation']
    pending = urgent + pending
    if not pending:
        pending = [r for r in bugs if r['queue_status'] == 'awaiting-preparation']
    failures = F.read(ROOT / "TASK-PREPARATION-FAILURES.json")
    target = next((r for r in pending if
        (failures.get(r["cluster"], {}).get("spec_hash") != F.spec_hash(r) or
         (int(failures.get(r["cluster"], {}).get("attempts", 0)) < 2 and time.time() - failures.get(r["cluster"], {}).get("at", 0) > 1800))), None)
    if target:
        try:
            prepare(target)
        except Exception as exc:
            previous = failures.get(target["cluster"], {})
            attempts = int(previous.get("attempts", 0)) + 1 if previous.get("spec_hash") == F.spec_hash(target) else 1
            failures[target["cluster"]] = {"at": time.time(), "attempts": attempts, "spec_hash": F.spec_hash(target), "error": type(exc).__name__ + ": " + str(exc)[:300]}
            F.write(ROOT / "TASK-PREPARATION-FAILURES.json", failures)
    try:
        ranked = rank_packets()
    except Exception as exc:
        F.write(ROOT / "TASK-RANKING-STATUS.json", {"at": time.time(), "status": "waiting", "reason": str(exc)[:300]})
    else:
        if ranked:
            F.write(ROOT / "TASK-RANKING-STATUS.json", {"at": time.time(), "status": "ranked"})
    F.rebuild(fq)


def main():
    with (ROOT / "TASK-PIPELINE.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        while not (ROOT / "STOP-TASK-PIPELINE").exists():
            try:
                cycle()
            except Exception as exc:
                F.write(ROOT / "TASK-PREPARATION-STATUS.json", {"at": time.time(), "status": "blocked", "error": str(exc)[:300]})
                raise
            if "--once" in sys.argv:
                break
            time.sleep(30)


if __name__ == "__main__":
    main()
