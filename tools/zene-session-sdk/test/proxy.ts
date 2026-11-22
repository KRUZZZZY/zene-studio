// A local socket proxy between the SDK and a real Zene instance, so the test can make the two situations recovery
// is for: a connection that drops, and a client that stops reading (its socket buffer fills, the instance holds the
// rest). It only forwards bytes; it never reads or changes the protocol. AF_UNIX on POSIX, loopback-free.

import { createServer, connect, type Server, type Socket } from "node:net";

interface Pair {
	client: Socket;
	upstream: Socket;
}

export class Proxy {
	readonly path: string;
	private readonly target: string;
	private readonly server: Server;
	private readonly pairs = new Set<Pair>();
	private down = false;
	private stalled = false;

	private constructor(path: string, target: string) {
		this.path = path;
		this.target = target;
		this.server = createServer((client) => this.accept(client));
	}

	static async listen(path: string, target: string): Promise<Proxy> {
		const proxy = new Proxy(path, target);
		await new Promise<void>((resolve, reject) => {
			proxy.server.once("error", reject);
			proxy.server.listen(path, () => resolve());
		});
		return proxy;
	}

	get connections(): number {
		return this.pairs.size;
	}

	/** Drops every connection and refuses new ones (accepted, then closed at once) until up(). */
	cut(): void {
		this.down = true;
		for (const pair of this.pairs) {
			pair.client.destroy();
			pair.upstream.destroy();
		}
		this.pairs.clear();
	}

	up(): void {
		this.down = false;
	}

	/** Stops reading from the instance: what it writes piles up in the socket and then in the instance's queue. */
	stall(): void {
		this.stalled = true;
		for (const pair of this.pairs) {
			// Unpiped first: a 'drain' on the client side would otherwise let pipe() resume the flow.
			pair.upstream.unpipe(pair.client);
			pair.upstream.pause();
		}
	}

	resume(): void {
		this.stalled = false;
		for (const pair of this.pairs) {
			pair.upstream.pipe(pair.client);
		}
	}

	close(): Promise<void> {
		this.cut();
		return new Promise((resolve) => this.server.close(() => resolve()));
	}

	private accept(client: Socket): void {
		if (this.down) {
			client.destroy();
			return;
		}
		const upstream = connect({ path: this.target });
		const pair: Pair = { client, upstream };
		this.pairs.add(pair);
		const drop = (): void => {
			client.destroy();
			upstream.destroy();
			this.pairs.delete(pair);
		};
		client.on("error", drop);
		upstream.on("error", drop);
		client.on("close", drop);
		upstream.on("close", drop);
		// pipe() honours back-pressure both ways; an unpiped, paused upstream really stops reading from the instance.
		client.pipe(upstream);
		if (!this.stalled) {
			upstream.pipe(client);
		}
	}
}
