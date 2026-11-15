"""Model adapters for duet: one call runs one agent session (fresh, or a resumed lead) through the harness this
machine already trusts for that model, streams its events to a log, guards its context, and accounts its usage.

  claude    tools/haiku/haiku-exec (Landlock write-jail, usage-limit line, lean mode, --autocompact)
  opencode  qwen/opencode_free.run_model (OpenCode / DeepSeek Harness / MiniMax Code, its own cost caps and rests)
  codex     tools/luna/luna-exec (Luna in the same jail)

Every adapter returns a Result with per-request usage summed into a Usage. Claude requests are priced per request
from qwen/api-prices.json, including the context tiers (Haiku 5.5: 5x above 100k); other adapters report the cost
their harness reports.
"""
import json
import os
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path

HOME = Path.home()
PROG = Path(os.environ.get("ZENE_PROG", HOME / "Documents/AI_KOS_PROJECT/projects/zene-studio-program"))
HAIKU_EXEC = Path(os.environ.get("DUET_HAIKU_EXEC", PROG / "tools/haiku/haiku-exec"))
LUNA_EXEC = Path(os.environ.get("DUET_LUNA_EXEC", PROG / "tools/luna/luna-exec"))
QWEN = HOME / ".local/share/zene-bunny-fix/qwen"
sys.path.insert(0, str(QWEN))
import prompt_context
PRICES = QWEN / "api-prices.json"
LIMIT_LINE = re.compile(r"^haiku-exec: hit your usage limit.*$", re.M)
CODEX_TOTAL = re.compile(r"^tokens used\n([\d,]+)", re.M)

CURRENT = {"proc": None}        # the running child, so duet can stop it on SIGTERM


@dataclass
class Usage:
    requests: int = 0
    input: int = 0
    output: int = 0
    cache_read: int = 0
    cache_write: int = 0
    peak_ctx: int = 0
    over_cap: int = 0              # requests whose prompt passed the profile's context_cap
    over_100k: int = 0
    api_cost: float = 0.0          # priced per request from api-prices.json (claude) or as the harness reports
    reported_cost: float = 0.0     # what the harness itself reported (claude: total_cost_usd)

    def add(self, other):
        for k, v in asdict(other).items():
            setattr(self, k, max(self.peak_ctx, v) if k == "peak_ctx" else getattr(self, k) + v)


@dataclass
class Result:
    rc: int
    text: str
    session: str = None
    usage: Usage = field(default_factory=Usage)
    minutes: float = 0.0
    limit: str = None              # the harness's usage-limit line: the run must stop and report it
    transient: bool = False        # a provider hiccup worth one retry
    killed: str = None             # why duet stopped the session (context cap, request cap)
    log: str = ""


# ---------------------------------------------------------------- pricing

_prices = None


def price_request(model, inp, out, cread, cwrite, cwrite_1h=0):
    """API price of one Claude request; the tier is chosen by the request's whole prompt size. cwrite_1h (part of
    cwrite) is written to the 1-hour cache, billed at 2x input instead of the 5-minute write price (Claude Code uses
    it; checked 2026-11-13 against its own total_cost_usd to the cent)."""
    global _prices
    if _prices is None:
        try:
            _prices = json.loads(PRICES.read_text()).get("models", {})
        except (OSError, ValueError):
            _prices = {}
    p = _prices.get(f"anthropic/{model}")
    if not p:
        return 0.0
    ctx = inp + cread + cwrite
    for tier in p.get("tiers") or []:
        if ctx > (tier.get("tier") or {}).get("size", 1 << 62):
            p = tier
    return (inp * p.get("input", 0) + out * p.get("output", 0) + cread * p.get("cache_read", 0)
            + (cwrite - cwrite_1h) * p.get("cache_write", 0) + cwrite_1h * 2 * p.get("input", 0)) / 1e6


# ---------------------------------------------------------------- process plumbing

def _stop(proc):
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        return
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def _stream(cmd, cwd, env, prompt, log_path, on_line):
    """Run cmd with prompt on stdin, copy every line to log_path, call on_line(line) -> reason or None (stop)."""
    killed = None
    with open(log_path, "w") as log:
        proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, start_new_session=True)
        CURRENT["proc"] = proc
        try:
            proc.stdin.write(prompt)
            proc.stdin.close()
        except BrokenPipeError:
            pass
        for line in proc.stdout:
            log.write(line)
            log.flush()
            if killed is None:
                killed = on_line(line)
                if killed:
                    log.write(f"\nduet: stopping this session: {killed}\n")
                    _stop(proc)
        rc = proc.wait()
        CURRENT["proc"] = None
    return rc, killed


