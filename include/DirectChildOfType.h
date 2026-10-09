/*
 * DirectChildOfType.h - find a named direct child by C++ type, without Q_OBJECT
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

#ifndef LMMS_DIRECT_CHILD_OF_TYPE_H
#define LMMS_DIRECT_CHILD_OF_TYPE_H

#include <QObject>
#include <QString>

namespace lmms
{

//! The direct child of \a parent named \a name whose type is \a T, or null. QObject::findChild<T*>() needs T
//! to carry Q_OBJECT on Qt 6.8 (a static_assert; the hosted msvc-x64 job builds Qt 6.8.3), and file-local
//! helper objects - the accessibility layer's per-window announcers - have none. This walks the same direct
//! children with dynamic_cast, which needs only RTTI, on every Qt version.
template <class T>
T* directChildOfType(const QObject* parent, const QString& name)
{
	if (parent == nullptr) { return nullptr; }
	for (QObject* child : parent->children())
	{
		auto* typed = dynamic_cast<T*>(child);
		if (typed != nullptr && child->objectName() == name) { return typed; }
	}
	return nullptr;
}

} // namespace lmms

#endif // LMMS_DIRECT_CHILD_OF_TYPE_H
