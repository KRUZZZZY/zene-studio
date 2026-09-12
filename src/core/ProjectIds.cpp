/*
 * ProjectIds.cpp - the project-scoped monotonic id counter.
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
 */

#include "ProjectIds.h"

namespace lmms
{

namespace
{

// The one piece of state. begin = 0 so the first object of a new document is
// <prefix>-0, which is what the index-derived surface already reported for the
// first object and therefore the smallest surprise for a legacy file's first
// save (the spec's example shows a first track with id 3; the value is arbitrary
// as long as it is creation-assigned and monotonic).
int s_next = 0;
int s_loadAssignments = 0;

} // namespace

int ProjectIds::next()
{
	return s_next;
}

void ProjectIds::observeNext(int next)
{
	if (next > s_next) { s_next = next; }
}

void ProjectIds::observe(int id)
{
	if (id >= s_next) { s_next = id + 1; }
}

int ProjectIds::allocate()
{
	const int id = s_next;
	observe(id);
	return id;
}

void ProjectIds::reset()
{
	s_next = 0;
	s_loadAssignments = 0;
}

int ProjectIds::loadAssignments()
{
	return s_loadAssignments;
}

void ProjectIds::beginLoad()
{
	s_loadAssignments = 0;
}

void ProjectIds::noteLoadAssignment()
{
	++s_loadAssignments;
}

} // namespace lmms
