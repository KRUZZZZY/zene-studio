// Q050-334.2: the SDK over its own named-pipe transport. This drives the TypeScript SDK (connect, read, edit, event,
// unsubscribe, close) against a real Zene that was started with --control-socket <path>. The path is whatever the
// launcher (tests/session-sdk-pipe-smoke.py) chose: \\.\pipe\<name> on Windows, a unix socket on POSIX. This file
// imports no AF_UNIX-only API: ZeneClient.connect({ path }) picks the transport from the path, so the same file runs
// against \\.\pipe\<name> on Windows and against a unix socket here. On POSIX that proves this file's logic only; the
// Windows half is proved on hosted CI by ControlSessionSdkPipeSmoke.
//
// Usage: node --experimental-strip-types test/pipe-smoke.ts <control-path>
// Exit 0 only when every check passed and the connection closed cleanly.

import { PROTOCOL_VERSION, ZeneClient, type ProjectChangedEvent } from "../src/index.ts";

const BOUND_MS = 30_000;

function check(condition: boolean, text: string): void {
	if (!condition) {
		throw new Error(`check failed: ${text}`);
	}
	console.log(`PASS: ${text}`);
}

async function waitUntil(predicate: () => boolean | Promise<boolean>, what: string, ms = BOUND_MS): Promise<void> {
	const deadline = Date.now() + ms;
	while (!(await predicate())) {
		if (Date.now() > deadline) {
			throw new Error(`timed out waiting for ${what}`);
		}
		await new Promise((resolve) => setTimeout(resolve, 20));
	}
}

/** Lets every event already queued arrive: the instance runs one turn per ping, then flushes. */
async function barrier(client: ZeneClient): Promise<void> {
	for (let i = 0; i < 2; ++i) {
		await client.call("control.ping");
		await new Promise((resolve) => setTimeout(resolve, 50));
	}
}

async function main(argv: string[]): Promise<number> {
	const path = argv[2];
	if (path === undefined) {
		console.error("usage: pipe-smoke.ts <control-path>");
		return 2;
	}
	const client = await ZeneClient.connect({ path });
	check(client.version !== null && PROTOCOL_VERSION === 1,
		`connect over ${path} checks control.version (protocol ${PROTOCOL_VERSION})`);

	const state = await client.call("project.get_state");
	check(Array.isArray(state.tracks), "project.get_state reads the project (tracks is an array)");

	// Subscribe BEFORE the edit: an event for an edit made before the subscription never reaches it.
	const events: ProjectChangedEvent[] = [];
	const sub = await client.subscribe();
	sub.onEvent((event) => events.push(event));

	const added = await client.call("track.add", { type: "instrument", name: "pipe smoke" });
	check(typeof added.track === "string" && added.track.length > 0, "track.add edits the project and returns a track id");
	await barrier(client);
	await waitUntil(() => events.some((event) => event.command === "track.add" && event.args?.name === "pipe smoke"),
		"the event for the first track.add");
	check(true, "the subscribed connection receives the track.add event");

	await sub.unsubscribe();
	const lastDelivered = Math.max(0, ...events.map((event) => event.seq));
	await client.call("track.add", { type: "instrument", name: "after unsubscribe" });
	await new Promise((resolve) => setTimeout(resolve, 1000));
	const late = events.filter((event) => event.seq > lastDelivered);
	check(late.length === 0, "after unsubscribe no event with a later seq arrives");

	await client.close();
	check(true, "close() ends the connection");
	console.log("PASS");
	return 0;
}

const watchdog = setTimeout(() => {
	console.error("FAIL: the pipe smoke did not finish within 120 s");
	process.exit(1);
}, 120_000);
watchdog.unref();

main(process.argv).then((code) => process.exit(code), (error: unknown) => {
	console.error(`FAIL: ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
	process.exit(1);
});
