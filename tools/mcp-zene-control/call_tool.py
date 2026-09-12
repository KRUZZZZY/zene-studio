#!/usr/bin/env python3
"""Call one bridge tool over real stdio MCP and print the result.

    python3 call_tool.py zene_mixer_get_state
    python3 call_tool.py zene_mixer_set_volume '{"channel":"ch-1","volume":0.5}'
    python3 call_tool.py zene_commands --socket /tmp/zene.sock

Uses the MCP client SDK against `server.py`, so it exercises exactly what an
agent session would see: initialize, tools/list (for the count), tools/call.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from mcp import ClientSession, StdioServerParameters  # noqa: E402
from mcp.client.stdio import stdio_client  # noqa: E402

PYTHON = os.environ.get("ZENE_CONTROL_PYTHON", sys.executable)


async def main_async(args: argparse.Namespace) -> int:
    argv = [str(HERE / "server.py"), "--socket", os.path.abspath(args.socket)]
    if args.proto is not None:
        argv += ["--proto", str(args.proto)]
    if args.call_timeout is not None:
        argv += ["--call-timeout", str(args.call_timeout)]
    params = StdioServerParameters(command=PYTHON, args=argv, env=dict(os.environ),
                                   cwd=str(HERE))
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            info = await session.initialize()
            listed = await session.list_tools()
            print(f"server={info.server_info.name} {info.server_info.version} "
                  f"tools={len(listed.tools)}")
            result = await session.call_tool(args.tool, json.loads(args.arguments),
                                             read_timeout_seconds=args.read_timeout)
            for block in (result.content or []):
                text = getattr(block, "text", None)
                if text is not None:
                    print(text)
                    break
            if result.structured_content:
                print(json.dumps(result.structured_content, indent=2, default=str))
            return 1 if result.is_error else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool")
    parser.add_argument("arguments", nargs="?", default="{}")
    parser.add_argument("--socket", default=os.environ.get("ZENE_CONTROL_SOCKET")
                        or os.path.join(os.getcwd(), "zene-control.sock"))
    parser.add_argument("--proto", type=int, default=None)
    parser.add_argument("--call-timeout", type=float, default=None)
    parser.add_argument("--read-timeout", type=float, default=300.0)
    return asyncio.run(main_async(parser.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
