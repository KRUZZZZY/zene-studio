/*
 * ProjectWorkspace.h - the workflows and the window layout a project keeps of its own
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

#ifndef LMMS_PROJECT_WORKSPACE_H
#define LMMS_PROJECT_WORKSPACE_H

#include <QJsonObject>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 (design themes 19 and 22): what a project keeps of the user's workspace, beside the
 *  personal copies in the config file -
 *  - its own workflows (workflow.*), used instead of the shared ones when the user chose to keep
 *    workflows with each project (ui/workflows.scope);
 *  - the window layout it remembers (window.layout_project_*), put back when the project opens.
 *  Each part is one JSON object; saved as <workspace> only when a part holds something (a project
 *  that never used it re-saves byte for byte; a project without the element loads with neither).
 *  UI thread. */
class LMMS_EXPORT ProjectWorkspace : public ProjectSection
{
public:
	enum class Part
	{
		Workflows,
		Layout
	};

	static ProjectWorkspace& instance();

	const QJsonObject& part(Part part) const { return part == Part::Workflows ? m_workflows : m_layout; }
	void setPart(Part part, const QJsonObject& value);

	QString elementName() const override;
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	ProjectWorkspace() = default;
	QJsonObject m_workflows;
	QJsonObject m_layout;
};

} // namespace lmms

#endif // LMMS_PROJECT_WORKSPACE_H
