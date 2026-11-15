"""Owner's four-rung Claude feature policy; task identity/budgets survive routing.

Prepared packets and pairwise ranks are immutable evidence. Missing preparation,
dependencies and historical failures remain visible in the inventory, not lost.
"""
import hashlib
import fcntl
import shutil
import json
from pathlib import Path
import re
import subprocess
import time
import queue_categories

ROOT = Path(__file__).resolve().parent
class StalePacket(RuntimeError):
    pass


class PacketEvidenceError(ValueError):
    """Pinned evidence needs repair; retain its queued job and stop redispatch."""


LEGACY = {"free": "haiku-max", "muse": "haiku-max", "sonnet": "sonnet-high", "opus": "opus-medium",
          "fable-low": "opus-xhigh", "fable-medium": "opus-xhigh", "fable-high": "opus-xhigh"}


def read(path, default=None):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {} if default is None else default


def write(path, data):
    path = Path(path)
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(json.dumps(data, indent=1) + "\n")
    temp.replace(path)


def config():
    return read(ROOT / "FEATURE-LADDER.json")


def enabled():
    return config().get("enabled") is True


def tiers():
    rows = config()["tiers"]
    if tuple(r["tier"] for r in rows) != ("haiku-max", "sonnet-high", "opus-medium", "opus-xhigh"):
        raise ValueError("Feature ladder must contain Haiku max, Sonnet high, Opus medium and Opus xhigh, in order")
    cuts = [r["below"] for r in rows]
    if cuts != sorted(cuts) or len(set(cuts)) != 4 or cuts[-1] <= 1:
        raise ValueError("Invalid ladder percentile boundaries")
    return rows


def names():
    return tuple(r["tier"] for r in tiers())


def canonical(tier):
    return LEGACY.get(tier, tier)


def start(pct):
    if isinstance(pct, bool) or not isinstance(pct, (int, float)) or not 0 <= pct <= 1:
        raise ValueError("Missing/invalid pairwise difficulty percentile")
    return next(r["tier"] for r in tiers() if pct < r["below"])


def next_tier(tier):
    # A non-Claude failure enters the first Claude rung, never skips Haiku.
    if tier in ("free", "muse"):
        return names()[0]
    ordered = names()
    index = ordered.index(canonical(tier))
    return ordered[index + 1] if index + 1 < len(ordered) else None


def select(lane, job):
    """Copy lane selection for this task, preserving older sessions on correction."""
    if not enabled() or not lane.get("difficulty_ladder") or job["item"].get("kind") != "feature":
        return lane
    item = job["item"]
    tier = canonical(item.get("feature_tier"))
    row = next(r for r in tiers() if r["tier"] == tier)
    resume = job.get("resume") or {}
    model, effort = row["model"], row["effort"]
    if resume.get("engine") == "claude" and resume.get("session") and resume.get("model") and "fable" not in resume["model"].lower():
        # Existing personal/work sessions are grandfathered, not restarted as new work.
        model = resume["model"]
        effort = resume.get("effort") or next((r["effort"] for r in tiers() if r["model"] == model), lane.get("effort", "high"))
    return {**lane, "model": model, "effort": effort}


def spec_hash(unit):
    fields = {k: unit.get(k) for k in ("title", "goal", "acceptance", "depends_on", "text")}
    fields["id"] = unit.get("queue_id") or unit.get("cluster")
    for key in ('category', 'owner_difficulty', 'review_process', 'interaction_contract', 'terra_scenarios'):
        if key in unit:
            fields[key] = unit[key]
    return hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()


def packet_for(unit):
    identity = str(unit.get("queue_id") or unit.get("cluster"))
    packet = read(ROOT / "task-packets" / f"{identity}.json")
    if packet.get("spec_hash") != spec_hash(unit):
        return {}
    base = (ROOT / "FIX-BASE").read_text().strip() if (ROOT / "FIX-BASE").exists() else packet.get("base")
    if base != packet.get("base"):
        tree = Path.home() / "Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-studio"
        for area in packet.get("code_areas") or []:
            result = subprocess.run(["git", "-C", str(tree), "show", f"{base}:{area['path']}"], capture_output=True)
            if result.returncode or hashlib.sha256(result.stdout.decode(errors="replace").encode()).hexdigest() != area.get("source_sha256"):
                return {}
    return packet


