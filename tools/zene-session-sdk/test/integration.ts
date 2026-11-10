// Q050-334.2: the SDK against a REAL Zene instance. Every call, subscription and recovery below goes through the
// package's own exports; the only thing that is not the SDK is the byte proxy that makes a connection drop or stall.
//
// Usage: node --experimental-strip-types test/integration.ts <control-socket-path>
// (tests/control-session-sdk.py starts the instance and runs this.) Exit 0 only when every check passed.

import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import {
	LiveSession,
	PROTOCOL_VERSION,
	SDK_VERSION,
	ZeneClient,
	ZeneCommandError,
	ZeneConnectionError,
	type ProjectChangedEvent,
	type ProjectState,
	type Subscription,
	type SubscriptionEnd,
	type SyncInfo,
} from "../src/index.ts";
import { Proxy } from "./proxy.ts";

const BOUND_MS = 60_000;

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

async function refusal(promise: Promise<unknown>): Promise<ZeneCommandError> {
	try {
		await promise;
	} catch (error) {
		if (error instanceof ZeneCommandError) {
			return error;
		}
		throw error;
	}
	throw new Error("expected a typed refusal, the call succeeded");
}

/** Lets every event already queued arrive: the instance runs one turn per ping, then flushes. */
async function barrier(client: ZeneClient): Promise<void> {
	for (let i = 0; i < 4; ++i) {
		await client.call("control.ping");
		await new Promise((resolve) => setTimeout(resolve, 50));
	}
}

function collect(subscription: Subscription): ProjectChangedEvent[] {
	const events: ProjectChangedEvent[] = [];
	subscription.onEvent((event) => events.push(event));
	return events;
}

async function trackCount(client: ZeneClient): Promise<number> {
	const state = await client.call("project.get_state");
	return state.tracks?.length ?? -1;
}

async function checkPackage(): Promise<void> {
	const manifest = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")) as { version: string };
	check(SDK_VERSION === manifest.version && /^\d+\.\d+\.\d+$/.test(SDK_VERSION),
		`the package is versioned (${SDK_VERSION}) and exports its version`);
	const nowhere = join(tmpdir(), `zene-sdk-nowhere-${process.pid}.sock`);
	try {
		await ZeneClient.connect({ path: nowhere, connectTimeoutMs: 2000 });
		throw new Error("connected to nothing");
	} catch (error) {
		check(error instanceof ZeneConnectionError && error.code === "connection",
			"connecting where nothing listens is a typed ZeneConnectionError");
	}
}

async function checkReadEditEvent(client: ZeneClient, editor: ZeneClient): Promise<{ sub: Subscription; events: ProjectChangedEvent[] }> {
	check(client.version !== null && PROTOCOL_VERSION === 1, `connect checked the protocol (Zene ${client.version})`);
	check((await client.call("control.ping")).pong === true, "a read call answers (control.ping)");
	const before = await trackCount(client);
	check(before >= 0, `project.get_state reads the open project (${before} tracks)`);

	const sub = await client.subscribe({ queueLimit: 64 });
	const events = collect(sub);
	check(sub.queueLimit === 64 && sub.isActive && typeof sub.startSeq === "number", "subscribe reports its bound and position");
	const busy = await refusal(client.subscribe());
	check(busy.kind === "busy" && busy.command === "events.subscribe", "a second subscription on one connection is a typed 'busy'");

	// The SDK's own edit, then another client's, a refusal and a read (neither is an event), then an undo.
	const added = await client.call("track.add", { type: "instrument", name: "sdk one" });
	check(typeof added.track === "string", `the SDK edits through a registered command (track.add -> ${added.track})`);
	await editor.call("track.rename", { track: added.track, name: "sdk two" });
	const missing = await refusal(editor.call("track.rename", { track: "trk-999999", name: "x" }));
	check(missing.kind === "not_found", "a refused edit is a typed ZeneCommandError with the protocol's kind");
	await editor.call("project.get_state");
	await editor.call("control.undo");
	await waitUntil(() => events.length >= 3, "three events");
	await barrier(client);
	checkThreeEvents(sub, events);
	check(await trackCount(client) === before + 1, "rereading shows the edit");
	return { sub, events };
}

function checkThreeEvents(sub: Subscription, events: ProjectChangedEvent[]): void {
	check(events.length === 3, "exactly the three successful changes arrived (no event for the refusal or the read)");
	const [first, second, third] = events as [ProjectChangedEvent, ProjectChangedEvent, ProjectChangedEvent];
	check(first.command === "track.add" && first.kind === "edit" && first.source === "command" &&
		first.args?.name === "sdk one", "the SDK's own edit arrives as the event identifying it");
	check(second.command === "track.rename" && second.args?.name === "sdk two", "another client's edit arrives too");
	check(third.kind === "undo" && third.command === "control.undo", "undo is told apart");
	check(events.map((event) => event.seq).join() === [1, 2, 3].map((n) => n + sub.startSeq).join(),
		"seq rises by one from the subscription's start");
	check(sub.lastSeq === third.seq && sub.cursor.session === sub.session, "the subscription's cursor follows the events");
}

