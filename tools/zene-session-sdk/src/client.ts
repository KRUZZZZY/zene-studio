// One connection to a running Zene instance's local control socket.

import { connect as netConnect, type Socket } from "node:net";

import { ZeneCommandError, ZeneConnectionError, ZeneError, ZeneProtocolError, ZeneTimeoutError } from "./errors.ts";
import { Listeners, rethrowLater, type Unlisten } from "./listeners.ts";
import {
	PROTOCOL_VERSION,
	type ArgsOf,
	type Cursor,
	type JsonObject,
	type ProjectChangedEvent,
	type ResultOf,
	type ServerEvent,
	type SubscribeResult,
	type UnsubscribeResult,
	type WireReply,
	type WireRequest,
} from "./protocol.ts";
import { Subscription, type SubscriptionHost } from "./subscription.ts";

/** The environment variable connect() reads when no path is given. */
export const SOCKET_PATH_ENV = "ZENE_CONTROL_SOCKET";

/** Largest reply line held while it arrives; the largest real one (control.commands_list) is well under 1 MiB. */
const MAX_LINE_BYTES = 64 * 1024 * 1024;
/** Event lines for a subscription whose reply has not been read yet (an event can overtake its reply). */
const MAX_EARLY_EVENTS = 4096;

export interface ConnectOptions {
	/**
	 * The path Zene was started with (`zene --control-socket <path>`): an AF_UNIX socket path on Linux and macOS,
	 * a `\\.\pipe\<name>` on Windows. Defaults to $ZENE_CONTROL_SOCKET.
	 */
	path?: string;
	/** Bound for the connection to open (default 10 000 ms). */
	connectTimeoutMs?: number;
	/** Default bound for each call (default 30 000 ms); a call can set its own. */
	callTimeoutMs?: number;
	/** Ask `control.version` on connect and refuse another protocol revision (default true). */
	checkProtocol?: boolean;
}

export interface CallOptions {
	timeoutMs?: number;
}

export interface SubscribeOptions {
	/** Changes the instance holds for this client before the subscription overflows (1..4096, default 256). */
	queueLimit?: number;
}

/**
 * The result of resync(): a live subscription and the state read AFTER it started, so no change falls between them.
 * Changes after `subscription.startSeq` arrive as events; some may already be visible in `state`.
 */
export interface ResyncResult<S> {
	subscription: Subscription;
	state: S;
	/** The position `state` is at least as new as. */
	cursor: Cursor;
	/**
	 * Changes made since `since` that were never delivered: 0 means nothing was missed. null when unknown: no `since`
	 * was given, or the instance is a different process (`sessionChanged`).
	 */
	missed: number | null;
	sessionChanged: boolean;
}

export interface ResyncOptions extends SubscribeOptions {
	/** The cursor of the subscription that ended (Subscription.cursor). */
	since?: Cursor;
}

interface Pending {
	command: string;
	resolve: (result: JsonObject) => void;
	reject: (error: Error) => void;
	timer: ReturnType<typeof setTimeout> | null;
}

export class ZeneClient implements SubscriptionHost {
	readonly path: string;
	private readonly socket: Socket;
	private readonly callTimeoutMs: number;
	private readonly pending = new Map<number, Pending>();
	private readonly subscriptions = new Map<string, Subscription>();
	private readonly earlyEvents: ServerEvent[] = [];
	private readonly closeListeners = new Listeners<[ZeneError | null]>(rethrowLater);
	private readonly chunks: Buffer[] = [];
	private chunkBytes = 0;
	private nextId = 1;
	private closedWith: ZeneError | null | undefined = undefined;
	private closing = false;
	private serverVersion: string | null = null;

	private constructor(path: string, socket: Socket, callTimeoutMs: number) {
		this.path = path;
		this.socket = socket;
		this.callTimeoutMs = callTimeoutMs;
		socket.on("data", (chunk: Buffer) => this.onData(chunk));
		socket.on("error", (error) => this.shutdown(new ZeneConnectionError(`connection to ${path} failed: ` +
			error.message, { cause: error })));
		socket.on("close", () => this.shutdown(this.closing ? null : new ZeneConnectionError(
			`the instance closed the connection to ${path}`)));
	}

	/** Opens a connection. Rejects with ZeneConnectionError when nothing listens, ZeneProtocolError on a revision mismatch. */
	static async connect(options: ConnectOptions = {}): Promise<ZeneClient> {
		const path = options.path ?? process.env[SOCKET_PATH_ENV];
		if (path === undefined || path === "") {
			throw new ZeneConnectionError(`no socket path: pass { path } or set ${SOCKET_PATH_ENV}`);
		}
		const socket = await openSocket(path, options.connectTimeoutMs ?? 10_000);
		const client = new ZeneClient(path, socket, options.callTimeoutMs ?? 30_000);
		if (options.checkProtocol ?? true) {
			try {
				const version = await client.call("control.version");
				if (version.proto !== PROTOCOL_VERSION) {
					throw new ZeneProtocolError(`the instance speaks control protocol ${version.proto}; ` +
						`this SDK speaks ${PROTOCOL_VERSION}`);
				}
				client.serverVersion = version.version;
			} catch (error) {
				await client.close();
				throw error;
			}
		}
		return client;
	}

