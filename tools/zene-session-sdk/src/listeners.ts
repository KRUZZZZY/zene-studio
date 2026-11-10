// A typed listener set. A throwing listener does not stop the others; its error goes to `onListenerError`.

export type Unlisten = () => void;

export class Listeners<A extends unknown[]> {
	private readonly fns = new Set<(...args: A) => void>();
	private readonly onListenerError: (error: unknown) => void;

	constructor(onListenerError: (error: unknown) => void) {
		this.onListenerError = onListenerError;
	}

	add(fn: (...args: A) => void): Unlisten {
		this.fns.add(fn);
		return () => {
			this.fns.delete(fn);
		};
	}

	emit(...args: A): void {
		for (const fn of [...this.fns]) {
			try {
				fn(...args);
			} catch (error) {
				this.onListenerError(error);
			}
		}
	}

	clear(): void {
		this.fns.clear();
	}
}

/**
 * A listener set that HOLDS what is pushed until its first listener attaches, then hands over everything held,
 * in order, before anything newer. Changes can arrive before a caller had any chance to listen (in the same read as
 * the subscribe reply, or while resync() rereads the state); without this they would reach nobody while the
 * cursor already counted them.
 */
export class HeldListeners<T> {
	private readonly listeners: Listeners<[T]>;
	private held: T[] | null = [];
	private flushQueued = false;
	private readonly afterFlush: (() => void)[] = [];
	private readonly limit: number;
	private readonly onHoldOverflow: () => void;

	/** Past `limit` held values nobody listens: holding stops, the held values are dropped, `onHoldOverflow` runs. */
	constructor(limit: number, onHoldOverflow: () => void) {
		this.listeners = new Listeners<[T]>(rethrowLater);
		this.limit = limit;
		this.onHoldOverflow = onHoldOverflow;
	}

	add(fn: (value: T) => void): Unlisten {
		const unlisten = this.listeners.add(fn);
		if (this.held !== null && !this.flushQueued) {
			// Not inside add(): the caller finishes setting up (more listeners, its own state) before values arrive.
			this.flushQueued = true;
			queueMicrotask(() => this.flush());
		}
		return unlisten;
	}

	push(value: T): void {
		if (this.held === null) {
			this.listeners.emit(value);
		} else if (this.held.length >= this.limit) {
			this.held = null;
			this.onHoldOverflow();
		} else {
			this.held.push(value);
		}
	}

	/** Runs `fn` once every held value has reached the listeners (a microtask later when nothing waits). */
	afterHeld(fn: () => void): void {
		if (this.flushQueued && this.held !== null) {
			this.afterFlush.push(fn);
		} else {
			queueMicrotask(fn);
		}
	}

	private flush(): void {
		const held = this.held ?? [];
		this.held = null;
		for (const value of held) {
			this.listeners.emit(value);
		}
		for (const fn of this.afterFlush.splice(0)) {
			fn();
		}
	}
}

/** Re-throws a listener's error outside the SDK's stack, where the host's uncaught-error handling sees it. */
export function rethrowLater(error: unknown): void {
	queueMicrotask(() => {
		throw error;
	});
}