def status_of(unit, on_base, accepted, claimed, by_title, packet, ranks, states):
    identity = unit["queue_id"]
    if identity in on_base:
        return "merged"
    if identity in accepted:
        return "accepted-awaiting-train"
    if identity in claimed:
        return "claimed"
    state = states.get(identity) or {}
    if state and str(state.get("outcome", "")).startswith(("parked", "no-result")):
        return "frontier-exhausted-or-routing-review"
    if state and not str(state.get("outcome", "")).startswith(("parked", "no-result")):
        return str(state.get("outcome") or "in-progress")
    deps = [by_title.get(d, d) for d in unit.get("depends_on") or []]
    if any(d not in on_base for d in deps):
        return "waiting-for-dependency"
    ledger = read(ROOT / "issues" / f"{identity}.json")
    if str(ledger.get("status", "")).startswith(("blocked", "budget-exhausted")):
        return ledger["status"]
    if not packet:
        failure = read(ROOT / 'TASK-PREPARATION-FAILURES.json').get(identity, {})
        if failure.get('spec_hash') == spec_hash(unit) and failure.get('attempts', 0) >= 2:
            return 'preparation-failed-preserved'
        return "awaiting-preparation"
    if queue_categories.difficulty(unit) is not None:
        return 'ready'
    rank = ranks.get(identity) or {}
    if rank.get("packet_hash") != packet.get("packet_hash"):
        return "awaiting-pairwise-ranking"
    return "ready"


