#!/usr/bin/env python3
"""Q050-501: the update-check commands over the real control socket, against a feed on loopback.

Off by default and refused without a request; set saves into the config file; a check runs in the
background and its answer is readable; the request says only the version and platform.

Usage: QT_QPA_PLATFORM=offscreen python3 control-update-check.py <zene>
"""

import http.server
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import control_socket_harness as H  # noqa: E402

REQUESTS = []
BODY = json.dumps({"tag_name": "v999.0.0", "name": "Far future", "body": "Notes", "draft": False,
                   "prerelease": False, "html_url": "https://example.invalid/page"}).encode()


class Feed(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        REQUESTS.append((self.command, self.path, dict((k.lower(), v) for k, v in self.headers.items())))
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(BODY)))
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, *_):
        pass


def started(instance, transcript):
    instance.spawn()
    instance.wait_for_socket(H.SOCKET_TIMEOUT)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    return client


def poll_until_newer(client):
    deadline = time.time() + 20
    result = {}
    request_id = 100
    while time.time() < deadline and result.get("state") != "newer":
        request_id += 1
        result = H.ok_result(client.call(request_id, "app.update_check_get"), request_id)["result"]
        time.sleep(0.05)
    return result


def check_request(problems):
    problems.require(len(REQUESTS) == 1, "one check, one request: %r" % REQUESTS)
    if not REQUESTS:
        return
    method, path, headers = REQUESTS[0]
    problems.require(method == "GET" and path == "/releases", "the request: %s %s" % (method, path))
    problems.require(headers.get("user-agent", "").startswith("ZeneStudio/"), "user agent: %r" % headers)
    problems.require(set(headers) <= {"host", "user-agent", "accept", "accept-language", "accept-encoding", "connection"},
                     "unexpected headers: %r" % headers)


def check_commands(binary, feed, problems, transcript):
    """Off, refused, set and saved, one background request, off again."""
    with H.Instance(binary, extra_xml='  <updatecheck feed="%s"/>' % feed) as instance:
        client = started(instance, transcript)
        state = H.ok_result(client.call(1, "app.update_check_get", transcript=transcript), 1)
        problems.require(state["enabled"] is False and state["answered"] is False, "a fresh config is off: %r" % state)
        problems.require(state["feed"] == feed, "the configured feed is read: %r" % state["feed"])

        H.typed_error(client.call(2, "app.update_check_run", transcript=transcript), 2, "refused")
        time.sleep(0.3)
        problems.require(not REQUESTS, "off made a request: %r" % REQUESTS)

        reply = H.ok_result(client.call(3, "app.update_check_set", {"enabled": True, "prereleases": False}, transcript=transcript), 3)
        problems.require(reply["enabled"] is True, "set: %r" % reply)
        recorded = json.dumps(H.ok_result(client.call(30, "control.transactions", transcript=transcript), 30))
        problems.require("app.update_check_set" in recorded, "set recorded no transaction: %s" % recorded[:400])
        problems.require(not REQUESTS, "setting the preference made a request: %r" % REQUESTS)
        with open(instance.config_path, encoding="utf-8") as saved:
            problems.require('enabled="1"' in saved.read(), "the choice is not in the config file")

        H.ok_result(client.call(4, "app.update_check_run", transcript=transcript), 4)
        result = poll_until_newer(client)
        problems.require(result.get("state") == "newer" and result.get("version") == "999.0.0", "result: %r" % result)
        problems.require(result.get("url") == "https://example.invalid/page", "url: %r" % result)
        check_request(problems)

        H.ok_result(client.call(5, "app.update_check_set", {"enabled": False}, transcript=transcript), 5)
        H.typed_error(client.call(6, "app.update_check_run", transcript=transcript), 6, "refused")
        time.sleep(0.3)
        problems.require(len(REQUESTS) == 1, "turning it off did not stop requests: %r" % REQUESTS)
        client.close()


def check_agent_start(binary, feed, problems, transcript):
    """An agent's instance starts no daily check of its own, even with consent in its config."""
    REQUESTS.clear()
    extra = '  <updatecheck feed="%s" answered="1" enabled="1"/>' % feed
    with H.Instance(binary, extra_xml=extra) as instance:
        client = started(instance, transcript)
        time.sleep(5)  # past the interface's 4 s start delay
        problems.require(not REQUESTS, "an agent instance checked by itself: %r" % REQUESTS)
        client.close()


def check_sandboxed(binary, feed, problems, transcript):
    """A copy something else updates: the ids do not exist and the feed hears nothing, even with consent in its config."""
    REQUESTS.clear()
    extra = '  <updatecheck feed="%s" answered="1" enabled="1"/>' % feed
    with H.Instance(binary, extra_xml=extra, extra_env={"FLATPAK_ID": "org.example.Zene"}) as instance:
        client = started(instance, transcript)
        commands = H.ok_result(client.call(7, "control.commands_list"), 7).get("commands") or []
        ids = [c["id"] for c in commands]
        problems.require("app.whats_new" in ids, "the command list was not read: %d entries" % len(ids))
        problems.require(not [i for i in ids if "update_" in i], "a Flatpak copy lists the commands")
        problems.require(client.call(8, "app.update_check_get", transcript=transcript).get("ok") is False, "get answered in a Flatpak copy")
        time.sleep(0.3)
        problems.require(not REQUESTS, "a Flatpak copy made a request: %r" % REQUESTS)
        client.close()


def check_docs(problems):
    """The manual's Help section and the limitations page say what is sent and that installing stays manual."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "data/manual/index.html"), encoding="utf-8") as page:
        manual = page.read()
    with open(os.path.join(root, "docs/KNOWN-LIMITATIONS.md"), encoding="utf-8") as page:
        limits = page.read()
    for needle in ("Help &gt; Check for Updates", "What is sent:", "ZeneStudio/",
                   "https://api.github.com/repos/KRUZZZZY/zene-studio/releases?per_page=10",
                   "Installation stays manual", "IP address"):
        problems.require(needle in manual, "the manual does not say %r" % needle)
    for needle in ("What is sent, exactly", "ZeneStudio/<version>", "Installation stays manual", "signed installers", "IP address"):
        problems.require(needle in limits, "KNOWN-LIMITATIONS does not say %r" % needle)


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Feed)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    feed = "http://127.0.0.1:%d/releases" % server.server_address[1]
    problems = H.Problems()
    transcript = H.Transcript()
    check_docs(problems)
    check_commands(sys.argv[1], feed, problems, transcript)
    check_agent_start(sys.argv[1], feed, problems, transcript)
    check_sandboxed(sys.argv[1], feed, problems, transcript)
    server.shutdown()
    return H.finish([("update check over the socket", not problems, list(problems.items))])


if __name__ == "__main__":
    sys.exit(main())
