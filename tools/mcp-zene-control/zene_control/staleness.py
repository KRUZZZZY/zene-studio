"""Is the offline command list stale? — the comparison, in one place.

The bridge serves a live instance's `control.commands_list` when one answers and
an offline copy (a cache written by the last live fetch, else the committed
snapshot) when none does. A copy's provenance — `captured_at`, the version it came
from — says *when* it was taken and cannot say whether it is *still right*: the
defect this module exists for is a 70-id 0.1.0-alpha copy sitting in a state
directory while the tree behind it registered 144 ids, with nothing comparing the
two.

So a bundle also records the SURFACE it describes (`registry.surface_fingerprint`:
id count, group count, id hash) and this module measures a copy against a live
list, naming the ids that differ. It is a mixin rather than free functions because
it needs the bridge's paths (`cache_path`, `snapshot_path`), which are the two
places an offline copy can live — and `Bridge` is where they belong.

WHY A MIXIN AND NOT MORE OF `tools.py`: the file-length ratchet (Gate 7, 500
lines) counts `tools.py` as one unit, and this concern is separable from serving
and dispatching commands — a fresh `stale_socket` cannot make the bridge misjudge
a surface, and a wrong surface judgement cannot break a call. Splitting on that
line keeps both halves readable; the alternative, growing a file the ratchet
already has near its limit, is what turns a documented cap into a debate.
"""
from __future__ import annotations

from .config import COMMANDS_LIST_TIMEOUT, SNAPSHOT_FIX, Config, ErrorKind, ZeneControlError
from .protocol import ControlClient
from . import registry as R


class StalenessMixin:
    """`probe_live` (read-only) and `offline_drift` (the comparison)."""

    #: The Config the mixin's host was built with (see `tools.Bridge`).
    config: Config

    # -- implemented by the Bridge this is mixed into ---------------------

    def cache_path(self) -> str:  # pragma: no cover - abstract
        raise NotImplementedError

    def snapshot_path(self) -> str:  # pragma: no cover - abstract
        raise NotImplementedError

    # -- the live side ----------------------------------------------------

    def offline_copies(self) -> list[tuple[str, str, dict]]:
        """The readable offline copies, freshest first (see rank_offline_bundles)."""
        return R.rank_offline_bundles([(self.cache_path(), "cache"),
                                       (self.snapshot_path(), "snapshot")])

    def probe_live(self, timeout: float | None = None) -> tuple[dict | None, ZeneControlError | None]:
        """`control.commands_list` from the running instance, or (None, why not).

        The read-only twin of `Bridge.fetch_live`: the same two socket round-trips,
        but nothing is written, so a diagnostic (`zene_status`) can look at the
        live surface without changing which list the next offline call serves.
        Never raises: the error is returned, because every caller here is
        reporting on an instance that may be absent.
        """
        budget = timeout or min(self.config.call_timeout, COMMANDS_LIST_TIMEOUT)
        try:
            with ControlClient(self.config.socket_path, budget, self.config.proto) as client:
                ping = client.handshake(wait_ready=False)
                result = client.request("control.commands_list", timeout=budget)
        except ZeneControlError as exc:
            return None, exc
        except Exception as exc:  # defensive: a listing must never kill a diagnostic
            return None, ZeneControlError(ErrorKind.BRIDGE_ERROR,
                                          f"could not read the live command list: {exc}")
        bundle = R.bundle_from_result(result, {
            "version": ping.get("version"),
            "proto": ping.get("proto"),
            "engine_ready": ping.get("engine_ready"),
            "socket_path": self.config.socket_path,
        })
        return bundle, None

    # -- the comparison ---------------------------------------------------

    def offline_drift(self, live_bundle: dict) -> dict:
        """Every readable offline copy measured against one live instance's list.

        The answer to "is the offline list I would be served stale?" that the
        bridge could not give before: a copy carries its own surface fingerprint,
        so the comparison is between two recorded facts and not between two
        timestamps an agent has to reason about. `copies: []` with `checked: true`
        means nothing readable — a fact, not a pass.
        """
        copies = []
        for path, source, bundle in self.offline_copies():
            drift = R.surface_drift(live_bundle.get("commands"), bundle.get("commands"))
            copies.append({
                "path": path,
                "source": source,
                "captured_at": bundle.get("captured_at"),
                "instance_version": (bundle.get("instance") or {}).get("version"),
                "surface": R.bundle_surface(bundle),
                **drift,
            })
        return {
            "checked": True,
            "stale": any(copy["stale"] for copy in copies),
            "stale_copies": [copy["source"] for copy in copies if copy["stale"]],
            "live_surface": R.surface_fingerprint(live_bundle.get("commands")),
            "copies": copies,
            "fix": SNAPSHOT_FIX,
        }