def _rebuild(fq):
    """Rebuild without deleting states, branches, accepted records or failed tasks."""
    advance_states()
    units = {r["queue_id"]: r for r in (json.loads(line) for line in (fq.PROG / "plans/queue-050.jsonl").read_text().splitlines() if line.strip())}
    base = (ROOT / "FIX-BASE").read_text().strip()
    log = subprocess.run(["git", "-C", str(fq.LINE), "log", "--format=%s %b", base], capture_output=True, text=True, errors="replace", check=True).stdout
    on_base = set(re.findall(r"Q050-\d+(?:\.\d+)?", log))
    accepted = set(line.split("\t")[0] for line in (ROOT / "FEATURES-ACCEPTED.tsv").read_text().splitlines()) if (ROOT / "FEATURES-ACCEPTED.tsv").exists() else set()
    claimed = set((ROOT / "LANE-CLAIMED").read_text().split()) if (ROOT / "LANE-CLAIMED").exists() else set()
    states = {p.stem: read(p) for p in (ROOT / "state").glob("Q050-*.json")}
    splits = read(ROOT / "SPLIT-UNITS.json")
    parents = dict(units)
    for identity, parent in parents.items():
        stages = fq.split_stages(identity)
        if not stages:
            continue
        ids = []
        for n, stage in enumerate(stages, 1):
            sid = f"{identity}.{n}"
            units[sid] = {**parent, "queue_id": sid, "title": f"{parent['title']} - stage {n}/{len(stages)}: {stage['title']}",
                          "goal": stage.get("goal") or parent.get("goal"), "acceptance": stage["acceptance"],
                          "depends_on": [f"{identity}.{n-1}"] if n > 1 else parent.get("depends_on", []),
                          "split_of": identity, "stage": n, "stages": len(stages)}
            ids.append(sid)
        splits[identity] = ids
    write(ROOT / "SPLIT-UNITS.json", splits)
    by_title = {r["title"]: identity for identity, r in units.items()}
    ranks = read(ROOT / "TASK-DIFFICULTY.json").get("tasks", {})
    escalations = read(ROOT / "ESCALATIONS.json")
    inventory, queue = [], []
    for identity, unit in units.items():
        packet = packet_for(unit)
        state = status_of(unit, on_base, accepted, claimed, by_title, packet, ranks, states)
        if identity in splits and identity not in on_base and identity not in accepted:
            state = "split-into-stages"
        row = queue_categories.annotate({**unit, "cluster": identity, "kind": "feature", "worktree": str(fq.LINE), "queue_status": state})
        rank = ranks.get(identity) or {}
        if packet:
            row.update(task_packet=str(ROOT / "task-packets" / f"{identity}.json"), packet_hash=packet["packet_hash"], preparation_verdict=packet.get("verdict"))
        if rank:
            row.update(pct=rank["pct"], rank=rank["rank"], rank_method="clef-pairwise-enriched-v1")
        owner_pct = queue_categories.difficulty_pct(unit)
        if owner_pct is not None:
            row.update(pct=owner_pct, rank_method='owner-assigned', rank=unit.get('category_sequence', 0))
        if state == "ready":
            step = escalations.get(identity) or {}
            tier = canonical(step["tier"]) if step.get("tier") else start(row["pct"])
            row.update(queue_id=identity, tier=tier, feature_tier=tier, clef_tier=f"feature:{tier}",
                       difficulty=tier, round_base=int(step.get("round_base") or 0),
                       escalation_notes=step.get("notes"), escalated=bool(step), feature_ladder=True)
            if tier not in names():
                row["queue_status"] = "invalid-escalation-tier"
            else:
                queue.append(row)
        inventory.append(row)
    queue.sort(key=queue_categories.order)
    inventory.sort(key=queue_categories.order)
    # Atomic independent queue files; no OpenCode feature work after activation.
    for name, rows in (("FEATURE-INVENTORY.jsonl", inventory), ("CLAUDE-QUEUE.jsonl", queue), ("FEATURE-QUEUE.jsonl", [])):
        path = ROOT / name
        temp = path.with_name(name + ".tmp")
        temp.write_text("".join(json.dumps(row) + "\n" for row in rows))
        temp.replace(path)
    counts = {}
    for row in inventory:
        counts[row["queue_status"]] = counts.get(row["queue_status"], 0) + 1
    category_counts = []
    for category in sorted(queue_categories.categories().values(), key=lambda c: c['order']):
        members = [r for r in inventory if r.get('category') == category['id']]
        category_counts.append({'id': category['id'], 'title': category['title'], 'order': category['order'],
                                'difficulty': category.get('difficulty'), 'total': len(members),
                                'ready': sum(r['queue_status'] == 'ready' for r in members),
                                'review_process': category.get('review_process')})
    write(ROOT / "FEATURE-LADDER-STATUS.json", {"at": time.strftime("%Y-%m-%dT%H:%M:%S"), "base": base,
          "tiers": tiers(), "inventory": len(inventory), "ready": len(queue), "counts": counts,
          "thresholds": config().get("thresholds"), 'categories': category_counts})
    print(f"Feature ladder: {len(queue)} ready; inventory {len(inventory)}; {counts}")


