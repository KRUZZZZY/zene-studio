// One events.subscribe on one connection: ordered project changes until it ends.

import { ZeneError } from "./errors.ts";
import { HeldListeners, Listeners, rethrowLater, type Unlisten } from "./listeners.ts";
import type { Cursor, EventsOverflowNotice, ProjectChangedEvent, SubscribeResult, UnsubscribeResult } from "./protocol.ts";

/**
 * Why a subscription ended.
 * - `unsubscribed`: you called unsubscribe(); nothing was missed.
 * - `overflow`: this client fell behind; `notice` names the changes that never arrived. Resync.
 * - `disconnected`: the connection closed; anything after `lastSeq` may be missed. Reconnect and resync.
 * - `gap`: an event arrived out of sequence (a defect: the instance promises none). Resync.
 */
export type SubscriptionEndReason = "unsubscribed" | "overflow" | "disconnected" | "gap";

export interface SubscriptionEnd {
	reason: SubscriptionEndReason;
	/** The last change this subscription delivered (its start position if none). */
	lastSeq: number;
	notice?: EventsOverflowNotice;
	error?: Error;
}

/** What a Subscription needs from its connection. */
export interface SubscriptionHost {
	sendUnsubscribe(subscription: string): Promise<UnsubscribeResult>;
}

/** Changes held for a subscription nobody listens to yet before it ends with reason `overflow`. */
export const MAX_HELD_EVENTS = 8192;

export interface EventIteratorOptions {
	/** Events held for a slow `for await` loop before the iterator fails (default 4096). */
	bufferLimit?: number;
}

export class Subscription {
	readonly id: string;
	readonly session: string;
	/** The instance's position when the subscription started: its first event is `startSeq + 1`. */
	readonly startSeq: number;
	readonly queueLimit: number;
	readonly queueByteLimit: number;
	/** Resolves (never rejects) once the subscription is over. */
	readonly ended: Promise<SubscriptionEnd>;

	private last: number;
	private endState: SubscriptionEnd | null = null;
	private resolveEnded!: (end: SubscriptionEnd) => void;
	private readonly host: SubscriptionHost;
	private readonly eventListeners: HeldListeners<ProjectChangedEvent>;
	private readonly endListeners = new Listeners<[SubscriptionEnd]>(rethrowLater);

	constructor(host: SubscriptionHost, reply: SubscribeResult) {
		this.host = host;
		this.id = reply.subscription;
		this.session = reply.session;
		this.startSeq = reply.seq;
		this.last = reply.seq;
		this.queueLimit = reply.queue_limit;
		this.queueByteLimit = reply.queue_byte_limit;
		this.eventListeners = new HeldListeners<ProjectChangedEvent>(MAX_HELD_EVENTS, () => this.finish({
			reason: "overflow",
			lastSeq: this.last,
			error: new ZeneError("protocol", `${MAX_HELD_EVENTS} changes arrived and no onEvent listener took them`),
		}));
		this.ended = new Promise((resolve) => {
			this.resolveEnded = resolve;
		});
	}

	/** The last change this client received; it reached the listeners, or is held for the first one. */
	get lastSeq(): number {
		return this.last;
	}

	/** Where this subscription has read up to; give it to resync() as `since` after it ends. */
	get cursor(): Cursor {
		return { session: this.session, seq: this.last };
	}

	get isActive(): boolean {
		return this.endState === null;
	}

	/** How it ended, or null while it is live. */
	get end(): SubscriptionEnd | null {
		return this.endState;
	}

	/**
	 * Called for each change, in `seq` order. Changes that arrived before the first listener (with the subscribe
	 * reply, or while resync() was reading) are held and reach it first, a microtask after it attaches: none is lost.
	 */
	onEvent(fn: (event: ProjectChangedEvent) => void): Unlisten {
		return this.eventListeners.add(fn);
	}

	/** Called once when the subscription ends; if it already has, after any held change reached onEvent. */
	onEnd(fn: (end: SubscriptionEnd) => void): Unlisten {
		if (this.endState !== null) {
			const end = this.endState;
			this.eventListeners.afterHeld(() => fn(end));
			return () => {};
		}
		return this.endListeners.add(fn);
	}

