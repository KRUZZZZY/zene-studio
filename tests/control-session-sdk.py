#!/usr/bin/env python3
"""Q050-334.2: the public TypeScript session SDK (tools/zene-session-sdk) against a REAL `zene`.

This script only starts the instance (the shared harness's headless recipe) and stops it. Everything in between is
the SDK's own code run by Node: tools/zene-session-sdk/test/integration.ts connects, reads, edits, receives the
matching events, unsubscribes and recovers after an overflow and after a dropped connection; then the package's
usage example (examples/live-view.ts --once) runs against the same instance as a published client would.

Node runs the TypeScript sources directly (type stripping, Node >= 22.6). Older or missing Node: exit 77 (ctest
Skipped) - the ctest is registered only when CMake found a node, so this is the old-Node case only.

Usage: QT_QPA_PLATFORM=offscreen python3 tests/control-session-sdk.py <zene> <node> <sdk-dir>
Exit code 0 only when every check passed.
"""

import os
import re
import subprocess
import sys

import control_socket_harness as H

NODE_FLAGS = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"]
RUN_BOUND_S = 300


def node_supports_type_stripping(node):
    try:
        out = subprocess.run([node, "--version"], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    match = re.match(r"v(\d+)\.(\d+)", out.strip())
    return bool(match) and (int(match.group(1)), int(match.group(2))) >= (22, 6)


def run_node(node, sdk, script, args, extra_env=None):
    env = dict(os.environ)
    env.update(extra_env or {})
    command = [node] + NODE_FLAGS + script + args
    print("running: %s" % " ".join(command), flush=True)
    result = subprocess.run(command, cwd=sdk, env=env, timeout=RUN_BOUND_S, check=False)
    return result.returncode


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    zene, node, sdk = argv[1], argv[2], os.path.abspath(argv[3])
    if not node_supports_type_stripping(node):
        print("SKIP: %s cannot run TypeScript sources (needs Node >= 22.6)" % node)
        return 77
    with H.start_instance(zene) as instance:
        probe = H.connect(instance)
        H.wait_ready(instance, probe, None)
        probe.close()
        path = instance.socket_path
        code = run_node(node, sdk, ["test/integration.ts"], [path])
        if code != 0:
            instance.dump_log()
            print("FAIL: the SDK integration test exited %d" % code)
            return 1
        H.ok("the SDK integration test passed against the real instance")
        # The usage example, resolved through the package's own name and "exports" (the zene-source condition maps
        # it to the sources), exactly as a client importing @zene-studio/session-sdk would.
        code = run_node(node, sdk, ["--conditions=zene-source", "examples/live-view.ts"], ["--once"],
                        {"ZENE_CONTROL_SOCKET": path})
        if code != 0:
            instance.dump_log()
            print("FAIL: the usage example exited %d" % code)
            return 1
        H.ok("the usage example runs against the real instance")
        closer = H.connect(instance)
        H.ok_result(closer.call(9001, "control.quit", {"save": False}), 9001)
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
