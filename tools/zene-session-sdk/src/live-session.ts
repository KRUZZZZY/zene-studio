// A view of an open Zene project that keeps itself current: it follows the change events and, after an overflow
// or a lost connection, reconnects, subscribes again and rereads the state - the protocol's documented recovery.

import { ZeneClient, type ConnectOptions } from "./client.ts";
import { ZeneError } from "./errors.ts";
import { HeldListeners, Listeners, rethrowLater, type Unlisten } from "./listeners.ts";
import type { Cursor, ProjectChangedEvent } from "./protocol.ts";
import type { Subscription, SubscriptionEnd, SubscriptionEndReason } from "./subscription.ts";

export type SyncReason = "initial" | Exclude<SubscriptionEndReason, "unsubscribed">;

/** Changes held for a LiveSession with no onChange listener; past it they are not kept (nobody listens). */
export const MAX_HELD_CHANGES = 8192;

export interface SyncInfo<S> {
	/** What made this sync happen. */
	reason: SyncReason;
	state: S;
	cursor: Cursor;
	/** Changes since the previous cursor that never arrived as events; null when unknown (first sync, new session). */
	missed: number | null;
	/** The instance is another process than before: everything known about the old one is stale. */
	sessionChanged: boolean;
}

export interface LiveSessionOptions<S> extends ConnectOptions {
	/** Reads the state the application keeps, e.g. `(client) => client.call("project.get_state")`. */
	read: (client: ZeneClient) => Promise<S>;
	/** Changes the instance holds for this view before it overflows (default 256). */
	queueLimit?: number;
	/** First wait before a reconnect attempt; doubles per failure up to maxReconnectDelayMs (default 200 ms). */
	reconnectDelayMs?: number;
	maxReconnectDelayMs?: number;
}

/**
 * Lifecycle: start() connects, subscribes and reads (`onSync` reason `initial`). Each change then arrives at
 * `onChange` in order. When the subscription ends other than by stop(), `onStale` fires: `state` no longer reflects
 * every change. The session then resyncs by itself (reconnecting with back-off if the connection was lost) and fires
 * `onSync` with the new state and how many changes it missed.
 */
export class LiveSession<S> {
	private readonly options: LiveSessionOptions<S>;
	private currentClient: ZeneClient | null = null;
	private subscription: Subscription | null = null;
	private latest: SyncInfo<S> | null = null;
	private position: Cursor | null = null;
	private stopped = false;
	private staleSince: SyncReason | null = null;
	private wake: (() => void) | null = null;
	private readonly syncListeners = new Listeners<[SyncInfo<S>]>(rethrowLater);
	// Changes that arrive before the caller's first onChange (start() resolves first) are held for it, not lost.
	private readonly changeListeners = new HeldListeners<ProjectChangedEvent>(MAX_HELD_CHANGES, () => {});
	private readonly staleListeners = new Listeners<[SubscriptionEnd]>(rethrowLater);
	private readonly errorListeners = new Listeners<[Error]>(rethrowLater);

	private constructor(options: LiveSessionOptions<S>) {
		this.options = options;
	}

	/** Connects and makes the first sync; rejects if that fails (later failures are retried). */
	static async start<S>(options: LiveSessionOptions<S>): Promise<LiveSession<S>> {
		const session = new LiveSession(options);
		const client = await ZeneClient.connect(options);
		await session.sync(client, "initial");
		return session;
	}

	/** The state from the last sync. Apply `onChange` events to it, or reread, to keep it current. */
	get state(): S {
		return this.lastSync().state;
	}

	/** The last change this view received: delivered to onChange, or held for its first listener. */
	get cursor(): Cursor {
		return this.position ?? this.lastSync().cursor;
	}

	/** True between a lost subscription and the sync that recovers from it. */
	get isStale(): boolean {
		return this.staleSince !== null;
	}

	/** The live connection, for edits; null while reconnecting. */
	get client(): ZeneClient | null {
		return this.currentClient;
	}

