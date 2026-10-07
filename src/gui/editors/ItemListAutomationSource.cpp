/*
 * ItemListAutomationSource.cpp - the automation editor's editable item list (ED-194)
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


/*! ED-194: window.item_list's "automation" surface - the open automation editor's points as a table: where each sits
 *  (song time) and its value as a share of the control's range, beside the value as stored. A cell edit moves the
 *  point through AutomationPointEdit.h (automation.move_point where the control has an address), one undo step; a
 *  move onto another point is refused and said on the list's status line. The current row follows the point the
 *  editor's keys edit, both ways. Points are added and removed on the canvas, not here. */

#include <algorithm>
#include <cmath>
#include <iterator>

#include <QPointer>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationPointEdit.h"
#include "ItemList.h"
#include "Song.h"

namespace lmms::gui::itemlists
{

namespace
{

class AutomationSource : public ItemListSource
{
public:
	AutomationSource(AutomationEditor* editor, QObject* parent)
		: ItemListSource(parent)
		, m_editor(editor)
	{
		connect(editor, &AutomationEditor::currentClipChanged, this, [this]() { watchClip(); emit changed(); });
		connect(editor, &AutomationEditor::keyPointChanged, this, &ItemListSource::changed);
		watchClip();
	}

	QString title() const override { return tr("Automation points"); }

	QList<ItemColumn> columns() const override
	{
		return {{QStringLiteral("position"), tr("Position"), ItemColumn::Kind::Position, 0, static_cast<int>(MaxSongLength), true},
			{QStringLiteral("value"), tr("Value"), ItemColumn::Kind::Percent, 0, 100, true},
			{QStringLiteral("stored"), tr("Stored value"), ItemColumn::Kind::Text, 0, 0, false}};
	}

	int rowCount() const override { return clip() != nullptr ? static_cast<int>(clip()->getTimeMap().size()) : 0; }
	QString rowId(int row) const override { return QStringLiteral("point-%1").arg(tickOf(row)); }

	int value(int row, int column) const override
	{
		if (column == 0) { return origin() + tickOf(row); }
		automationedit::Range range;
		if (column != 1 || !automationedit::rangeOf(clip(), &range) || range.maximum <= range.minimum) { return 0; }
		return static_cast<int>(std::lround((storedAt(row) - range.minimum) * 100.f / (range.maximum - range.minimum)));
	}

	QString text(int row, int column) const override
	{
		return column == 2 ? QString::number(static_cast<double>(storedAt(row))) : ItemListSource::text(row, column);
	}

	bool setValue(int row, int column, int value) override
	{
		if (row < 0 || row >= rowCount()) { return false; }
		const int tick = tickOf(row);
		int toTick = tick;
		float toValue = storedAt(row);
		automationedit::Range range;
		if (column == 0) { toTick = value - origin(); }
		else if (column == 1 && automationedit::rangeOf(clip(), &range))
		{
			toValue = range.minimum + (range.maximum - range.minimum) * static_cast<float>(std::clamp(value, 0, 100)) / 100.f;
		}
		QString error;
		if (!automationedit::movePoint(m_editor->editedClip(), tick, toTick, toValue, &error))
		{
			refuse(error);
			return false;
		}
		if (m_editor->keyPoint() == tick) { m_editor->setKeyPoint(std::max(0, toTick)); }
		return true;
	}

	int step(int column) const override { return column == 0 ? std::max(1, AutomationClip::quantization()) : 1; }
	bool tracksSelection() const override { return true; }

	QList<int> selectedRows() const override
	{
		const int chosen = m_editor != nullptr ? m_editor->keyPoint() : -1;
		for (int row = 0; chosen >= 0 && row < rowCount(); ++row)
		{
			if (tickOf(row) == chosen) { return {row}; }
		}
		return {};
	}

	void select(const QList<int>& rows) override
	{
		if (m_editor == nullptr) { return; }
		m_editor->setKeyPoint(rows.isEmpty() || rows.first() >= rowCount() ? -1 : tickOf(rows.first()));
	}

private:
	const AutomationClip* clip() const { return m_editor != nullptr ? m_editor->currentClip() : nullptr; }
	int origin() const { return clip() != nullptr ? clip()->startPosition().getTicks() : 0; }

	int tickOf(int row) const
	{
		auto it = clip()->getTimeMap().constBegin();
		std::advance(it, row);
		return it.key();
	}

	float storedAt(int row) const
	{
		auto it = clip()->getTimeMap().constBegin();
		std::advance(it, row);
		return it.value().getInValue();
	}

	void watchClip()
	{
		if (m_watched != nullptr) { disconnect(m_watched, nullptr, this, nullptr); }
		m_watched = m_editor != nullptr ? m_editor->editedClip() : nullptr;
		if (m_watched != nullptr) { connect(m_watched, &AutomationClip::dataChanged, this, &ItemListSource::changed); }
	}

	QPointer<AutomationEditor> m_editor;
	QPointer<AutomationClip> m_watched;
};

} // namespace

ItemListSource* automationPoints(AutomationEditor* editor, QObject* parent)
{
	return new AutomationSource(editor, parent);
}

} // namespace lmms::gui::itemlists
