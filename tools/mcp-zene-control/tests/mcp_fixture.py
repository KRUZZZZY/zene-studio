#!/usr/bin/env python3
"""Module fixture and MCP helpers shared by the bridge's two end-to-end suites.

`tests/test_mcp_e2e.py` drives the *live instance's own contract* (the generated
tool list, the full flow, resources, the DAW's typed errors, the protocol check,
undo). `tests/test_mcp_errors.py` drives the *bounded typed failures* (no
instance, stale socket, killed mid-call, wedged stub, the readiness gate). Both
need the same one real headless instance, the same state directory — the live
command-list cache the offline assertions read back lives there — and the same
verbatim transcript, so that fixture lives here instead of being copied into
either module.

`ensure()` starts the instance on first use and is **idempotent**, so whichever
suite unittest reaches first starts it and the other reuses it (a second
instance would also overwrite `STATE_DIR`, which is what 05 reads). Cleanup —
quit, then write and print the transcript — is registered with `atexit`, once,
so no module's teardown can pull the instance out from under a later module
mid-run. That is why the transcript dump prints after the test summary rather
than before it.

This module is deliberately not named `test_*`: it is a helper, not a suite, so
unittest discovery must not collect it.
"""
from __future__ import annotations

import asyncio
import atexit
import json
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Coroutine

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
sys.path.insert(0, str(BRIDGE_DIR))

try:  # package import (python3 -m unittest tests.test_mcp_e2e)
    from . import harness as H
except ImportError:  # direct run (python3 tests/test_mcp_e2e.py)
    import harness as H  # type: ignore[no-redef]

from mcp import ClientSession, StdioServerParameters  # noqa: E402
from mcp.client.stdio import stdio_client  # noqa: E402

import zene_control.config as C  # noqa: E402
import zene_control.tools as T  # noqa: E402

#: The `phase` recorded for the one test that walks the whole flow.
FULL_FLOW_PHASE = "full_flow"

INSTANCE: H.Instance | None = None
STATE_DIR: str | None = None
INFO: dict[str, Any] = {}
TRANSCRIPT: list[dict] = []
FAILURES: list[str] = []

_started = False


def ensure() -> None:
    """Start the shared real instance on first call; later calls are no-ops."""
    global INSTANCE, STATE_DIR, _started
    if _started:
        return
    _started = True
    H.binary_path()  # fail loudly (never silently skip) when there is no binary
    STATE_DIR = tempfile.mkdtemp(prefix="zene-control-state-", dir="/tmp")
    instance = H.Instance()
    try:
        INFO["socket_s"] = round(instance.wait_socket(), 3)
        INFO["engine_ready_s"] = round(instance.wait_engine(), 3)
        INFO["binary"] = instance.binary
        INFO["socket_path"] = instance.socket_path
        config = C.Config(socket_path=instance.socket_path, workdir=str(H.BRIDGE_DIR),
                          state_dir=STATE_DIR)
        source = T.Bridge(config).fetch_live()
        INFO["daw_commands"] = source.count
        INFO["command_list_source"] = source.source
    except Exception:
        instance.cleanup()
        raise
    INSTANCE = instance
    atexit.register(finish)


def finish() -> None:
    """Quit the shared instance and write/print the transcript. Runs at exit, once."""
    if INSTANCE is not None:
        INFO["instance_exit"] = INSTANCE.quit()
        INSTANCE.cleanup()
    if STATE_DIR:
        out = os.path.join(STATE_DIR, "mcp-transcript.json")
        try:
            with open(out, "w", encoding="utf-8") as handle:
                json.dump({"info": INFO, "transcript": TRANSCRIPT}, handle, indent=2, default=str)
            INFO["transcript_path"] = out
        except OSError:
            pass
    print("\n---- module fixture ----")
    print(json.dumps(INFO, indent=2, default=str))
    print("\n---- MCP transcript (every request/response) ----")
    for entry in TRANSCRIPT:
        print(json.dumps(entry, indent=2, default=str))
    no = [t for t in TRANSCRIPT if t.get("kind") == "tools_list" and t.get("generated_count") == 0]
    if no:
        FAILURES.append("a tools/list returned zero generated tools")
    print(f"\nMCP E2E: {len(TRANSCRIPT)} recorded exchanges; fixture socket "
          f"{INFO.get('socket_s')}s, engine ready {INFO.get('engine_ready_s')}s, "
          f"{INFO.get('daw_commands')} DAW commands")


def live_instance() -> H.Instance:
    """The shared instance, with the not-None assertion the tests used to inline."""
    assert INSTANCE is not None
    return INSTANCE


def bridge_env() -> dict[str, str]:
    env = dict(os.environ)
    if STATE_DIR:
        env["ZENE_CONTROL_STATE"] = STATE_DIR
    return env


def record(**entry: Any) -> None:
    entry["step"] = len(TRANSCRIPT) + 1
    TRANSCRIPT.append(entry)


def run_bridge(socket_path: str, options: dict, body: Callable[..., Coroutine]):
    """Open a stdio MCP session to the bridge, initialize, run `body`."""
    params = StdioServerParameters(
        command=H.PYTHON,
        args=H.bridge_argv(socket_path, **options),
        env=bridge_env(),
        cwd=str(H.BRIDGE_DIR),
    )

    async def _main():
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                init = await session.initialize()
                return await body(session, init)

    return asyncio.run(_main())


async def call(session: ClientSession, tool: str, arguments: dict | None = None,
               phase: str = "call", read_timeout: float | None = None):
    """tools/call with the exchange recorded verbatim."""
    started = time.monotonic()
    result = await session.call_tool(tool, arguments or {},
                                     read_timeout_seconds=read_timeout)
    elapsed = time.monotonic() - started
    structured = getattr(result, "structured_content", None) or {}
    first = ""
    for block in (getattr(result, "content", None) or []):
        if getattr(block, "type", None) == "text":
            first = block.text
            break
    record(kind="tools_call", phase=phase, tool=tool, arguments=arguments or {},
           is_error=bool(getattr(result, "is_error", False)), elapsed_s=round(elapsed, 3),
           summary=first, structured=structured)
    return result, structured, elapsed


def error_kind(structured: dict) -> str | None:
    return ((structured or {}).get("error") or {}).get("kind")


def daw_tool_name(command_id: str) -> str:
    return "zene_" + command_id.replace(".", "_")
