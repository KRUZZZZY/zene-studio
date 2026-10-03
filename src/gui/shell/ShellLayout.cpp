/*
 * ShellLayout.cpp - the 0.5.0 shell's panels, regions and views, as data
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

#include "ShellLayout.h"

#include <QCoreApplication>

namespace lmms::gui
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("ShellLayout", text); }

const QString kLauncher = QStringLiteral("launcher");

constexpr int kViews = 3;

int regionKey(ShellRegion region) { return static_cast<int>(region); }

QString regionName(ShellRegion region)
{
	switch (region)
	{
	case ShellRegion::Left: return QStringLiteral("left");
	case ShellRegion::Center: return QStringLiteral("center");
	case ShellRegion::Bottom: return QStringLiteral("bottom");
	case ShellRegion::Right: return QStringLiteral("right");
	}
	return {};
}

const ShellRegion kSideRegions[] = {ShellRegion::Left, ShellRegion::Bottom, ShellRegion::Right};

} // namespace

ShellLayout::ShellLayout()
{
	// The first-run arrangement: Inspector and Browser open in every view; Arrange shows the
	// note editor below the timeline, Mix and Edit show the device chain below their centre.
	for (int v = 0; v < kViews; ++v)
	{
		m_states[v].openPanel.insert(regionKey(ShellRegion::Left), QStringLiteral("inspector"));
		m_states[v].openPanel.insert(regionKey(ShellRegion::Right), QStringLiteral("browser"));
		m_states[v].openPanel.insert(regionKey(ShellRegion::Bottom), QStringLiteral("devices"));
	}
	m_states[static_cast<int>(ShellView::Arrange)].openPanel.insert(
		regionKey(ShellRegion::Bottom), QStringLiteral("detail"));
}

QList<ShellPanelSpec> ShellLayout::panels()
{
	static const QList<ShellPanelSpec> list{
		{QStringLiteral("arranger"), tr("Arranger"), ShellRegion::Center, QString()},
		{kLauncher, tr("Clip Launcher"), ShellRegion::Center, QStringLiteral("Alt+L")},
		{QStringLiteral("mixer"), tr("Mixer"), ShellRegion::Bottom, QStringLiteral("Alt+M")},
		{QStringLiteral("detail"), tr("Note Editor"), ShellRegion::Bottom, QStringLiteral("Alt+E")},
		{QStringLiteral("devices"), tr("Devices"), ShellRegion::Bottom, QStringLiteral("Alt+D")},
		{QStringLiteral("automation"), tr("Automation"), ShellRegion::Bottom, QStringLiteral("Alt+A")},
		{QStringLiteral("pattern"), tr("Pattern Editor"), ShellRegion::Bottom, QStringLiteral("Alt+P")},
		{QStringLiteral("inspector"), tr("Inspector"), ShellRegion::Left, QStringLiteral("Alt+I")},
		{QStringLiteral("browser"), tr("Browser"), ShellRegion::Right, QStringLiteral("Alt+B")},
	};
	return list;
}

bool ShellLayout::isPanel(const QString& id)
{
	for (const ShellPanelSpec& s : panels())
	{
		if (s.id == id) { return true; }
	}
	return false;
}

ShellPanelSpec ShellLayout::spec(const QString& id)
{
	for (const ShellPanelSpec& s : panels())
	{
		if (s.id == id) { return s; }
	}
	return {};
}

QString ShellLayout::viewName(ShellView view)
{
	switch (view)
	{
	case ShellView::Arrange: return QStringLiteral("arrange");
	case ShellView::Mix: return QStringLiteral("mix");
	case ShellView::Edit: return QStringLiteral("edit");
	}
	return {};
}

bool ShellLayout::viewFromName(const QString& name, ShellView& out)
{
	for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
	{
		if (viewName(v) == name)
		{
			out = v;
			return true;
		}
	}
	return false;
}

QStringList ShellLayout::viewNames()
{
	return {viewName(ShellView::Arrange), viewName(ShellView::Mix), viewName(ShellView::Edit)};
}

QString ShellLayout::centerOf(ShellView view)
{
	switch (view)
	{
	case ShellView::Arrange: return QStringLiteral("arranger");
	case ShellView::Mix: return QStringLiteral("mixer");
	case ShellView::Edit: return QStringLiteral("detail");
	}
	return {};
}

bool ShellLayout::offers(ShellView view, const QString& id)
{
	if (!isPanel(id)) { return false; }
	if (id == QStringLiteral("arranger")) { return view == ShellView::Arrange; }
	if (id == kLauncher) { return view != ShellView::Edit; }
	return true;
}

void ShellLayout::setView(ShellView view)
{
	m_view = view;
	// A side region never shows the panel that is now the centre (the note editor cannot be
	// both the Edit view's centre and the panel below it).
	const QString centre = centerOf(view);
	for (ShellRegion r : kSideRegions)
	{
		if (current().openPanel.value(regionKey(r)) == centre) { current().openPanel.remove(regionKey(r)); }
	}
	reopenPinned();
}

void ShellLayout::reopenPinned()
{
	for (const QString& id : m_pinned)
	{
		if (offers(m_view, id) && id != centerOf(m_view)) { current().openPanel.insert(regionKey(spec(id).home), id); }
	}
}

bool ShellLayout::setPinnedQuietly(const QString& id)
{
	if (!isPanel(id) || id == kLauncher || spec(id).home == ShellRegion::Center) { return false; }
	if (!m_pinned.contains(id)) { m_pinned.append(id); }
	return true;
}

bool ShellLayout::setPinned(const QString& id, bool pinned)
{
	if (!pinned) { return isPanel(id) && m_pinned.removeAll(id) >= 0; }
	if (!setPinnedQuietly(id)) { return false; }
	open(id, nullptr);
	return true;
}

bool ShellLayout::isVisible(const QString& id) const
{
	if (!offers(m_view, id)) { return false; }
	if (id == centerOf(m_view)) { return true; }
	if (id == kLauncher) { return current().launcher; }
	return current().openPanel.value(regionKey(spec(id).home)) == id;
}

ShellRegion ShellLayout::regionOf(const QString& id) const
{
	if (id == centerOf(m_view) || id == kLauncher) { return ShellRegion::Center; }
	return spec(id).home;
}

QString ShellLayout::openIn(ShellRegion region) const
{
	return current().openPanel.value(regionKey(region));
}

bool ShellLayout::launcherBesideCenter() const
{
	return offers(m_view, kLauncher) && current().launcher;
}

QString ShellLayout::refusal(const QString& id) const
{
	if (!isPanel(id)) { return tr("there is no panel called \"%1\"").arg(id); }
	return tr("the %1 view has no %2 panel").arg(viewName(m_view), spec(id).title);
}

bool ShellLayout::open(const QString& id, QString* reason)
{
	if (!offers(m_view, id))
	{
		if (reason != nullptr) { *reason = refusal(id); }
		return false;
	}
	if (id == centerOf(m_view)) { return true; }
	if (id == kLauncher)
	{
		current().launcher = true;
		return true;
	}
	current().openPanel.insert(regionKey(spec(id).home), id);
	return true;
}

bool ShellLayout::close(const QString& id, QString* reason)
{
	if (id == centerOf(m_view))
	{
		if (reason != nullptr) { *reason = tr("the %1 view's main panel cannot be hidden").arg(viewName(m_view)); }
		return false;
	}
	if (!offers(m_view, id))
	{
		if (reason != nullptr) { *reason = refusal(id); }
		return false;
	}
	if (id == kLauncher)
	{
		current().launcher = false;
		return true;
	}
	const int key = regionKey(spec(id).home);
	if (current().openPanel.value(key) == id) { current().openPanel.remove(key); }
	return true;
}

bool ShellLayout::toggle(const QString& id, QString* reason)
{
	if (isVisible(id))
	{
		close(id, reason);
	}
	else
	{
		open(id, reason);
	}
	return isVisible(id);
}

void ShellLayout::setFocused(const QString& id)
{
	if (isPanel(id)) { m_focused = id; }
}

QVariantMap ShellLayout::save() const
{
	QVariantMap state;
	state.insert(QStringLiteral("view"), viewName(m_view));
	for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
	{
		const ViewState& s = m_states[static_cast<int>(v)];
		QVariantMap entry;
		for (ShellRegion r : kSideRegions)
		{
			entry.insert(regionName(r), s.openPanel.value(regionKey(r)));
		}
		entry.insert(kLauncher, s.launcher);
		state.insert(viewName(v), entry);
	}
	if (!m_pinned.isEmpty()) { state.insert(QStringLiteral("pinned"), m_pinned); }
	return state;
}

void ShellLayout::restore(const QVariantMap& state)
{
	const ShellView keep = m_view;
	for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
	{
		const QVariantMap entry = state.value(viewName(v)).toMap();
		m_view = v;
		for (ShellRegion r : kSideRegions)
		{
			const QString id = entry.value(regionName(r)).toString();
			const bool fits = offers(v, id) && spec(id).home == r && id != centerOf(v);
			if (fits) { current().openPanel.insert(regionKey(r), id); }
			if (entry.contains(regionName(r)) && id.isEmpty()) { current().openPanel.remove(regionKey(r)); }
		}
		current().launcher = entry.value(kLauncher, current().launcher).toBool();
	}
	m_view = keep;
	m_pinned.clear();
	for (const QString& id : state.value(QStringLiteral("pinned")).toStringList()) { setPinnedQuietly(id); }
	ShellView named = keep;
	if (viewFromName(state.value(QStringLiteral("view")).toString(), named)) { setView(named); }
}

} // namespace lmms::gui
