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

/** Re-throws a listener's error outside the SDK's stack, where the host's uncaught-error handling sees it. */
export function rethrowLater(error: unknown): void {
	queueMicrotask(() => {
		throw error;
	});
}