	onSync(fn: (sync: SyncInfo<S>) => void): Unlisten {
		return this.syncListeners.add(fn);
	}

	/**
	 * Each change, in `seq` order, including those that arrived while a sync was reading the state. Changes that
	 * arrived before the first listener reach it first, a microtask after it attaches.
	 */
	onChange(fn: (event: ProjectChangedEvent) => void): Unlisten {
		return this.changeListeners.add(fn);
	}

	onStale(fn: (end: SubscriptionEnd) => void): Unlisten {
		return this.staleListeners.add(fn);
	}

	/** A failed reconnect or reread; the session keeps retrying until stop(). */
	onError(fn: (error: Error) => void): Unlisten {
		return this.errorListeners.add(fn);
	}

	/** Unsubscribes, closes the connection and stops recovering. */
	async stop(): Promise<void> {
		this.stopped = true;
		this.wake?.();
		const client = this.currentClient;
		const subscription = this.subscription;
		this.currentClient = null;
		this.subscription = null;
		if (subscription !== null && client !== null && client.isOpen) {
			await subscription.unsubscribe().catch(() => null);
		}
		await client?.close();
	}

	private lastSync(): SyncInfo<S> {
		if (this.latest === null) {
			throw new ZeneError("connection", "the live session has not synced yet");
		}
		return this.latest;
	}

	private async sync(client: ZeneClient, reason: SyncReason): Promise<void> {
		const resyncOptions = {
			...(this.options.queueLimit === undefined ? {} : { queueLimit: this.options.queueLimit }),
			...(this.position === null ? {} : { since: this.position }),
		};
		const result = await client.resync(this.options.read, resyncOptions);
		if (this.stopped) {
			await result.subscription.unsubscribe().catch(() => null);
			return;
		}
		this.currentClient = client;
		this.subscription = result.subscription;
		this.position = result.cursor;
		this.staleSince = null;
		result.subscription.onEvent((event) => {
			this.position = { session: event.session, seq: event.seq };
			this.changeListeners.push(event);
		});
		result.subscription.onEnd((end) => this.onSubscriptionEnd(client, end));
		const info: SyncInfo<S> = {
			reason,
			state: result.state,
			cursor: result.cursor,
			missed: result.missed,
			sessionChanged: result.sessionChanged,
		};
		this.latest = info;
		this.syncListeners.emit(info);
	}

	private onSubscriptionEnd(client: ZeneClient, end: SubscriptionEnd): void {
		if (this.stopped || end.reason === "unsubscribed") {
			return;
		}
		this.subscription = null;
		this.staleSince = end.reason;
		this.staleListeners.emit(end);
		void this.recover(client, end.reason);
	}

	private async recover(client: ZeneClient, reason: SyncReason): Promise<void> {
		let delay = this.options.reconnectDelayMs ?? 200;
		const maxDelay = this.options.maxReconnectDelayMs ?? 5000;
		// After an overflow the instance has ended the subscription and the connection is fine. After a gap the
		// instance still holds it, so start over on a new connection.
		let current: ZeneClient | null = client.isOpen && reason === "overflow" ? client : null;
		if (current === null) {
			await client.close();
		}
		while (!this.stopped) {
			try {
				if (current === null) {
					this.currentClient = null;
					current = await ZeneClient.connect(this.options);
				}
				await this.sync(current, reason);
				if (this.stopped) {
					await current.close();
				}
				return;
			} catch (error) {
				this.errorListeners.emit(error instanceof Error ? error : new Error(String(error)));
				if (current !== null) {
					await current.close();
					current = null;
				}
				await this.sleep(delay);
				delay = Math.min(delay * 2, maxDelay);
			}
		}
	}

	private sleep(ms: number): Promise<void> {
		return new Promise((resolve) => {
			const timer = setTimeout(done, ms);
			function done(): void {
				clearTimeout(timer);
				resolve();
			}
			this.wake = done;
		});
	}
}
