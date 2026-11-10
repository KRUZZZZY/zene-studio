// Every failure the SDK reports is a ZeneError; `code` tells the four kinds apart without instanceof.

import type { ErrorKind } from "./protocol.ts";

export type ZeneErrorCode = "command" | "connection" | "timeout" | "protocol";

export class ZeneError extends Error {
	readonly code: ZeneErrorCode;

	constructor(code: ZeneErrorCode, message: string, options?: { cause?: unknown }) {
		super(message, options);
		this.name = "ZeneError";
		this.code = code;
	}
}

/** The instance answered the command with a typed refusal. Nothing changed. */
export class ZeneCommandError extends ZeneError {
	/** One of the protocol's closed set; kept as a string so a newer instance's kind is not lost. */
	readonly kind: ErrorKind | (string & {});
	readonly command: string;
	readonly requestId: number;

	constructor(command: string, requestId: number, kind: string, message: string) {
		super("command", `${command}: ${kind}: ${message}`);
		this.name = "ZeneCommandError";
		this.kind = kind;
		this.command = command;
		this.requestId = requestId;
	}
}

/** The connection could not be made, or it closed. Calls still waiting are rejected with this. */
export class ZeneConnectionError extends ZeneError {
	constructor(message: string, options?: { cause?: unknown }) {
		super("connection", message, options);
		this.name = "ZeneConnectionError";
	}
}

/**
 * No reply inside the call's bound. The connection stays open, and the command may still have run: reread
 * before retrying an edit.
 */
export class ZeneTimeoutError extends ZeneError {
	readonly command: string;

	constructor(command: string, timeoutMs: number) {
		super("timeout", `${command}: no reply within ${timeoutMs} ms`);
		this.name = "ZeneTimeoutError";
		this.command = command;
	}
}

/** The instance sent something this SDK cannot read, or speaks another protocol revision. */
export class ZeneProtocolError extends ZeneError {
	constructor(message: string) {
		super("protocol", message);
		this.name = "ZeneProtocolError";
	}
}