def advance_states():
    """Escalate a verified failed attempt without resetting its canonical budget."""
    esc = read(ROOT / 'ESCALATIONS.json')
    for path in (ROOT / 'state').glob('Q050-*.json'):
        rec = read(path)
        if not str(rec.get('outcome', '')).startswith(('parked', 'no-result')):
            continue
        identity = path.stem
        item = rec.get('item') or {}
        current = item.get('feature_tier') or (esc.get(identity) or {}).get('tier')
        if not current:
            current = str(rec.get('tier', '')).removeprefix('feature:')
        if current not in names() and current not in LEGACY:
            continue
        nxt = next_tier(current)
        if nxt is None:
            continue  # frontier exhaustion remains visible, not auto-donated
        qa = rec.get('qa') or {}
        prior = esc.get(identity) or {}
        esc[identity] = {'tier': nxt, 'round_base': int(rec.get('round') or 0),
            'notes': 'Preserve the existing branch and correct the independently reviewed failure. ' +
                str(qa.get('summary') or qa.get('rework_instructions') or rec.get('outcome')),
            'history': prior.get('history', []) + [{'from': current, 'to': nxt, 'at': time.strftime('%Y-%m-%dT%H:%M:%S'),
                'outcome': rec.get('outcome'), 'model': rec.get('model'), 'branch': rec.get('branch')}]}
        archive = ROOT / 'parked/_ladder-escalated'
        archive.mkdir(parents=True, exist_ok=True)
        for old in [path, ROOT / 'parked' / path.name]:
            if old.exists():
                shutil.move(str(old), str(archive / f'{old.parent.name}-{old.stem}-{time.time_ns()}.json'))
    write(ROOT / 'ESCALATIONS.json', esc)


def rebuild(fq):
    with (ROOT / 'FEATURE-QUEUE.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _rebuild(fq)


def freeze_packet(item, folder):
    for companion in item.get('batch') or []:
        child = Path(folder) / ('packet-' + str(companion['cluster']))
        child.mkdir(exist_ok=True)
        freeze_packet(companion, child)
    if item.get('feature_ladder') and item.get('packet_hash'):
        source = Path(item.get('task_packet', '/nonexistent'))
        packet = read(source)
        if packet.get('packet_hash') != item['packet_hash']:
            # Only the mutable preparation queue can be refreshed by reranking.
            # A paused rework pins historical evidence, even after its run moves.
            if source.parent == ROOT / 'task-packets':
                raise StalePacket('Prepared packet changed after dispatch; retry with current ranking')
            packet = recover_packet(source, item['packet_hash'])
    else:
        packet = packet_for(item)
    if packet:
        verify_packet(packet, item.get('packet_hash'))
        target = Path(folder) / 'task-packet.json'
        evidence = ROOT / 'task-packet-evidence'
        evidence.mkdir(exist_ok=True)
        stable = evidence / (packet['packet_hash'] + '.json')
        with (evidence / '.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if stable.exists():
                stored = read(stable)
                if stored.get('packet_hash') != packet['packet_hash']:
                    raise PacketEvidenceError(f'Immutable task packet evidence is damaged: {stable}')
                verify_packet(stored, packet['packet_hash'])
            else:
                write(stable, packet)
        write(target, packet)
        item.update(task_packet=str(stable), packet_hash=packet['packet_hash'])
    return packet


def recover_packet(source, expected):
    """Recover legacy run references by exact hash; never substitute newer preparation."""
    if not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise PacketEvidenceError('Invalid pinned task packet hash')
    candidates = [ROOT / 'task-packet-evidence' / (expected + '.json')]
    try:
        relative = source.relative_to(ROOT / 'runs')
    except ValueError:
        relative = None
    if relative and len(relative.parts) > 1 and not relative.parts[0].startswith('_'):
        for kind in ('_usage-limit', '_free-limit', '_infrastructure'):
            candidates.extend((ROOT / 'runs' / kind).glob(
                relative.parts[0] + '-*/' + str(Path(*relative.parts[1:]))))
    for path in candidates:
        packet = read(path)
        if packet.get('packet_hash') == expected:
            try:
                return verify_packet(packet, expected)
            except ValueError as exc:
                raise PacketEvidenceError(str(exc)) from exc
    raise PacketEvidenceError(f'Pinned task packet {expected} is unavailable at {source} or its archives; preserve job for repair')


def verify_packet(packet, expected=None):
    if not packet:
        return packet
    actual = hashlib.sha256(json.dumps({k:v for k,v in packet.items() if k != 'packet_hash'}, sort_keys=True).encode()).hexdigest()
    if actual != packet.get('packet_hash') or (expected and actual != expected):
        raise ValueError('Task packet content/hash mismatch; preparation evidence must remain intact')
    return packet