# ---------------------------------------------------------------- claude (haiku-exec)

def claude_usage(seen, res, cap=None):
    """Usage of one Claude session from its assistant events (seen: id -> (usage, model, is_subagent)) and result event."""
    # The input side of each request is exact in its assistant events; their output_tokens are streaming snapshots
    # (measured 2026-11-13: 19 shown vs 1662 billed). The result event's usage has this call's true output (its
    # modelUsage and total_cost_usd are cumulative over a resumed session): spread the true output over the requests
    # in proportion to the snapshots, so each is priced at its own context tier.
    models = {m for _, m, _ in seen.values()}
    if len(models) == 1 and (res.get("usage") or {}).get("output_tokens") is not None:
        true_out = {next(iter(models)): res["usage"]["output_tokens"]}
    else:                                   # several models (a fallback): per-model totals, cumulative if resumed
        true_out = {m: (v or {}).get("outputTokens") for m, v in (res.get("modelUsage") or {}).items()}
    snap = {}
    for u, model, _sub in seen.values():
        snap[model] = snap.get(model, 0) + u.get("output_tokens", 0) + 1
    usage = Usage()
    for u, model, _sub in seen.values():
        o = u.get("output_tokens", 0)
        target = true_out.get(model) if model in true_out else (next(iter(true_out.values())) if len(true_out) == 1 else None)
        if target is not None:
            o = target * (o + 1) / snap[model]
        i, cr, cw = u.get("input_tokens", 0), u.get("cache_read_input_tokens", 0), u.get("cache_creation_input_tokens", 0)
        ctx = i + cr + cw
        usage.requests += 1
        usage.input += i; usage.output += o; usage.cache_read += cr; usage.cache_write += cw
        usage.peak_ctx = max(usage.peak_ctx, ctx)
        usage.over_cap += bool(cap and ctx > cap)
        usage.over_100k += ctx > 100_000
        usage.api_cost += price_request(model, i, o, cr, cw, (u.get("cache_creation") or {}).get("ephemeral_1h_input_tokens", 0))
    usage.output = round(usage.output)
    return usage



