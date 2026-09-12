#!/usr/bin/env python3
"""Entry point for the mcp-zene-control stdio MCP server.

    python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control/server.py

The socket path comes from `--socket`, else `ZENE_CONTROL_SOCKET`, else
`<workdir>/zene-control.sock` (workdir = `--workdir`, else `ZENE_CONTROL_WORKDIR`,
else the current directory). Nothing here talks to the DAW itself: the tool list
is generated from `control.commands_list` at request time.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from zene_control.server import main  # noqa: E402

if __name__ == "__main__":
    main()
