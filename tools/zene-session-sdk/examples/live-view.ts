// Usage example: follow an open Zene project, make one edit, see its event, and recover by itself after a gap.
//
//   zene --control-socket /tmp/zene.sock            (start Zene with its local control socket)
//   ZENE_CONTROL_SOCKET=/tmp/zene.sock node --experimental-strip-types examples/live-view.ts [--once]
//
// With --once it exits after its own edit's event arrived; without it, it prints every change until Ctrl+C.

import {
	LiveSession,
	ZeneClient,
	ZeneCommandError,
	type ProjectChangedEvent,
	type ProjectState,
	type SyncInfo,
} from "@zene-studio/session-sdk";

async function main(): Promise<number> {
	const once = process.argv.includes("--once");

	// 1. Connect. LiveSession uses ZeneClient underneath; the path defaults to $ZENE_CONTROL_SOCKET.
	const view = await LiveSession.start<ProjectState>({
		read: (client) => client.call("project.get_state"),
		queueLimit: 256,
	});
	view.onSync((sync: SyncInfo<ProjectState>) => {
		// After an overflow or a reconnect: the view was refreshed and says how much it missed.
		console.log(`resynced after ${sync.reason}: ${sync.state.track_count ?? 0} tracks, missed ${sync.missed ?? "unknown"}`);
	});
	view.onStale((end) => console.log(`view is stale (${end.reason}); recovering`));
	console.log(`connected: ${view.state.track_count ?? 0} tracks at seq ${view.cursor.seq}`);

	// 2. Edit through a registered command, on the view's own connection. It is one undo step in Zene.
	const client: ZeneClient | null = view.client;
	if (client === null) {
		throw new Error("not connected");
	}
	const ownEvent = new Promise<ProjectChangedEvent>((resolve) => {
		const unlisten = view.onChange((event) => {
			console.log(`#${event.seq} ${event.kind} ${event.command ?? "(interface)"}`);
			if (event.command === "track.add") {
				unlisten();
				resolve(event);
			}
		});
	});
	const added = await client.call("track.add", { type: "instrument", name: "From the SDK" });
	console.log(`added ${added.track}`);

	// 3. A refused command is a typed error; nothing changed.
	try {
		await client.call("track.rename", { track: "trk-does-not-exist", name: "x" });
	} catch (error) {
		if (error instanceof ZeneCommandError) {
			console.log(`refused as expected: ${error.kind}`);
		} else {
			throw error;
		}
	}

	// 4. The edit's event, then read the current state.
	const event = await ownEvent;
	const state = await client.call("project.get_state");
	console.log(`event #${event.seq} for ${event.command}; the project now has ${state.track_count ?? 0} tracks`);

	if (!once) {
		await new Promise<void>((resolve) => process.once("SIGINT", () => resolve()));
	}
	// 5. Unsubscribe and close.
	await view.stop();
	return 0;
}

main().then((code) => process.exit(code), (error: unknown) => {
	console.error(error);
	process.exit(1);
});