def run_claude(profile, prompt, cwd, out, log, *, write, resume=None, persist=False, cap=None, max_requests=None,
               tools=None, rules="", extra_dirs=(), ledger=None, env_extra=None):
    prompt = prompt_context.stamp(prompt)
    out = Path(out)
    session_file = out.parent / "claude-session.json"
    env = dict(os.environ)
    for k in ("HAIKU_RESUME", "HAIKU_SESSION_OUT", "HAIKU_PERSIST", "HAIKU_AUTOCOMPACT", "HAIKU_TOOLS",
              "HAIKU_APPEND_RULES", "HAIKU_EXTRA_DIRS"):
        env.pop(k, None)                       # the outer lane's settings must not leak into a sub-session
    env.update(HAIKU_MODEL=profile["model"], HAIKU_EFFORT=profile.get("effort", "high"),
               HAIKU_FALLBACK=profile.get("fallback", ""), HAIKU_LEAN="1" if profile.get("lean", True) else "0",
               HAIKU_SESSION_OUT=str(session_file), HAIKU_APPEND_RULES=rules,
               HAIKU_EXTRA_DIRS=":".join(str(d) for d in extra_dirs))
    if tools:
        env["HAIKU_TOOLS"] = tools
    env["HAIKU_AUTOCOMPACT"] = str(profile["autocompact"]) if profile.get("autocompact") else "off"
    if persist or resume:
        env["HAIKU_PERSIST"] = "1"
    if resume:
        env["HAIKU_RESUME"] = resume
    if ledger:
        env["HAIKU_LEDGER"] = str(ledger)
    env.update(env_extra or {})
    cmd = [sys.executable, str(HAIKU_EXEC), "-C", str(cwd), "-o", str(out),
           "-s", "danger-full-access" if write else "read-only", "-"]
    seen, texts, state = {}, [], {"result": None}

    def on_line(line):
        if LIMIT_LINE.match(line):
            state["limit"] = line.strip()
        try:
            ev = json.loads(line)
        except ValueError:
            return None
        if not isinstance(ev, dict):
            return None
        if ev.get("type") == "result":
            state["result"] = ev
        if ev.get("type") != "assistant":
            return None
        msg = ev.get("message") or {}
        for c in msg.get("content") or []:
            if isinstance(c, dict) and c.get("type") == "text" and not ev.get("parent_tool_use_id"):
                texts.append(c.get("text", ""))
        mid, u = msg.get("id"), msg.get("usage") or {}
        if not mid or not u:
            return None
        new = mid not in seen
        seen[mid] = (u, msg.get("model") or profile["model"], ev.get("parent_tool_use_id") is not None)
        if not new or ev.get("parent_tool_use_id"):
            return None
        ctx = u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0) + u.get("cache_creation_input_tokens", 0)
        top = sum(1 for _, _, sub in seen.values() if not sub)
        if cap and ctx > cap:
            return f"context {ctx} > cap {cap}"
        if max_requests and top > max_requests:
            return f"request cap {max_requests} reached"
        return None

    t0 = time.time()
    rc, killed = _stream(cmd, cwd, env, prompt, log, on_line)
    res = state["result"] or {}
    usage = claude_usage(seen, res, cap)
    usage.reported_cost = res.get("total_cost_usd") or 0.0
    text = out.read_text() if out.exists() else (res.get("result") or "\n".join(texts[-3:]))
    session = res.get("session_id")
    if not session and session_file.exists():
        try:
            session = json.loads(session_file.read_text()).get("session")
        except ValueError:
            pass
    tail = Path(log).read_text(errors="replace")[-4000:] if Path(log).exists() else ""
    limit = state.get("limit") or (LIMIT_LINE.search(tail).group(0) if LIMIT_LINE.search(tail) else None)
    transient = rc not in (0, 2) and not killed and bool(re.search(r"overloaded|529|ECONNRESET|timed? ?out", tail, re.I))
    return Result(rc=0 if killed and rc != 2 else rc, text=text, session=session, usage=usage,
                  minutes=round((time.time() - t0) / 60, 1), limit=limit, transient=transient, killed=killed, log=str(log))


# ---------------------------------------------------------------- opencode (OpenCode, DeepSeek Harness, MiniMax Code)

_OPENCODE = r"""
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import opencode_free as o
model, cwd, out, session = sys.argv[2:6]
try:
    # Duet calls run_model directly, bypassing the ordinary dispatcher's cooldown checks.
    # Never let the shared picker's fallback select a resting or exhausted provider.
    if o.uses_dsh(model):
        awake = o.dsh_providers_awake()
        pinned = o.load(o.DSH_PROVIDERS, {}).get(session) if session else None
        if not awake or (pinned and pinned not in awake):
            raise RuntimeError("Provider usage limit: DeepSeek routes are resting or the resumed provider is unavailable")
    rc, transient = o.run_model(model, Path(cwd), sys.stdin.read(), Path(out), None, session or None)
except RuntimeError as error:
    print(f"duet-opencode: {error}", flush=True)
    sys.exit(103)
sys.exit(rc + (100 if transient else 0))
"""


