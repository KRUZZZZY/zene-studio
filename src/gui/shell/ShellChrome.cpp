/*
 * ShellChrome.cpp - the 0.5.0 shell's header, footer and flat button
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

#include "ShellChrome.h"

#include <algorithm>

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionToolButton>

#include "Accessibility.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "ShellCommandMenus.h"
#include "ShellIcons.h"
#include "TransportReadouts.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

QFrame* separator(QWidget* parent)
{
	auto* line = new QFrame(parent);
	line->setFixedSize(1, tokens::size::target);
	line->setStyleSheet(QStringLiteral("background-color: %1;").arg(tokens::palette().separator.name()));
	return line;
}

//! A view word as the footer shows it: upper case, no mnemonic marker, no shortcut suffix.
QString wordOf(QString text)
{
	return text.remove(QLatin1Char('&')).section(QLatin1Char('\t'), 0, 0).toUpper();
}

} // namespace

ShellButton::ShellButton(QWidget* parent)
	: QToolButton(parent)
{
	setAttribute(Qt::WA_Hover, true);
	setFocusPolicy(Qt::TabFocus);
	setToolButtonStyle(Qt::ToolButtonIconOnly);
	setIconSize(QSize(tokens::size::icon, tokens::size::icon));
	setCursor(Qt::PointingHandCursor);
}

QSize ShellButton::sizeHint() const
{
	if (toolButtonStyle() == Qt::ToolButtonTextOnly)
	{
		const QFontMetrics fm(tokens::font(tokens::TextRole::Small));
		const int w = fm.horizontalAdvance(wordOf(text())) + 2 * tokens::space::md;
		return {std::max(w, tokens::targetSize()), tokens::targetSize()};
	}
	// 0.5.0 W3 (WF-134 / WF-133): the target grows with the layout density (size::target at standard).
	return {tokens::targetSize() + tokens::space::xs, tokens::targetSize()};
}

QColor ShellButton::fillColour() const
{
	const tokens::Palette& pal = tokens::palette();
	const bool textOnly = toolButtonStyle() == Qt::ToolButtonTextOnly;
	if (isDown()) { return textOnly ? pal.accentHover : pal.selected; }
	if (isChecked()) { return textOnly ? pal.accent : pal.control; }
	if (underMouse() && isEnabled()) { return pal.controlHover; }
	return Qt::transparent;
}

void ShellButton::paintFace(QPainter& p)
{
	const tokens::Palette& pal = tokens::palette();
	if (toolButtonStyle() == Qt::ToolButtonTextOnly)
	{
		p.setFont(tokens::font(tokens::TextRole::Small));
		p.setPen(!isEnabled() ? pal.textDisabled : isChecked() ? pal.textOnAccent : pal.textMuted);
		p.drawText(rect(), Qt::AlignCenter, wordOf(text()));
		return;
	}
	const QSize is = iconSize();
	const QRect target(QPoint((width() - is.width()) / 2, (height() - is.height()) / 2), is);
	icon().paint(&p, target, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled,
		isChecked() ? QIcon::On : QIcon::Off);
}

void ShellButton::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
	p.setPen(Qt::NoPen);
	p.setBrush(fillColour());
	p.drawRoundedRect(r, tokens::radius::md, tokens::radius::md);
	paintFace(p);
	if (!hasFocus()) { return; }
	QPen ring(tokens::palette().focusRing);
	ring.setWidthF(1.5);
	p.setPen(ring);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), tokens::radius::md, tokens::radius::md);
}

ShellHeader::ShellHeader(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("shellHeader"));
	setFixedHeight(tokens::size::headerHeight);
	a11y::announce(this, tr("Header"), tr("Project and transport controls, tempo, time and meter."));

	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(tokens::space::lg, 0, tokens::space::lg, 0);
	row->setSpacing(tokens::space::md);

	auto* mark = new QLabel(QStringLiteral("ZENE"), this);
	QFont markFont = tokens::font(tokens::TextRole::Title);
	markFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
	mark->setFont(markFont);
	QPalette pal = mark->palette();
	pal.setColor(QPalette::WindowText, tokens::palette().accent);
	mark->setPalette(pal);
	row->addWidget(mark);
	row->addWidget(separator(this));

	m_strip = new QWidget(this);
	m_strip->setObjectName(QStringLiteral("mainToolbar"));
	a11y::announce(m_strip, tr("Main toolbar"),
		tr("Project actions, the transport, the metronome, tempo, time and meter."));
	m_stripLayout = new QHBoxLayout(m_strip);
	m_stripLayout->setContentsMargins(0, 0, 0, 0);
	m_stripLayout->setSpacing(tokens::space::xs);
	row->addWidget(m_strip, 1);

	m_play = addCommandButton(QStringLiteral("play"), tr("Play"), QStringLiteral("transport.play"));
	m_play->setCheckable(true);
	addCommandButton(QStringLiteral("stop"), tr("Stop"), QStringLiteral("transport.stop"));
	m_record = addCommandButton(QStringLiteral("record"), tr("Record"), QStringLiteral("transport.record"));
	m_record->setCheckable(true);
	connect(m_record, &QToolButton::clicked, this, &ShellHeader::syncTransport, Qt::QueuedConnection);
	connect(Engine::getSong(), &Song::playbackStateChanged, this, &ShellHeader::syncTransport);
	// A checkable button toggles itself on click; the song's state is the truth, so re-read it.
	connect(m_play, &QToolButton::clicked, this, &ShellHeader::syncTransport, Qt::QueuedConnection);
	m_stripLayout->addWidget(separator(m_strip));
	m_readouts = new TransportReadouts(m_strip);
	m_stripLayout->addWidget(m_readouts);
	// Everything addToStrip() adds (the song editor's tempo, time, meter and master controls)
	// goes before this stretch; addButton() puts the project buttons after it, on the right.
	m_stripLayout->addStretch(1);
	m_stretchIndex = m_stripLayout->count() - 1;
	// The project's own surfaces (revisions, files, plug-ins, groups, mastering, safe start):
	// the first button right of the stretch, before the project buttons MainWindow adds.
	ShellButton* tools = addButton(QStringLiteral("project_tools"), tr("Project tools"));
	tools->setPopupMode(QToolButton::InstantPopup);
	auto* toolsMenu = new QMenu(tools);
	connect(toolsMenu, &QMenu::aboutToShow, toolsMenu, [toolsMenu] {
		toolsMenu->clear();
		ShellCommandMenus::populateProject(toolsMenu);
	});
	tools->setMenu(toolsMenu);
	syncTransport();
}

ShellButton* ShellHeader::addButton(const QString& icon, const QString& text)
{
	auto* button = new ShellButton(m_strip);
	button->setObjectName(QStringLiteral("header_") + icon);
	button->setIcon(shellicons::icon(icon));
	button->setToolTip(text);
	a11y::announce(button, text);
	m_stripLayout->addWidget(button);
	return button;
}

ShellButton* ShellHeader::addCommandButton(const QString& icon, const QString& text, const QString& commandId)
{
	ShellButton* button = addButton(icon, text);
	button->setProperty("controlCommand", commandId);
	connect(button, &QToolButton::clicked, button, [commandId]() { dispatchShellCommand(commandId); });
	return button;
}

void ShellHeader::addToStrip(QWidget* widget)
{
	widget->setParent(m_strip);
	m_stripLayout->insertWidget(m_stretchIndex++, widget, 0, Qt::AlignVCenter);
}

void ShellHeader::addStripSpacing(int pixels) { m_stripLayout->insertSpacing(m_stretchIndex++, pixels); }

int ShellHeader::stripCount() const { return m_stripLayout->count(); }

void ShellHeader::customise(const QJsonObject& doc)
{
	qDeleteAll(m_extra);
	m_extra.clear();
	for (const QJsonValue& value : doc.value(QStringLiteral("extra")).toArray())
	{
		const QJsonObject extra = value.toObject();
		const QString command = extra.value(QStringLiteral("command")).toString();
		const QString label = extra.value(QStringLiteral("label")).toString();
		m_extra.append(addCommandButton(QStringLiteral("plus"), label.isEmpty() ? command : label, command));
		m_extra.last()->setObjectName(QStringLiteral("header_extra_") + command);
	}
	QStringList hidden;
	for (const QJsonValue& name : doc.value(QStringLiteral("hidden")).toArray()) { hidden.append(name.toString()); }
	for (auto* button : m_strip->findChildren<ShellButton*>(QString(), Qt::FindDirectChildrenOnly))
	{
		button->setVisible(!hidden.contains(button->objectName().mid(7)));  // "header_" + name
	}
}

namespace
{

//! The header menu's command-button section: add one (window.header_set's form, the current list
//! filled in) and remove each added one - every change one window.header_set, one undo step.
void addExtrasSection(QMenu& menu)
{
	const QJsonArray extras = ControlRegistry::instance()->invoke(QStringLiteral("window.header_get")).result
		.value(QStringLiteral("extra")).toArray();
	menu.addSeparator();
	addCommandAction(&menu, ShellHeader::tr("Add Command Button..."), QStringLiteral("window.header_set"), [extras]() {
		return CommandCall{QStringLiteral("window.header_set"), {}, {{QStringLiteral("extra"), extras}}};
	});
	if (extras.isEmpty()) { return; }
	QMenu* remove = menu.addMenu(ShellHeader::tr("Remove Command Button"));
	for (int i = 0; i < extras.size(); ++i)
	{
		const QJsonObject extra = extras.at(i).toObject();
		QJsonArray rest = extras;
		rest.removeAt(i);
		const QString label = extra.value(QStringLiteral("label")).toString(extra.value(QStringLiteral("command")).toString());
		addCommandAction(remove, label, QStringLiteral("window.header_set"), [rest]() {
			return CommandCall{QStringLiteral("window.header_set"), {{QStringLiteral("extra"), rest}}, {}, {}, false};
		});
	}
}

} // namespace

void ShellHeader::contextMenuEvent(QContextMenuEvent* event)
{
	// WF-116: right-click the header to choose its buttons (window.header_set, one undo step).
	QMenu menu(this);
	QStringList hidden;
	const QJsonArray all = buttons();
	for (const QJsonValue& value : all)
	{
		if (!value.toObject().value(QStringLiteral("visible")).toBool()) { hidden.append(value.toObject().value(QStringLiteral("name")).toString()); }
	}
	for (const QJsonValue& value : all)
	{
		const QJsonObject button = value.toObject();
		const QString name = button.value(QStringLiteral("name")).toString();
		if (name.startsWith(QStringLiteral("extra_"))) { continue; }
		QAction* action = menu.addAction(button.value(QStringLiteral("text")).toString());
		action->setCheckable(true);
		action->setChecked(!hidden.contains(name));
		action->setProperty("controlCommand", QStringLiteral("window.header_set"));
		connect(action, &QAction::toggled, this, [hidden, name](bool shown) {
			QStringList next = hidden;
			if (shown) { next.removeAll(name); } else { next.append(name); }
			ControlRegistry::instance()->invoke(QStringLiteral("window.header_set"),
				{{QStringLiteral("hidden"), QJsonArray::fromStringList(next)}});
		});
	}
	addExtrasSection(menu);
	menu.exec(event->globalPos());
}

QJsonArray ShellHeader::buttons() const
{
	QJsonArray out;
	for (const auto* button : m_strip->findChildren<ShellButton*>(QString(), Qt::FindDirectChildrenOnly))
	{
		out.append(QJsonObject{{QStringLiteral("name"), button->objectName().mid(7)},
			{QStringLiteral("text"), button->toolTip()}, {QStringLiteral("command"), button->property("controlCommand").toString()},
			{QStringLiteral("visible"), !button->isHidden()}});
	}
	return out;
}

void ShellHeader::syncTransport()
{
	const Song* song = Engine::getSong();
	m_play->setChecked(song != nullptr && song->isPlaying());
	m_record->setChecked(song != nullptr && song->isPlaying() && song->isRecording());
}

void ShellHeader::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), tokens::palette().raised);
	p.fillRect(QRect(0, height() - 1, width(), 1), tokens::palette().base);
}

ShellFooter::ShellFooter(const QList<QAction*>& viewActions, const QList<QAction*>& panelActions, QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("shellFooter"));
	// The footer holds a row of buttons, so it grows with them at a lower density (WF-134).
	setFixedHeight(std::max(tokens::size::footerHeight, tokens::targetSize() + tokens::space::sm));
	a11y::announce(this, tr("Footer"), tr("Panel switches, grouped by where each panel opens, and the views."));

	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(tokens::space::sm, 0, tokens::space::sm, 0);
	row->setSpacing(tokens::space::xxs);

	// Grouped by region, in screen order: the Inspector's cluster on the left, the bottom
	// region's in the middle-left, the launcher beside them, the Browser's on the far right.
	auto byRegion = [&panelActions](const QString& region) {
		QList<QAction*> out;
		for (QAction* a : panelActions)
		{
			if (a->property("shellRegion").toString() == region) { out << a; }
		}
		return out;
	};
	for (QAction* a : byRegion(QStringLiteral("left"))) { addPanelButton(row, a); }
	row->addSpacing(tokens::space::lg);
	for (QAction* a : byRegion(QStringLiteral("bottom"))) { addPanelButton(row, a); }
	row->addSpacing(tokens::space::lg);
	for (QAction* a : byRegion(QStringLiteral("center"))) { addPanelButton(row, a); }
	row->addStretch(1);
	for (QAction* a : viewActions)
	{
		auto* word = new ShellButton(this);
		word->setToolButtonStyle(Qt::ToolButtonTextOnly);
		word->setDefaultAction(a);
		a11y::announce(word, a->text().remove(QLatin1Char('&')), a->toolTip());
		row->addWidget(word);
	}
	row->addSpacing(tokens::space::lg);
	for (QAction* a : byRegion(QStringLiteral("right"))) { addPanelButton(row, a); }
}

void ShellFooter::addPanelButton(QHBoxLayout* row, QAction* action)
{
	auto* button = new ShellButton(this);
	button->setDefaultAction(action);
	a11y::announce(button, action->text(), tr("Show or hide this panel. %1").arg(action->toolTip()));
	row->addWidget(button);
}

void ShellFooter::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), tokens::palette().raised);
	p.fillRect(QRect(0, 0, width(), 1), tokens::palette().base);
}

} // namespace lmms::gui
