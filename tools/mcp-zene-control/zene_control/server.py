"""The stdio MCP server: `tools/list` is generated, `tools/call` is typed.

Handlers run on a worker thread (`asyncio.to_thread`), so a slow command — a
`render.render` that takes minutes — cannot wedge the stdio server: other
requests keep being served, and the call itself is bounded by its budget.
"""
from __future__ import annotations

import asyncio
import json
import math
import traceback
from typing import Any

import mcp.types as types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server

from . import registry as R
from .config import (SERVER_NAME, SERVER_VERSION, Config, ErrorKind,
                     ZeneControlError, parse_args)
from .tools import (BRIDGE_TOOL_SPECS, RESOURCE_INSTANCE_STATUS,
                    RESOURCE_PROJECT_STATE, Bridge)


def _json_safe(value: Any) -> Any:
    """Recursively replace non-finite floats (inf/nan) with None."""
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if isinstance(value, dict):
        return {str(k): _json_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_safe(v) for v in value]
    return value


class BridgeServer:
    """Owns the configuration and turns bridge calls into MCP results."""

    def __init__(self, config: Config):
        self.config = config
        self.bridge = Bridge(config)

    # -- tool list --------------------------------------------------------

    def tool_payloads(self) -> list[dict]:
        """Bridge-owned tools plus one generated tool per DAW command."""
        tools: list[dict] = [
            {
                "name": name,
                "description": description,
                "inputSchema": schema,
                "annotations": {"read_only_hint": name != "zene_status",
                                "destructive_hint": False,
                                "idempotent_hint": True,
                                "open_world_hint": False,
                                "title": name},
                "meta": {"zene/bridge_tool": True},
            }
            for name, description, schema in BRIDGE_TOOL_SPECS
        ]
        source = self.bridge.resolve()
        for spec in source.specs:
            tools.append(R.mcp_tool(spec, self.config.timeout_for(spec.id)))
        return tools

    @staticmethod
    def _to_tool(payload: dict) -> types.Tool:
        annotations = payload.get("annotations")
        return types.Tool(
            name=payload["name"],
            description=payload.get("description"),
            input_schema=payload["inputSchema"],
            annotations=types.ToolAnnotations(**annotations) if annotations else None,
            _meta=payload.get("meta"),
        )

    # -- results ----------------------------------------------------------

    @staticmethod
    def _ok(name: str, payload: dict) -> types.CallToolResult:
        safe = _json_safe(payload)
        summary = str(safe.get("summary") or f"{name}: ok={safe.get('ok')}")
        body = json.dumps(safe, indent=2, ensure_ascii=False, default=str)
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=summary),
                     types.TextContent(type="text", text=body)],
            structured_content=json.loads(json.dumps(safe, default=str)),
            is_error=not bool(safe.get("ok", False)),
        )

    @classmethod
    def _error(cls, name: str, error: ZeneControlError) -> types.CallToolResult:
        payload = error.to_payload()
        payload["tool"] = name
        return cls._ok(name, payload)

    # -- dispatch ---------------------------------------------------------

    def call(self, name: str, arguments: dict | None) -> dict:
        arguments = dict(arguments or {})
        if name == "zene_commands":
            return self.bridge.commands_payload(arguments)
        if name == "zene_status":
            return self.bridge.status_payload(arguments)
        source = self.bridge.resolve()
        spec = self.bridge.find_spec_by_tool(name, source)
        return self.bridge.call_command(spec, arguments)

    # -- resources --------------------------------------------------------

    @staticmethod
    def resource_payloads() -> list[dict]:
        return [
            {
                "uri": RESOURCE_PROJECT_STATE,
                "name": "project-state",
                "description": "The running project's state (project.get_state): file, "
                               "modified, tempo and track count.",
                "mimeType": "application/json",
            },
            {
                "uri": RESOURCE_INSTANCE_STATUS,
                "name": "instance-status",
                "description": "Bridge/instance diagnostics: socket path, reachability, "
                               "engine readiness, budgets and the typed error when offline.",
                "mimeType": "application/json",
            },
        ]

    def read_resource(self, uri: str) -> tuple[str, str]:
        """Return (mime_type, text) for one resource URI."""
        if uri == RESOURCE_PROJECT_STATE:
            payload = self.bridge.project_state()
        elif uri == RESOURCE_INSTANCE_STATUS:
            payload = self.bridge.status_payload({})
        else:
            known = ", ".join(r["uri"] for r in self.resource_payloads())
            raise ValueError(f"unknown resource {uri!r}; the bridge serves: {known}")
        return "application/json", json.dumps(_json_safe(payload), indent=2, ensure_ascii=False)

    # -- MCP wiring -------------------------------------------------------

    def build_server(self) -> Server:
        async def _list_tools(_ctx: Any, _params: Any) -> types.ListToolsResult:
            try:
                payloads = await asyncio.to_thread(self.tool_payloads)
            except Exception as exc:  # a listing failure must not be silent
                raise RuntimeError(f"could not build the tool list: {type(exc).__name__}: {exc}") from exc
            return types.ListToolsResult(tools=[self._to_tool(p) for p in payloads])

        async def _call_tool(_ctx: Any, params: types.CallToolRequestParams) -> types.CallToolResult:
            name = params.name
            try:
                payload = await asyncio.to_thread(self.call, name, dict(params.arguments or {}))
            except ZeneControlError as exc:
                return self._error(name, exc)
            except Exception as exc:  # surfaced verbatim, never swallowed
                return self._error(name, ZeneControlError(
                    ErrorKind.BRIDGE_ERROR,
                    f"unhandled {type(exc).__name__}: {exc}",
                    detail={"traceback": traceback.format_exc()[-2000:]},
                ))
            return self._ok(name, payload)

        async def _list_resources(_ctx: Any, _params: Any) -> types.ListResourcesResult:
            return types.ListResourcesResult(
                resources=[types.Resource(**r) for r in self.resource_payloads()]
            )

        async def _read_resource(_ctx: Any,
                                 params: types.ReadResourceRequestParams) -> types.ReadResourceResult:
            try:
                mime, text = await asyncio.to_thread(self.read_resource, params.uri)
            except ZeneControlError as exc:
                raise ValueError(f"{exc.kind}: {exc.message}") from exc
            return types.ReadResourceResult(
                contents=[types.TextResourceContents(uri=params.uri, mime_type=mime, text=text)]
            )

        return Server(
            SERVER_NAME,
            version=SERVER_VERSION,
            instructions=(
                "Bridge to a running Zene Studio instance's control socket. Every "
                "`zene_<group>_<verb>` tool is generated from the instance's "
                "control.commands_list, so it cannot drift from the DAW. Use "
                "`zene_commands` to see the registry (works with no instance "
                "connected) and `zene_status` to check reachability and engine "
                "readiness. Engine commands are only sent after control.ping "
                "reports engine_ready."
            ),
            on_list_tools=_list_tools,
            on_call_tool=_call_tool,
            on_list_resources=_list_resources,
            on_read_resource=_read_resource,
        )


async def _serve(config: Config) -> None:
    server = BridgeServer(config).build_server()
    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream, server.create_initialization_options())


def main(argv: list[str] | None = None) -> None:
    asyncio.run(_serve(parse_args(argv)))


if __name__ == "__main__":
    main()
