/*
 * SessionSubScenes.h - the launcher's sub-scenes in a project file: the <z:subscenes> section
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

#ifndef LMMS_SESSION_SUB_SCENES_H
#define LMMS_SESSION_SUB_SCENES_H

#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L02 (WF-064): where a project FILE keeps the session model's sub-scenes (SessionModel::subScenes()) - one
 *  top-level <z:subscenes v="1"> section of <z:subscene name scene tracks="0,2"/> rows, written only when there is
 *  a sub-scene, so a project without one re-saves byte for byte.
 *
 *  WHY A SECTION OF ITS OWN, not rows inside <z:scenes>: a build before this one claims <z:scenes> only in the exact
 *  shape it writes and declines any other child - so a sub-scene row there would make it decline the whole grid and
 *  keep it as unclaimed text. As its own section, an older build loads the grid as before and keeps
 *  <z:subscenes> verbatim (Song::restoreNamedSection's unclaimed walk), re-saving it unchanged.
 *
 *  A section of a version this build does not know (v other than "1") is not read: it is kept as text and written
 *  back as it came, so a newer build's data is never downgraded. UI thread. */
class LMMS_EXPORT SessionSubScenes : public ProjectSection
{
public:
	static SessionSubScenes& instance();

	//! The section's element name, "z:subscenes".
	static QString nodeName();
	static constexpr int Version = 1;

	QString elementName() const override { return nodeName(); }
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { m_preserved.clear(); }

private:
	SessionSubScenes() = default;

	QString m_preserved;  //!< a section of an unknown version, as it came
};

} // namespace lmms

#endif // LMMS_SESSION_SUB_SCENES_H
