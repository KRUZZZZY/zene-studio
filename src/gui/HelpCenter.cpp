/*
 * HelpCenter.cpp - the manual, the command reference and the guided tours inside the application
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
#include "HelpCenter.h"

#include <algorithm>

#include <QDir>
#include <QAbstractScrollArea>
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "HelpTasks.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

QString dataPath(const QString& sub) { return QDir(ConfigManager::inst()->dataDir()).filePath(sub); }

QPointer<HelpCenter> s_center;

//! Where a tour step's frame (@a bubble) goes in an @a area: beside its target (@a hole) on the right,
//! on the left when the right has no room, below when neither side has, so it does not cover what it
//! points at; centred when the step has no target. The caller keeps it inside the area.
QPoint bubbleAt(const QRect& hole, const QSize& bubble, const QSize& area)
{
	constexpr int Gap = 12;
	constexpr int Margin = 8;
	if (hole.isNull()) { return QRect(QPoint(), area).center() - QPoint(160, 60); }
	if (hole.right() + Gap + bubble.width() <= area.width() - Margin) { return {hole.right() + Gap, hole.top()}; }
	if (hole.left() - Gap - bubble.width() >= Margin) { return {hole.left() - Gap - bubble.width(), hole.top()}; }
	return {hole.left(), hole.bottom() + Gap};
}

bool shouldRepositionTour(QEvent::Type type)
{
	switch (type)
	{
	case QEvent::Move:
	case QEvent::Resize:
	case QEvent::Show:
	case QEvent::Hide:
	case QEvent::LayoutRequest:
	case QEvent::Scroll:
		return true;
	default:
		return false;
	}
}

} // namespace

HelpCenter::HelpCenter(QWidget* parent) :
	QDialog(parent),
	m_tabs(new QTabWidget(this)),
	m_manual(new QTextBrowser(this)),
	m_reference(new QTextBrowser(this)),
	m_filter(new QLineEdit(this))
{
	setWindowTitle(tr("Zene Studio Help"));
	resize(760, 620);
	auto* layout = new QVBoxLayout(this);
	m_context = new QLabel(this);
	m_context->setObjectName(QStringLiteral("helpContext"));
	m_context->setWordWrap(true);
	m_context->setTextFormat(Qt::RichText);
	m_context->setTextInteractionFlags(Qt::TextBrowserInteraction);  // its manual link is reached with Tab
	m_context->setFrameShape(QFrame::StyledPanel);
	m_context->setAccessibleName(tr("Help for the focused control"));
	m_context->hide();
	connect(m_context, &QLabel::linkActivated, this, [this](const QString& link) { follow(QUrl(link)); });
	layout->addWidget(m_context);
	layout->addWidget(m_tabs);
	m_manual->setOpenExternalLinks(true);
	const QString manual = dataPath(QStringLiteral("manual/index.html"));
	if (QFileInfo::exists(manual)) { m_manual->setSource(QUrl::fromLocalFile(manual)); }
	else
	{
		// A broken install says so instead of showing an empty page.
		m_manual->setHtml(tr("<p>The manual was not found (%1). Reinstalling Zene Studio puts it back; the Command "
			"Reference tab still lists every command.</p>").arg(manual.toHtmlEscaped()));
	}
	m_manual->setAccessibleName(tr("Manual"));
	m_tabs->addTab(makeManualTab(), tr("Manual"));
	auto* reference = new QWidget(this);
	auto* column = new QVBoxLayout(reference);
	m_filter->setPlaceholderText(tr("Find a command: its name, a task (mixing, recording...), a shortcut or a word of its description"));
	m_filter->setAccessibleName(tr("Find a command"));
	m_reference->setAccessibleName(tr("Command reference, by task"));
	m_reference->setOpenLinks(false);  // a task's "read the manual" opens the Manual tab there (follow)
	connect(m_reference, &QTextBrowser::anchorClicked, this, &HelpCenter::follow);
	column->addWidget(m_filter);
	column->addWidget(m_reference);
	m_tabs->addTab(reference, tr("Command Reference"));
	connect(m_filter, &QLineEdit::textChanged, this, &HelpCenter::filterReference);
}

QWidget* HelpCenter::makeManualTab()
{
	auto* tab = new QWidget(this);
	auto* column = new QVBoxLayout(tab);
	auto* row = new QHBoxLayout;
	m_manualFind = new QLineEdit(tab);
	m_manualFind->setObjectName(QStringLiteral("helpManualFind"));
	m_manualFind->setPlaceholderText(tr("Find in the manual (Enter: next, Shift+Enter: previous)"));
	m_manualFind->setAccessibleName(tr("Find in the manual"));
	m_manualFind->setClearButtonEnabled(true);
	m_manualFind->installEventFilter(this);
	m_manualFound = new QLabel(tab);
	m_manualFound->setAccessibleName(tr("Find result"));
	row->addWidget(m_manualFind, 1);
	row->addWidget(m_manualFound);
	column->addLayout(row);
	column->addWidget(m_manual, 1);
	connect(m_manualFind, &QLineEdit::returnPressed, this, [this] { findInManual(m_manualFind->text()); });
	connect(m_manualFind, &QLineEdit::textEdited, this, [this](const QString& text) {
		m_manual->moveCursor(QTextCursor::Start);  // as typed: the first match from the top
		findInManual(text);
	});
	return tab;
}

bool HelpCenter::findInManual(const QString& text, bool backward)
{
	const QString needle = text.trimmed();
	m_manualFound->clear();
	if (needle.isEmpty()) { return false; }
	const QTextDocument::FindFlags flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
	bool found = m_manual->find(needle, flags);
	if (!found)
	{
		m_manual->moveCursor(backward ? QTextCursor::End : QTextCursor::Start);  // round again from the other end
		found = m_manual->find(needle, flags);
	}
	if (!found)
	{
		m_manualFound->setText(tr("Not in the manual - try other words, the Command Reference or Help > Ask"));
		a11y::announceStatus(m_manualFound, m_manualFound->text());
	}
	return found;
}

bool HelpCenter::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_manualFind && event->type() == QEvent::KeyPress)
	{
		const auto* key = static_cast<QKeyEvent*>(event);
		const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
		if (enter && (key->modifiers() & Qt::ShiftModifier))
		{
			findInManual(m_manualFind->text(), true);
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void HelpCenter::follow(const QUrl& url)
{
	if (url.scheme() != QLatin1String("topic")) { return; }
	m_manual->scrollToAnchor(url.path());
	m_tabs->setCurrentIndex(0);
}

void HelpCenter::showContext(const QJsonObject& context)
{
	m_context->setText(context.isEmpty() ? QString() : help::contextHtml(context));
	m_context->setVisible(!context.isEmpty());
	if (!context.isEmpty()) { a11y::announceStatus(m_context, m_context->text()); }
}

QString HelpCenter::contextText() const
{
	return m_context->isHidden() ? QString() : QTextDocumentFragment::fromHtml(m_context->text()).toPlainText();
}

HelpCenter* HelpCenter::showHelp(const QString& topic, bool reference)
{
	if (s_center.isNull())
	{
		QWidget* parent = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
		s_center = new HelpCenter(parent);
	}
	// The shortcuts as the menus have them now (the user may have rebound one), in the reference's entries.
	QMenuBar* bar = getGUI() != nullptr && getGUI()->mainWindow() != nullptr ? getGUI()->mainWindow()->menuBar() : nullptr;
	s_center->m_shortcuts = help::commandShortcuts(bar);
	s_center->filterReference(s_center->m_filter->text());
	s_center->showContext({});
	if (!topic.isEmpty()) { s_center->m_manual->scrollToAnchor(topic); }
	s_center->m_tabs->setCurrentIndex(reference ? 1 : 0);
	s_center->QDialog::show();
	s_center->raise();
	return s_center;
}

QString HelpCenter::referenceText() const { return m_reference->toPlainText(); }

void HelpCenter::findCommand(const QString& text)
{
	m_tabs->setCurrentIndex(1);
	m_filter->setText(text);  // filters as typing does
	filterReference(text);    // and when the text was already there
}

void HelpCenter::filterReference(const QString& text)
{
	const QString html = help::referenceHtml(text, m_shortcuts);
	m_reference->setHtml(html.isEmpty() ? tr("<p>No command matches. Try a task (mixing, recording), other words, "
		"or Help &gt; Ask.</p>") : html);
}

// ---------------------------------------------------------------------------

QJsonArray GuidedTour::tours()
{
	QJsonArray out;
	const QDir dir(dataPath(QStringLiteral("tours")));
	for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
	{
		QFile in(dir.filePath(file));
		if (!in.open(QIODevice::ReadOnly)) { continue; }
		const QJsonObject tour = QJsonDocument::fromJson(in.readAll()).object();
		out.append(QJsonObject{{QStringLiteral("name"), QFileInfo(file).completeBaseName()}, {QStringLiteral("title"), tour.value(QStringLiteral("title"))},
			{QStringLiteral("steps"), tour.value(QStringLiteral("steps")).toArray().size()}});
	}
	return out;
}

namespace
{

//! Whether @a name is a plain tour file name - no directory part, so it names a file in the tours folder only.
bool isTourName(const QString& name)
{
	return !name.isEmpty() && QFileInfo(name).fileName() == name && !name.contains(QLatin1Char('\\'));
}

} // namespace

GuidedTour* GuidedTour::start(const QString& name, QWidget* window, QString* error)
{
	QFile in(QDir(dataPath(QStringLiteral("tours"))).filePath(name + QStringLiteral(".json")));
	const bool found = isTourName(name) && in.open(QIODevice::ReadOnly);
	const QJsonArray steps = found ? QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("steps")).toArray() : QJsonArray();
	if (steps.isEmpty() || window == nullptr)
	{
		*error = window == nullptr ? tr("there is no window to show a tour in")
			: found ? tr("the tour '%1' has no steps: its file is empty or not valid JSON").arg(name)
			: tr("there is no tour '%1'").arg(name);
		return nullptr;
	}
	for (auto* old : window->findChildren<GuidedTour*>())
	{
		old->hide();  // gone from the screen now, deleted once control returns to the event loop
		old->deleteLater();
	}
	auto* tour = new GuidedTour(steps, window);
	tour->showStep(0);
	return tour;
}

GuidedTour::GuidedTour(const QJsonArray& steps, QWidget* window) :
	QWidget(window),
	m_steps(steps),
	m_bubble(new QFrame(this)),
	m_title(new QLabel(m_bubble)),
	m_text(new QLabel(m_bubble))
{
	setObjectName(QStringLiteral("guidedTour"));
	m_bubble->setObjectName(QStringLiteral("guidedTourStep"));
	m_bubble->setAutoFillBackground(true);
	m_bubble->setFrameShape(QFrame::StyledPanel);
	m_bubble->setFixedWidth(320);
	m_text->setWordWrap(true);
	auto* column = new QVBoxLayout(m_bubble);
	column->addWidget(m_title);
	column->addWidget(m_text);
	auto* buttons = new QHBoxLayout;
	m_back = new QPushButton(tr("Back"), m_bubble);
	m_next = new QPushButton(tr("Next"), m_bubble);
	auto* close = new QPushButton(tr("Close"), m_bubble);
	close->setToolTip(tr("End the tour (Escape)"));
	buttons->addWidget(m_back);
	buttons->addStretch(1);
	buttons->addWidget(close);
	buttons->addWidget(m_next);
	column->addLayout(buttons);
	connect(m_back, &QPushButton::clicked, this, [this] { showStep(std::max(0, m_step - 1)); });
	connect(m_next, &QPushButton::clicked, this, [this] {
		if (m_step + 1 >= stepCount()) { deleteLater(); return; }
		showStep(m_step + 1);
	});
	connect(close, &QPushButton::clicked, this, &QObject::deleteLater);
	window->installEventFilter(this);  // the overlay follows the window's size
}

bool GuidedTour::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parentWidget() && event->type() == QEvent::Resize) { place(); }
	else if (shouldRepositionTour(event->type()))
	{
		for (QWidget* ancestor = targetOf(m_step); ancestor != nullptr; ancestor = ancestor->parentWidget())
		{
			if (ancestor == watched) { place(); break; }
			if (ancestor == parentWidget()) { break; }
		}
	}
	return QWidget::eventFilter(watched, event);
}

QWidget* GuidedTour::targetOf(int index) const
{
	const QString name = m_steps.at(index).toObject().value(QStringLiteral("target")).toString();
	return name.isEmpty() ? nullptr : parentWidget()->findChild<QWidget*>(name);
}

void GuidedTour::showStep(int index)
{
	m_step = std::clamp(index, 0, stepCount() - 1);
	const QJsonObject step = m_steps.at(m_step).toObject();
	const QWidget* target = targetOf(m_step);  // place() finds it again once its panel is shown
	const QString panel = step.value(QStringLiteral("panel")).toString();
	if ((target == nullptr || !target->isVisible()) && !panel.isEmpty())
	{
		ControlRegistry::instance()->invoke(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), panel}});
	}
	m_title->setText(QStringLiteral("<b>%1</b>  <small>(%2/%3)</small>").arg(step.value(QStringLiteral("title")).toString().toHtmlEscaped())
		.arg(m_step + 1).arg(stepCount()));
	m_text->setText(step.value(QStringLiteral("text")).toString());
	// The step for a screen reader, and the keyboard on the tour rather than on the dimmed window.
	m_bubble->setAccessibleName(tr("Tour step %1 of %2: %3").arg(m_step + 1).arg(stepCount()).arg(step.value(QStringLiteral("title")).toString()));
	m_bubble->setAccessibleDescription(m_text->text());
	m_back->setEnabled(m_step > 0);
	place();
	show();
	raise();
	m_next->setFocus();
}

void GuidedTour::place()
{
	setGeometry(parentWidget()->rect());
	QWidget* target = targetOf(m_step);
	for (QWidget* ancestor = target; ancestor != nullptr; ancestor = ancestor->parentWidget())
	{
		ancestor->installEventFilter(this);
		if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(ancestor))
		{
			connect(scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this, &GuidedTour::place, Qt::UniqueConnection);
			connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, &GuidedTour::place, Qt::UniqueConnection);
		}
		if (ancestor == parentWidget()) { break; }
	}
	m_hole = target != nullptr && target->isVisible() ? QRect(target->mapTo(parentWidget(), QPoint(0, 0)), target->size()) : QRect();
	m_bubble->adjustSize();
	const QPoint beside = bubbleAt(m_hole, m_bubble->size(), size());
	m_bubble->move(std::clamp(beside.x(), 8, std::max(8, width() - m_bubble->width() - 8)),
		std::clamp(beside.y(), 8, std::max(8, height() - m_bubble->height() - 8)));
	update();
}

void GuidedTour::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape) { deleteLater(); return; }
	QWidget::keyPressEvent(event);
}

void GuidedTour::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	QPainterPath dim;
	dim.addRect(rect());
	if (!m_hole.isNull())
	{
		QPainterPath hole;
		hole.addRoundedRect(m_hole.adjusted(-4, -4, 4, 4), 6, 6);
		dim = dim.subtracted(hole);
	}
	p.fillPath(dim, QColor(0, 0, 0, 120));
	if (!m_hole.isNull())
	{
		p.setPen(QPen(palette().highlight().color(), 2));
		p.drawRoundedRect(m_hole.adjusted(-4, -4, 4, 4), 6, 6);
	}
}

// ---------------------------------------------------------------------------

void installHelpEntries(QMenuBar* bar)
{
	if (bar->actions().isEmpty()) { return; }
	QMenu* help = bar->actions().last()->menu();
	if (help == nullptr || help->findChild<QAction*>(QStringLiteral("helpWhatsThis")) != nullptr) { return; }
	help->addSeparator();
	QAction* whatsThis = addCommandAction(help, HelpCenter::tr("What's This?"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {{QStringLiteral("whats_this"), true}}, {}, QString(), false}; });
	whatsThis->setObjectName(QStringLiteral("helpWhatsThis"));
	whatsThis->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
	// Design theme 21: help for the focused control from the keyboard - no hovering, no click needed.
	QAction* forThis = addCommandAction(help, HelpCenter::tr("Help for This"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {{QStringLiteral("context"), true}}, {}, QString(), false}; });
	forThis->setObjectName(QStringLiteral("helpForThis"));
	forThis->setShortcut(QKeySequence(Qt::Key_F1));
	addCommandAction(help, HelpCenter::tr("Manual"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {}, {}, QString(), false}; });
	addCommandAction(help, HelpCenter::tr("Command Reference"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {{QStringLiteral("reference"), true}}, {}, QString(), false}; });
	QMenu* tours = help->addMenu(HelpCenter::tr("Tours"));
	const QJsonArray installed = GuidedTour::tours();
	if (installed.isEmpty()) { tours->addAction(HelpCenter::tr("No tours installed"))->setEnabled(false); }
	for (const QJsonValue& tour : installed)
	{
		const QString name = tour.toObject().value(QStringLiteral("name")).toString();
		addCommandAction(tours, tour.toObject().value(QStringLiteral("title")).toString(name), QStringLiteral("app.tour"),
			[name] { return CommandCall{QStringLiteral("app.tour"), {{QStringLiteral("name"), name}}, {}, QString(), false}; });
	}
	addCommandAction(help, HelpCenter::tr("Appearance..."), QStringLiteral("window.appearance"),
		[] { return CommandCall{QStringLiteral("window.appearance"), {}, {}, QString(), true}; });
}

} // namespace lmms::gui