	/** Zene's version string, when connect() checked the protocol. */
	get version(): string | null {
		return this.serverVersion;
	}

	get isOpen(): boolean {
		return this.closedWith === undefined;
	}

	/**
	 * Runs one registered command and resolves with its result. Edits go through the same commands, refusals and
	 * undo history as Zene's menus; each successful edit is one undo step.
	 * Rejects with ZeneCommandError (a typed refusal: nothing changed), ZeneTimeoutError or ZeneConnectionError.
	 */
	call<C extends string>(command: C, args?: ArgsOf<C>, options: CallOptions = {}): Promise<ResultOf<C>> {
		if (this.closedWith !== undefined) {
			return Promise.reject(this.closedWith ?? new ZeneConnectionError(`the connection to ${this.path} is closed`));
		}
		const id = this.nextId++;
		const request: WireRequest = { id, cmd: command, args: (args ?? {}) as JsonObject, proto: PROTOCOL_VERSION };
		const timeoutMs = options.timeoutMs ?? this.callTimeoutMs;
		return new Promise<ResultOf<C>>((resolve, reject) => {
			const entry: Pending = {
				command,
				resolve: resolve as (result: JsonObject) => void,
				reject,
				timer: null,
			};
			if (timeoutMs > 0 && Number.isFinite(timeoutMs)) {
				entry.timer = setTimeout(() => {
					this.pending.delete(id);
					reject(new ZeneTimeoutError(command, timeoutMs));
				}, timeoutMs);
			}
			this.pending.set(id, entry);
			this.socket.write(JSON.stringify(request) + "\n");
		});
	}

	/**
	 * Subscribes this connection to the instance's project-change events (one subscription per connection; a second
	 * is refused `busy`). Every change from any client, a script or the interface then arrives in `seq` order.
	 */
	async subscribe(options: SubscribeOptions = {}): Promise<Subscription> {
		const args = options.queueLimit === undefined ? {} : { queue_limit: options.queueLimit };
		const reply: SubscribeResult = await this.call("events.subscribe", args);
		const subscription = new Subscription(this, reply);
		this.subscriptions.set(subscription.id, subscription);
		subscription.onEnd(() => this.subscriptions.delete(subscription.id));
		this.replayEarlyEvents(subscription);
		return subscription;
	}

	/**
	 * The documented recovery after an overflow or a reconnect: subscribe FIRST, then read the state with `read`,
	 * so no change can fall between the read and the first event. Missed changes are never replayed; `missed` says
	 * how many there were.
	 */
	async resync<S>(read: (client: ZeneClient) => Promise<S>, options: ResyncOptions = {}): Promise<ResyncResult<S>> {
		const subscribeOptions: SubscribeOptions = options.queueLimit === undefined ? {} : { queueLimit: options.queueLimit };
		const subscription = await this.subscribe(subscribeOptions);
		let state: S;
		try {
			state = await read(this);
		} catch (error) {
			await subscription.unsubscribe().catch(() => null);
			throw error;
		}
		const since = options.since;
		const sessionChanged = since !== undefined && since.session !== subscription.session;
		const missed = since === undefined || sessionChanged ? null : Math.max(0, subscription.startSeq - since.seq);
		return {
			subscription,
			state,
			cursor: { session: subscription.session, seq: subscription.startSeq },
			missed,
			sessionChanged,
		};
	}

	/** Called once when the connection closes: with null after close(), with the error otherwise. */
	onClose(fn: (error: ZeneError | null) => void): Unlisten {
		if (this.closedWith !== undefined) {
			const error = this.closedWith;
			queueMicrotask(() => fn(error));
			return () => {};
		}
		return this.closeListeners.add(fn);
	}

	/** Closes the connection. A live subscription ends with reason `disconnected`; the instance frees it. */
	close(): Promise<void> {
		if (this.closedWith !== undefined) {
			return Promise.resolve();
		}
		this.closing = true;
		return new Promise((resolve) => {
			this.socket.once("close", () => resolve());
			this.socket.end();
			// A peer that never acknowledges the end must not hold close() open.
			setTimeout(() => this.socket.destroy(), 2000).unref();
		});
	}

	/** @internal SubscriptionHost. */
	sendUnsubscribe(subscription: string): Promise<UnsubscribeResult> {
		return this.call("events.unsubscribe", { subscription });
	}