async function checkIteratorAndUnsubscribe(client: ZeneClient, editor: ZeneClient, sub: Subscription,
	events: ProjectChangedEvent[]): Promise<void> {
	const iterator = sub.events();
	await editor.call("control.redo");
	const next = await iterator.next();
	check(!next.done && next.value.kind === "redo", "for-await over subscription.events() yields the next change");
	await iterator.return?.();

	const result = await sub.unsubscribe();
	check(result !== null && result.unsubscribed && result.last_delivered_seq === sub.lastSeq,
		"unsubscribe reports the last delivered seq");
	const end = await sub.ended;
	check(end.reason === "unsubscribed" && !sub.isActive, "the subscription ended, reason 'unsubscribed'");
	check(await sub.unsubscribe() === null, "unsubscribing an ended subscription asks nothing");

	const count = events.length;
	const tracks = (await editor.call("project.get_state")).tracks ?? [];
	await editor.call("track.rename", { track: tracks[tracks.length - 1] ?? "", name: "after unsubscribe" });
	await barrier(client);
	check(events.length === count, "after unsubscribe no further event arrives");
	const state = await editor.call("events.get_state");
	check(!state.subscriptions.some((row) => row.subscription === sub.id), "the instance holds no subscription for it");

	const unknown = await refusal(client.call("no.such_command"));
	check(unknown.kind.length > 0 && unknown.requestId > 0, `an unknown command is a typed refusal (${unknown.kind})`);

	const other = await ZeneClient.connect({ path: client.path });
	const otherSub = await other.subscribe();
	let closed: string | null = "never";
	other.onClose((error) => {
		closed = error === null ? null : error.message;
	});
	await other.close();
	const lost = await otherSub.ended;
	check(lost.reason === "disconnected" && closed === null && !other.isOpen,
		"closing the connection ends its subscription, reason 'disconnected'");
	const failed = await other.call("control.ping").then(() => null, (error: unknown) => error);
	check(failed instanceof ZeneConnectionError, "a call on a closed connection is a typed ZeneConnectionError");
}

interface Observed {
	syncs: SyncInfo<ProjectState>[];
	changes: ProjectChangedEvent[];
	stales: SubscriptionEnd[];
	errors: Error[];
}

function observe(live: LiveSession<ProjectState>): Observed {
	const seen: Observed = { syncs: [], changes: [], stales: [], errors: [] };
	live.onSync((sync) => seen.syncs.push(sync));
	live.onChange((event) => seen.changes.push(event));
	live.onStale((end) => seen.stales.push(end));
	live.onError((error) => seen.errors.push(error));
	return seen;
}

async function overflowLive(live: LiveSession<ProjectState>, editor: ZeneClient, proxy: Proxy): Promise<{ edits: number; pending: Promise<unknown>[] }> {
	// The view stops reading: two replies far larger than the socket buffer are left unread, so the instance holds
	// a reply tail and every later change waits in the view's bounded queue.
	proxy.stall();
	const client = live.client;
	if (client === null) {
		throw new Error("the live session has no connection");
	}
	const pending = [0, 1].map(() => client.call("control.commands_list", {}, { timeoutMs: 120_000 }));
	let edits = 0;
	let row = undefined as { queued: number; overflowed: boolean; queue_limit: number } | undefined;
	while (edits < 400) {
		await editor.call("track.add", { type: "instrument", name: `overflow ${edits}` });
		++edits;
		row = (await editor.call("events.get_state")).subscriptions[0];
		if (row?.overflowed === true) {
			break;
		}
	}
	check(row !== undefined && row.overflowed && row.queued === row.queue_limit && row.queue_limit === 4,
		`a view that stops reading is held at its bound in the instance (queued ${row?.queued} of 4 after ${edits} edits)`);
	return { edits, pending };
}

async function checkOverflowRecovery(live: LiveSession<ProjectState>, seen: Observed, editor: ZeneClient,
	proxy: Proxy): Promise<void> {
	const syncsBefore = seen.syncs.length;
	const { pending } = await overflowLive(live, editor, proxy);
	await editor.call("track.add", { type: "instrument", name: "after the bound" });
	proxy.resume();
	const replies = await Promise.all(pending);
	check(replies.length === 2, "the replies the view had not read still arrive whole");
	await waitUntil(() => seen.syncs.length > syncsBefore, "the resync after the overflow");

	const stale = seen.stales[seen.stales.length - 1];
	const sync = seen.syncs[seen.syncs.length - 1] as SyncInfo<ProjectState>;
	const notice = stale?.notice;
	check(stale?.reason === "overflow" && notice !== undefined && notice.missed > 0,
		`the overflow is detectable: onStale('overflow'), ${notice?.missed} changes named as missed`);
	const delivered = seen.changes.filter((event) => event.subscription === notice?.subscription).map((event) => event.seq);
	check(delivered.every((seq, i) => i === 0 || seq === (delivered[i - 1] ?? 0) + 1) &&
		delivered[delivered.length - 1] === notice?.last_delivered_seq,
		"the changes delivered before it are gap-free and end at the notice's last_delivered_seq");
	check(seen.changes.every((event) => event.seq < (notice?.first_missed_seq ?? 0) || event.subscription !== notice?.subscription),
		"no missed change is claimed as received");
	check(sync.reason === "overflow" && sync.missed === sync.cursor.seq - (notice?.last_delivered_seq ?? 0) &&
		!sync.sessionChanged && !live.isStale, `the view resynced by itself and says it missed ${sync.missed}`);
	check(sync.state.tracks?.length === await trackCount(editor), "the reread state includes every missed edit");

	await editor.call("track.add", { type: "instrument", name: "after overflow" });
	await waitUntil(() => seen.changes.some((event) => event.seq === sync.cursor.seq + 1), "the next change");
	check(true, "the new subscription receives the next change");
}

