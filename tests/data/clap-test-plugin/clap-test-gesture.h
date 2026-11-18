/*
 * clap-test-gesture.h - an editor gesture the CLAP test plug-ins report, scripted by the test (Q050-214)
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef CLAP_TEST_GESTURE_H
#define CLAP_TEST_GESTURE_H

#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

static void clap_test_push_mark(const clap_output_events_t* out, clap_id param, uint16_t type)
{
	clap_event_param_gesture_t gesture;
	memset(&gesture, 0, sizeof(gesture));
	gesture.header.size = sizeof(gesture);
	gesture.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
	gesture.header.type = type;
	gesture.param_id = param;
	out->try_push(out, &gesture.header);
}

static void clap_test_push_value(const clap_output_events_t* out, clap_id param, double value)
{
	clap_event_param_value_t event;
	memset(&event, 0, sizeof(event));
	event.header.size = sizeof(event);
	event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
	event.header.type = CLAP_EVENT_PARAM_VALUE;
	event.param_id = param;
	event.note_id = -1;
	event.port_index = -1;
	event.channel = -1;
	event.key = -1;
	event.value = value;
	out->try_push(out, &event.header);
}

/* CLAP_TEST_GESTURE="<sequence> <token>...": a block that sees a new sequence reports the tokens through its output
   events, in order: b (gesture begin), e (gesture end), or a number (the parameter's new value, written to *value too,
   as a plug-in's own edit changes it). This is what a plug-in does when its editor's knob is dragged. */
static void clap_test_report_gesture(const clap_output_events_t* out, clap_id param, double* value, long* seen)
{
	const char* script = getenv("CLAP_TEST_GESTURE");
	char* cursor = NULL;
	long sequence = 0;
	if (script == NULL || out == NULL) { return; }
	sequence = strtol(script, &cursor, 10);
	if (sequence == *seen) { return; }
	*seen = sequence;
	for (char* next = cursor;; cursor = next)
	{
		cursor += strspn(cursor, " ");
		if (*cursor == 'b' || *cursor == 'e')
		{
			clap_test_push_mark(out, param, *cursor == 'b' ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END);
			next = cursor + 1;
			continue;
		}
		if (*cursor == 0) { return; }
		const double parsed = strtod(cursor, &next);
		if (next == cursor) { return; }
		*value = parsed;
		clap_test_push_value(out, param, parsed);
	}
}

#endif /* CLAP_TEST_GESTURE_H */