	/**
	 * Ends the subscription on the instance. Events already on their way arrive before this resolves; none after.
	 * Resolves without asking the instance when the subscription has already ended.
	 */
	async unsubscribe(): Promise<UnsubscribeResult | null> {
		if (this.endState !== null) {
			return null;
		}
		const result = await this.host.sendUnsubscribe(this.id);
		this.finish({ reason: "unsubscribed", lastSeq: this.last });
		return result;
	}

	/** The changes as an async iterator; the loop ends when the subscription does. */
	events(options: EventIteratorOptions = {}): AsyncIterableIterator<ProjectChangedEvent> {
		return new EventIterator(this, options.bufferLimit ?? 4096);
	}

	/** @internal The connection routes this subscription's lines here. */
	deliver(event: ProjectChangedEvent): void {
		if (this.endState !== null) {
			return;
		}
		if (event.session !== this.session || event.seq !== this.last + 1) {
			this.finish({
				reason: "gap",
				lastSeq: this.last,
				error: new ZeneError("protocol", `expected seq ${this.last + 1} in session ${this.session}, ` +
					`got seq ${event.seq} in session ${event.session}`),
			});
			return;
		}
		this.last = event.seq;
		this.eventListeners.push(event);
	}

	/** @internal */
	overflowed(notice: EventsOverflowNotice): void {
		this.finish({ reason: "overflow", lastSeq: this.last, notice });
	}

	/** @internal */
	finish(end: SubscriptionEnd): void {
		if (this.endState !== null) {
			return;
		}
		this.endState = end;
		// Event listeners stay: changes held before the end still reach the first one (deliver() takes no more).
		this.endListeners.emit(end);
		this.endListeners.clear();
		this.resolveEnded(end);
	}
}

class EventIterator implements AsyncIterableIterator<ProjectChangedEvent> {
	private readonly buffer: ProjectChangedEvent[] = [];
	private waiter: ((result: IteratorResult<ProjectChangedEvent>) => void) | null = null;
	private failWaiter: ((error: Error) => void) | null = null;
	private failure: Error | null = null;
	private done = false;
	private readonly unlisten: Unlisten[] = [];

	constructor(subscription: Subscription, bufferLimit: number) {
		this.unlisten.push(subscription.onEvent((event) => {
			if (this.waiter !== null) {
				const resolve = this.waiter;
				this.clearWaiter();
				resolve({ value: event, done: false });
			} else if (this.buffer.length >= bufferLimit) {
				this.fail(new ZeneError("protocol", `the event iterator's buffer of ${bufferLimit} is full: ` +
					"the loop reads slower than the project changes"));
			} else {
				this.buffer.push(event);
			}
		}));
		this.unlisten.push(subscription.onEnd(() => this.close()));
	}

	[Symbol.asyncIterator](): AsyncIterableIterator<ProjectChangedEvent> {
		return this;
	}

	next(): Promise<IteratorResult<ProjectChangedEvent>> {
		const event = this.buffer.shift();
		if (event !== undefined) {
			return Promise.resolve({ value: event, done: false });
		}
		if (this.failure !== null) {
			return Promise.reject(this.failure);
		}
		if (this.done) {
			return Promise.resolve({ value: undefined, done: true });
		}
		return new Promise((resolve, reject) => {
			this.waiter = resolve;
			this.failWaiter = reject;
		});
	}

	return(): Promise<IteratorResult<ProjectChangedEvent>> {
		this.close();
		this.buffer.length = 0;
		return Promise.resolve({ value: undefined, done: true });
	}

	private clearWaiter(): void {
		this.waiter = null;
		this.failWaiter = null;
	}

	private fail(error: Error): void {
		this.failure = error;
		const reject = this.failWaiter;
		this.clearWaiter();
		this.detach();
		reject?.(error);
	}

	private close(): void {
		this.done = true;
		const resolve = this.waiter;
		this.clearWaiter();
		this.detach();
		resolve?.({ value: undefined, done: true });
	}

	private detach(): void {
		for (const unlisten of this.unlisten.splice(0)) {
			unlisten();
		}
	}
}