async function checkDisconnectRecovery(live: LiveSession<ProjectState>, seen: Observed, editor: ZeneClient,
	proxy: Proxy): Promise<void> {
	const syncsBefore = seen.syncs.length;
	const errorsBefore = seen.errors.length;
	const lastSeen = live.cursor.seq;
	proxy.cut();
	await waitUntil(() => seen.stales[seen.stales.length - 1]?.reason === "disconnected", "onStale('disconnected')");
	check(live.isStale, "a lost connection is detectable: onStale('disconnected'), isStale");
	const before = await trackCount(editor);
	await editor.call("track.add", { type: "instrument", name: "while away 1" });
	await editor.call("track.add", { type: "instrument", name: "while away 2" });
	await waitUntil(() => seen.errors.length > errorsBefore, "a refused reconnect attempt");
	check(seen.syncs.length === syncsBefore, "while the instance cannot be reached the view stays stale and retries");
	proxy.up();
	await waitUntil(() => seen.syncs.length > syncsBefore, "the resync after reconnecting");
	const sync = seen.syncs[seen.syncs.length - 1] as SyncInfo<ProjectState>;
	check(sync.reason === "disconnected" && sync.missed === 2 && !sync.sessionChanged,
		"after reconnecting the view says exactly two changes were missed; none is replayed");
	check(!seen.changes.some((event) => event.seq > lastSeen && event.seq <= lastSeen + 2),
		"the two missed changes never arrive as events");
	check(sync.state.tracks?.length === before + 2, "the reread state shows the changes made while away");
	await editor.call("track.add", { type: "instrument", name: "after reconnect" });
	await waitUntil(() => seen.changes.some((event) => event.seq === lastSeen + 3), "the change after reconnect");
	check(true, "the reconnected view receives the next change");
}

async function checkLiveSession(path: string, editor: ZeneClient): Promise<void> {
	const scratch = mkdtempSync(join("/tmp", "zsdk-"));
	const proxy = await Proxy.listen(join(scratch, "proxy.sock"), path);
	try {
		const live = await LiveSession.start<ProjectState>({
			path: proxy.path,
			read: (client) => client.call("project.get_state"),
			queueLimit: 4,
			reconnectDelayMs: 50,
			maxReconnectDelayMs: 400,
		});
		const seen = observe(live);
		check(live.state.tracks?.length === await trackCount(editor), "LiveSession connects and reads the state");
		const own = await live.client?.call("track.add", { type: "instrument", name: "live one" });
		await waitUntil(() => seen.changes.some((event) => event.args?.name === "live one"), "the live edit's event");
		check(own !== undefined, "LiveSession's connection edits and its event arrives");

		await checkOverflowRecovery(live, seen, editor, proxy);
		await checkDisconnectRecovery(live, seen, editor, proxy);

		await live.stop();
		await waitUntil(async () => (await editor.call("events.get_state")).subscriptions.length === 0,
			"the stopped view's subscription to go");
		check(live.client === null, "stop() unsubscribes and closes");
	} finally {
		await proxy.close();
		rmSync(scratch, { recursive: true, force: true });
	}
}

async function main(argv: string[]): Promise<number> {
	const path = argv[2];
	if (path === undefined) {
		console.error("usage: integration.ts <control-socket-path>");
		return 2;
	}
	await checkPackage();
	const client = await ZeneClient.connect({ path });
	const editor = await ZeneClient.connect({ path });
	const { sub, events } = await checkReadEditEvent(client, editor);
	await checkIteratorAndUnsubscribe(client, editor, sub, events);
	await checkLiveSession(path, editor);
	await client.close();
	await editor.close();
	console.log("PASS");
	return 0;
}

const watchdog = setTimeout(() => {
	console.error("FAIL: the test did not finish within 240 s");
	process.exit(1);
}, 240_000);
watchdog.unref();

main(process.argv).then((code) => process.exit(code), (error: unknown) => {
	console.error(`FAIL: ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
	process.exit(1);
});
