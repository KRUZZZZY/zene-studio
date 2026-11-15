// The wire contract of Zene's local control socket, as TypeScript types.
//
// One JSON object per line. A request carries an `id`; its reply carries the same `id` and either `ok:true` with
// a `result` or `ok:false` with a typed `error`. A connection that called `events.subscribe` also receives
// unsolicited lines that carry `event` and no `id` (see docs/050-features/Q050-334.1.md in the Zene tree).

/** The control protocol revision this SDK speaks (`control.version` reports the instance's). */
export const PROTOCOL_VERSION = 1;

export type JsonValue = string | number | boolean | null | JsonValue[] | JsonObject;
export interface JsonObject {
	[key: string]: JsonValue;
}

/** The closed set of typed refusals a command answers with. */
export type ErrorKind = "not_found" | "requires" | "invalid_args" | "busy" | "refused" | "irreversible";

export interface WireRequest {
	id: number;
	cmd: string;
	args: JsonObject;
	proto: number;
}

export interface WireSuccess {
	id: number;
	ok: true;
	result: JsonObject;
}

export interface WireFailure {
	id: number;
	ok: false;
	error: { kind: string; message: string };
}

export type WireReply = WireSuccess | WireFailure;

/** What made a change: a registered command, or a project-history step made outside any command (the interface). */
export type ChangeSource = "command" | "journal";
export type ChangeKind = "edit" | "undo" | "redo" | "project_replaced";

/** One change to the open project. `seq` rises by exactly one per change within one `session`. */
export interface ProjectChangedEvent {
	event: "project_changed";
	subscription: string;
	session: string;
	seq: number;
	source: ChangeSource;
	kind: ChangeKind;
	/** The command that made the change; absent when the interface made it directly. */
	command?: string;
	/** The command's arguments, so a client can tell what changed. */
	args?: JsonObject;
	/** Set instead of `args` when the arguments were too large for one event line. */
	args_omitted?: boolean;
}

/** The last line a subscription that fell behind receives; the subscription is over after it. */
export interface EventsOverflowNotice {
	event: "events_overflow";
	subscription: string;
	session: string;
	last_delivered_seq: number;
	first_missed_seq: number;
	missed: number;
	/** The instance's current position when the notice was written. */
	seq: number;
	message: string;
}

export type ServerEvent = ProjectChangedEvent | EventsOverflowNotice;

/** A position in one instance's change stream. */
export interface Cursor {
	session: string;
	seq: number;
}

export type EmptyArgs = Record<string, never>;

export interface PingResult {
	pong: boolean;
	engine_ready?: boolean;
	proto?: number;
	version?: string;
}

export interface VersionResult {
	proto: number;
	version: string;
}

export interface ProjectState {
	file?: string;
	modified?: boolean;
	tempo?: number;
	playing?: boolean;
	track_count?: number;
	/** Track ids, in project order. */
	tracks?: string[];
	missing_device_count?: number;
}

export type TrackType = "instrument" | "pattern" | "sample" | "automation" | "folder" | "neutral";

export interface TrackAddArgs {
	type?: TrackType;
	name?: string;
	instrument?: string;
	output?: string;
}

export interface TrackAddResult {
	track: string;
	name?: string;
	index?: number;
	type?: string;
}

export interface TrackRenameArgs {
	track: string;
	name: string;
}

export interface TrackRenameResult {
	track?: string;
	name?: string;
}

export interface UndoResult {
	undone?: boolean;
	can_undo?: boolean;
	can_redo?: boolean;
	undone_command?: string;
}

export interface RedoResult {
	redone?: boolean;
	can_undo?: boolean;
	can_redo?: boolean;
}

export interface SubscribeArgs {
	/** Events the instance holds for this subscriber before it overflows (default 256, at most 4096). */
	queue_limit?: number;
}

export interface SubscribeResult {
	subscription: string;
	session: string;
	/** The current position: the next change is `seq + 1`. */
	seq: number;
	queue_limit: number;
	queue_byte_limit: number;
}

export interface UnsubscribeResult {
	unsubscribed: boolean;
	subscription: string;
	last_delivered_seq: number;
	/** Queued events dropped unsent. */
	discarded: number;
}

export interface SubscriptionRow {
	subscription: string;
	queued: number;
	queued_bytes: number;
	queue_limit: number;
	overflowed: boolean;
	delivered: number;
	last_delivered_seq: number;
}

export interface EventsState {
	session: string;
	seq: number;
	subscriptions: SubscriptionRow[];
	overflows: number;
	disconnects: number;
	default_queue_limit: number;
	max_queue_limit: number;
	queue_byte_limit: number;
}

export interface CommandInfo {
	id: string;
	group?: string;
	verb?: string;
	description?: string;
	mutating?: boolean;
	args_schema?: JsonObject;
	result_schema?: JsonObject;
}

export interface CommandsListResult {
	commands: CommandInfo[];
	count?: number;
}

/**
 * Commands this SDK knows the shape of. Every other registered command is callable too, with `JsonObject`
 * arguments and result (`control.commands_list` describes them all).
 */
export interface KnownCommands {
	"control.ping": { args: EmptyArgs; result: PingResult };
	"control.version": { args: EmptyArgs; result: VersionResult };
	"control.commands_list": { args: EmptyArgs; result: CommandsListResult };
	"control.undo": { args: EmptyArgs; result: UndoResult };
	"control.redo": { args: EmptyArgs; result: RedoResult };
	"project.get_state": { args: EmptyArgs; result: ProjectState };
	"track.add": { args: TrackAddArgs; result: TrackAddResult };
	"track.rename": { args: TrackRenameArgs; result: TrackRenameResult };
	"events.subscribe": { args: SubscribeArgs; result: SubscribeResult };
	"events.unsubscribe": { args: { subscription: string }; result: UnsubscribeResult };
	"events.get_state": { args: EmptyArgs; result: EventsState };
}

export type CommandId = keyof KnownCommands;
export type ArgsOf<C extends string> = C extends CommandId ? KnownCommands[C]["args"] : JsonObject;
export type ResultOf<C extends string> = C extends CommandId ? KnownCommands[C]["result"] : JsonObject;