def run_opencode(profile, prompt, cwd, out, log, *, write, resume=None, persist=False, cap=None, max_requests=None,
                 tools=None, rules="", extra_dirs=(), ledger=None, env_extra=None):
    prompt = prompt_context.stamp(prompt)
    out = Path(out)
    model = profile["model"]
    if not write:
        prompt = ("READ-ONLY ROLE: do not edit, create or delete files and do not commit; read files and run read-only "
                  "commands only.\n\n") + prompt
    if rules:
        prompt = rules + "\n\n" + prompt
    env = dict(os.environ, **(env_extra or {}))
    cmd = [sys.executable, "-c", _OPENCODE, str(QWEN), model, str(cwd), str(out), resume or ""]
    usage, state = Usage(), {"steps": 0}

    def on_line(line):
        try:
            ev = json.loads(line)
        except ValueError:
            return None
        if not isinstance(ev, dict):
            return None
        if ev.get("type") == "step_finish":                            # OpenCode
            part = ev.get("part") or {}
            t = part.get("tokens") or {}
            i, o = t.get("input") or 0, (t.get("output") or 0) + (t.get("reasoning") or 0)
            cr, cw = (t.get("cache") or {}).get("read") or 0, (t.get("cache") or {}).get("write") or 0
            cost = part.get("cost") or 0
        elif ev.get("phase") == "step_end" or ev.get("type") == "turn.completed":   # DeepSeek Harness / MiniMax Code
            u = ev.get("usage") or {}
            cr = u.get("cacheReadTokens") or 0
            i, o, cw = max(0, (u.get("inputTokens") or 0) - cr), u.get("outputTokens") or 0, 0
            cost = 0
        else:
            return None
        ctx = i + cr + cw
        usage.requests += 1
        usage.input += i; usage.output += o; usage.cache_read += cr; usage.cache_write += cw
        usage.peak_ctx = max(usage.peak_ctx, ctx)
        usage.over_cap += bool(cap and ctx > cap)
        usage.over_100k += ctx > 100_000
        usage.api_cost += cost
        if cap and ctx > cap:
            return f"context {ctx} > cap {cap}"
        if max_requests and usage.requests > max_requests:
            return f"request cap {max_requests} reached"
        return None

    t0 = time.time()
    rc, killed = _stream(cmd, cwd, env, prompt, log, on_line)
    for f in out.parent.glob("opencode-*-usage.json"):        # the harness's own cost figures (DeepSeek Harness)
        try:
            costs = json.loads(f.read_text()).get("step_costs") or []
            usage.reported_cost += sum(c or 0 for c in costs)
        except ValueError:
            pass
    if not usage.api_cost:
        usage.api_cost = usage.reported_cost
    session = None
    if (out.parent / "opencode-session.json").exists():
        try:
            session = json.loads((out.parent / "opencode-session.json").read_text()).get("session")
        except ValueError:
            pass
    text = out.read_text() if out.exists() else ""
    # Provider refusals often leave an empty final reply. Surface the actual quota error
    # before the orchestrator retries it as malformed JSON or spends a lead review on it.
    limit = None
    if rc and not killed:
        detail = Path(log).read_text(errors="replace") if Path(log).exists() else ""
        for f in out.parent.glob("opencode-*.stderr"):
            detail += "\n" + f.read_text(errors="replace")
        if re.search(r"insufficient_balance|coding_plan_exhausted|GoUsageLimitError|FreeUsageLimitError|"
                     r"Provider usage limit:|OpenCode Go usage limit reached", detail, re.I):
            limit = f"haiku-exec: hit your usage limit; {model} provider quota or cooldown; see {log}"
    transient = rc >= 100 and not killed
    return Result(rc=2 if limit else (0 if killed else (rc - 100 if rc >= 100 else rc)),
                  text=text, session=session, usage=usage, limit=limit,
                  minutes=round((time.time() - t0) / 60, 1), transient=transient and not limit,
                  killed=killed, log=str(log))


# ---------------------------------------------------------------- codex (Luna)

def run_codex(profile, prompt, cwd, out, log, *, write, resume=None, persist=False, cap=None, max_requests=None,
              tools=None, rules="", extra_dirs=(), ledger=None, env_extra=None):
    prompt = prompt_context.stamp(prompt)
    out = Path(out)
    if rules:
        prompt = rules + "\n\n" + prompt
    env = dict(os.environ, LUNA_MODEL=profile["model"], **(env_extra or {}))
    cmd = [sys.executable, str(LUNA_EXEC), "-C", str(cwd), "-o", str(out), "--effort", profile.get("effort", "max")]
    cmd += ([] if write else ["--read-only"]) + ["-"]
    t0 = time.time()
    rc, killed = _stream(cmd, cwd, env, prompt, log, lambda line: None)
    text_log = Path(log).read_text(errors="replace")
    usage = Usage(requests=1, input=sum(int(x.replace(",", "")) for x in CODEX_TOTAL.findall(text_log)))
    limit = None
    if re.search(r"usage limit|hit your limit|rate limit", text_log[-3000:], re.I) and rc != 0:
        limit = "haiku-exec: hit your usage limit (Luna)"
    return Result(rc=rc, text=out.read_text() if out.exists() else "", usage=usage,
                  minutes=round((time.time() - t0) / 60, 1), limit=limit, killed=killed, log=str(log))


ADAPTERS = {"claude": run_claude, "opencode": run_opencode, "codex": run_codex}
RESUMABLE = {"claude", "opencode"}


def run(profile, prompt, cwd, out, log, **kw):
    return ADAPTERS[profile["adapter"]](profile, prompt, cwd, out, log, **kw)
