/*
 * ArrangerNavigator.cpp - the whole song in miniature under the arranger
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
#include "ArrangerNavigator.h"

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QWheelEvent>

#include "AutomationClip.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

const char* const NavigatorName = "arrangerNavigator";

SongEditor* arrangerEditor()
{
	GuiApplication* g = getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor : nullptr;
}

//! The ticks the arranger shows across its width.
tick_t visibleTicks(SongEditor* editor)
{
	const int width = editor->timeLine()->width() - editor->timeLine()->markerX(editor->currentPosition());
	return static_cast<tick_t>(std::max(width, 1) * TimePos::ticksPerBar() / std::max(editor->pixelsPerBar(), 1.f));
}

QColor clipColour(const Clip* clip, const tokens::Palette& pal)
{
	if (clip->color().has_value()) { return *clip->color(); }
	if (dynamic_cast<const SampleClip*>(clip) != nullptr) { return pal.clipSample; }
	if (dynamic_cast<const MidiClip*>(clip) != nullptr) { return pal.clipMidi; }
	return dynamic_cast<const AutomationClip*>(clip) != nullptr ? pal.clipAutomation : pal.clipPattern;
}

class Navigator : public QWidget
{
public:
	Navigator(SongEditor* editor, QWidget* parent) :
		QWidget(parent),
		m_editor(editor)
	{
		setMinimumHeight(34);
		setAccessibleName(QObject::tr("Navigator"));
		setToolTip(QObject::tr("The whole song: click or drag to scroll there, turn the wheel to zoom"));
		setProperty("controlCommand", QStringLiteral("arrangement.scroll"));
		connect(editor->horizontalScroll(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
		connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
		auto* timer = new QTimer(this);  // clips come and go without a signal this strip could follow
		connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
		timer->start(500);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.panel);
		if (m_editor.isNull()) { return; }
		const double perTick = width() / static_cast<double>(spanTicks());
		const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
		const int rowHeight = std::clamp((height() - 4) / std::max<int>(1, static_cast<int>(tracks.size())), 1, 6);
		for (int row = 0; row < static_cast<int>(tracks.size()); ++row)
		{
			for (const Clip* clip : tracks[row]->getClips())
			{
				const int x = static_cast<int>(clip->startPosition().getTicks() * perTick);
				const int w = std::max(1, static_cast<int>(clip->length().getTicks() * perTick));
				p.fillRect(QRect(x, 2 + row * rowHeight, w, std::max(1, rowHeight - 1)), clipColour(clip, pal));
			}
		}
		const int left = static_cast<int>(m_editor->currentPosition().getTicks() * perTick);
		const int shown = std::max(4, static_cast<int>(visibleTicks(m_editor) * perTick));
		p.setPen(QPen(pal.accent, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(QRect(left + 1, 1, shown - 2, height() - 2));
	}

	void mousePressEvent(QMouseEvent* event) override { scrollTo(event->position().x()); }
	void mouseMoveEvent(QMouseEvent* event) override
	{
		if (event->buttons() & Qt::LeftButton) { scrollTo(event->position().x()); }
	}
	void wheelEvent(QWheelEvent* event) override
	{
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), event->angleDelta().y() > 0 ? 1 : -1}});
	}

private:
	//! The song's length with a bar to spare, or the end of what the arranger shows when that is further.
	tick_t spanTicks() const
	{
		const tick_t song = (Engine::getSong()->length() + 1) * TimePos::ticksPerBar();
		return std::max<tick_t>(song, m_editor->currentPosition().getTicks() + visibleTicks(m_editor));
	}

	//! Centre the arranger on the tick under @a x.
	void scrollTo(double x)
	{
		if (m_editor.isNull()) { return; }
		const auto tick = static_cast<tick_t>(x / std::max(1, width()) * spanTicks()) - visibleTicks(m_editor) / 2;
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), std::max<int>(0, tick)}});
	}

	QPointer<SongEditor> m_editor;
};

QToolBar* navigatorBar()
{
	GuiApplication* g = getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->findChild<QToolBar*>(QLatin1String(NavigatorName)) : nullptr;
}

ControlResult scroll(const QJsonObject& args)
{
	SongEditor* editor = arrangerEditor();
	if (editor == nullptr) { return noInterface(QStringLiteral("arrangement.scroll")); }
	editor->horizontalScroll()->setValue(std::max(0, args.value(QStringLiteral("position")).toInt()));
	return ControlResult::success({{QStringLiteral("position"), static_cast<int>(editor->currentPosition().getTicks())},
		{QStringLiteral("visible_ticks"), static_cast<int>(visibleTicks(editor))}});
}

ControlResult navigator(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("visible")))
	{
		const bool visible = args.value(QStringLiteral("visible")).toBool();
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("navigator"), QString::number(visible ? 1 : 0));
		if (QToolBar* bar = navigatorBar()) { bar->setVisible(visible); }
	}
	return ControlResult::success({{QStringLiteral("visible"),
		ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("navigator")).toInt() != 0}});
}

} // namespace

void installArrangerNavigator(SongEditorWindow* window)
{
	if (window == nullptr || window->m_editor == nullptr || navigatorBar() != nullptr) { return; }
	auto* bar = new QToolBar(QObject::tr("Navigator"), window);
	bar->setObjectName(QLatin1String(NavigatorName));
	bar->setMovable(false);
	bar->addWidget(new Navigator(window->m_editor, bar));
	window->addToolBar(Qt::BottomToolBarArea, bar);
	bar->setVisible(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("navigator")).toInt() != 0);
}

} // namespace lmms::gui

namespace lmms
{

void registerArrangerNavigatorCommands(ControlRegistry& registry)
{
	ControlCommand scrollCmd = guiCommand("arrangement", "scroll");
	scrollCmd.description = QStringLiteral("Scroll the arranger so it starts at 'position' (ticks) - the navigator's click. Reports the "
		"position it reached and how many ticks the arranger shows. Interface state; nothing in the project changes.");
	scrollCmd.argsSchema = control::objectSchema({{QStringLiteral("position"), control::integerProperty()}}, {QStringLiteral("position")});
	scrollCmd.resultSchema = control::objectProperty();
	scrollCmd.handler = gui::scroll;
	registry.registerCommand(scrollCmd);
	ControlCommand navigatorCmd = guiCommand("arrangement", "navigator");
	navigatorCmd.description = QStringLiteral("Show or hide the navigator under the arranger ('visible'), the whole song in miniature "
		"with the part on screen outlined. A preference (config ui/navigator); with no arguments it reports it.");
	navigatorCmd.argsSchema = control::objectSchema({{QStringLiteral("visible"), control::booleanProperty()}});
	navigatorCmd.resultSchema = control::objectProperty();
	navigatorCmd.handler = gui::navigator;
	registry.registerCommand(navigatorCmd);
}

} // namespace lmms