	private onData(chunk: Buffer): void {
		let start = 0;
		let newline = chunk.indexOf(0x0a, start);
		while (newline !== -1) {
			this.chunks.push(chunk.subarray(start, newline));
			const line = Buffer.concat(this.chunks).toString("utf8");
			this.chunks.length = 0;
			this.chunkBytes = 0;
			this.onLine(line);
			if (this.closedWith !== undefined) {
				return;
			}
			start = newline + 1;
			newline = chunk.indexOf(0x0a, start);
		}
		if (start < chunk.length) {
			this.chunks.push(chunk.subarray(start));
			this.chunkBytes += chunk.length - start;
			if (this.chunkBytes > MAX_LINE_BYTES) {
				this.fail(new ZeneProtocolError(`a line from the instance exceeds ${MAX_LINE_BYTES} bytes`));
			}
		}
	}

	private onLine(line: string): void {
		if (line.trim() === "") {
			return;
		}
		let message: unknown;
		try {
			message = JSON.parse(line);
		} catch {
			this.fail(new ZeneProtocolError(`the instance sent a line that is not JSON: ${line.slice(0, 200)}`));
			return;
		}
		if (typeof message !== "object" || message === null || Array.isArray(message)) {
			this.fail(new ZeneProtocolError(`the instance sent a line that is not an object: ${line.slice(0, 200)}`));
			return;
		}
		const record = message as Record<string, unknown>;
		if (typeof record.event === "string" && !("id" in record)) {
			this.onEvent(record as unknown as ServerEvent);
		} else if (typeof record.id === "number") {
			this.onReply(record as unknown as WireReply);
		}
		// A reply whose id is not a number (a malformed request's refusal) belongs to no call of ours.
	}

	private onReply(reply: WireReply): void {
		const entry = this.pending.get(reply.id);
		if (entry === undefined) {
			return; // its call timed out already
		}
		this.pending.delete(reply.id);
		if (entry.timer !== null) {
			clearTimeout(entry.timer);
		}
		if (reply.ok) {
			entry.resolve(reply.result ?? {});
		} else {
			const error = reply.error ?? { kind: "unknown", message: "no error detail" };
			entry.reject(new ZeneCommandError(entry.command, reply.id, error.kind, error.message));
		}
	}

	private onEvent(event: ServerEvent): void {
		const subscription = this.subscriptions.get(event.subscription);
		if (subscription === undefined) {
			// Its subscribe reply is still on its way; anything else (an ended subscription's straggler) is dropped there.
			if (this.earlyEvents.length < MAX_EARLY_EVENTS) {
				this.earlyEvents.push(event);
			}
			return;
		}
		this.route(subscription, event);
	}

	private route(subscription: Subscription, event: ServerEvent): void {
		if (event.event === "events_overflow") {
			subscription.overflowed(event);
		} else if (event.event === "project_changed") {
			subscription.deliver(event as ProjectChangedEvent);
		}
	}

	private replayEarlyEvents(subscription: Subscription): void {
		const held = this.earlyEvents.splice(0);
		for (const event of held) {
			if (event.subscription === subscription.id && event.seq > subscription.startSeq) {
				this.route(subscription, event);
			}
		}
	}

	private fail(error: ZeneError): void {
		this.shutdown(error);
		this.socket.destroy();
	}

	private shutdown(error: ZeneError | null): void {
		if (this.closedWith !== undefined) {
			return;
		}
		this.closedWith = error;
		const reason = error ?? new ZeneConnectionError(`the connection to ${this.path} was closed`);
		for (const [id, entry] of this.pending) {
			this.pending.delete(id);
			if (entry.timer !== null) {
				clearTimeout(entry.timer);
			}
			entry.reject(reason);
		}
		for (const subscription of [...this.subscriptions.values()]) {
			subscription.finish({ reason: "disconnected", lastSeq: subscription.lastSeq, error: reason });
		}
		this.subscriptions.clear();
		this.earlyEvents.length = 0;
		this.closeListeners.emit(error);
		this.closeListeners.clear();
	}
}

function openSocket(path: string, timeoutMs: number): Promise<Socket> {
	return new Promise((resolve, reject) => {
		const socket = netConnect({ path });
		const timer = setTimeout(() => {
			socket.destroy();
			reject(new ZeneConnectionError(`no connection to ${path} within ${timeoutMs} ms`));
		}, timeoutMs);
		socket.once("connect", () => {
			clearTimeout(timer);
			socket.removeAllListeners("error");
			resolve(socket);
		});
		socket.once("error", (error) => {
			clearTimeout(timer);
			reject(new ZeneConnectionError(`cannot connect to ${path}: ${error.message}`, { cause: error }));
		});
	});
}
