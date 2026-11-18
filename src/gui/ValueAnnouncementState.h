/*
 * ValueAnnouncementState.h - the decision to defer or announce a changed accessible value
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

#ifndef LMMS_GUI_VALUE_ANNOUNCEMENT_STATE_H
#define LMMS_GUI_VALUE_ANNOUNCEMENT_STATE_H

#include <QString>

namespace lmms::a11y::detail
{

enum class ValueChangeAction
{
	NoChange,
	Defer,
	Announce
};

inline ValueChangeAction valueChangeAction(const QString& value, const QString& said, bool settling, bool& pending)
{
	if (value == said)
	{
		pending = false;
		return ValueChangeAction::NoChange;
	}
	if (settling)
	{
		pending = true;
		return ValueChangeAction::Defer;
	}
	return ValueChangeAction::Announce;
}

} // namespace lmms::a11y::detail

#endif // LMMS_GUI_VALUE_ANNOUNCEMENT_STATE_H
