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
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QWhatsThis>

#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

QString dataPath(const QString& sub) { return QDir(ConfigManager::inst()->dataDir()).filePath(sub); }

//! One command as the reference shows it: id, description, arguments (required ones marked).
QString commandEntry(const ControlCommand& command)
{
	QStringList args;
	const QJsonArray required = command.argsSchema.value(QStringLiteral("required")).toArray();
	const QJsonObject properties = command.argsSchema.value(QStringLiteral("properties")).toObject();
	for (auto it = properties.begin(); it != properties.end(); ++it)
	{
		args << (required.contains(it.key()) ? QStringLiteral("<b>%1</b>").arg(it.key()) : it.key());
	}
	return QStringLiteral("<h3>%1</h3><p>%2</p><p><i>%3</i> %4</p>").arg(command.id, command.description.toHtmlEscaped(),
		HelpCenter::tr("Arguments:"), args.isEmpty() ? HelpCenter::tr("none") : args.join(QStringLiteral(", ")));
}

QPointer<HelpCenter> s_center;

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
	layout->addWidget(m_tabs);
	m_manual->setOpenExternalLinks(true);
	m_manual->setSource(QUrl::fromLocalFile(dataPath(QStringLiteral("manual/index.html"))));
	m_manual->setAccessibleName(tr("Manual"));
	m_tabs->addTab(m_manual, tr("Manual"));
	auto* reference = new QWidget(this);
	auto* column = new QVBoxLayout(reference);
	m_filter->setPlaceholderText(tr("Find a command: name or words of its description"));
	m_filter->setAccessibleName(tr("Find a command"));
	m_reference->setAccessibleName(tr("Command reference"));
	column->addWidget(m_filter);
	column->addWidget(m_reference);
	m_tabs->addTab(reference, tr("Command Reference"));
	connect(m_filter, &QLineEdit::textChanged, this, &HelpCenter::filterReference);
	filterReference(QString());
}

HelpCenter* HelpCenter::showHelp(const QString& topic, bool reference)
{
	if (s_center.isNull())
	{
		QWidget* parent = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
		s_center = new HelpCenter(parent);
	}
	if (!topic.isEmpty()) { s_center->m_manual->scrollToAnchor(topic); }
	s_center->m_tabs->setCurrentIndex(reference ? 1 : 0);
	s_center->QDialog::show();
	s_center->raise();
	return s_center;
}

QString HelpCenter::referenceText() const { return m_reference->toPlainText(); }

void HelpCenter::filterReference(const QString& text)
{
	QString html;
	const QString needle = text.trimmed();
	for (const QString& id : ControlRegistry::instance()->commandIds())
	{
		const ControlCommand* command = ControlRegistry::instance()->command(id);
		if (command == nullptr) { continue; }
		if (!needle.isEmpty() && !id.contains(needle, Qt::CaseInsensitive) && !command->description.contains(needle, Qt::CaseInsensitive)) { continue; }
		html += commandEntry(*command);
	}
	m_reference->setHtml(html.isEmpty() ? tr("<p>No command matches.</p>") : html);
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

GuidedTour* GuidedTour::start(const QString& name, QWidget* window, QString* error)
{
	QFile in(QDir(dataPath(QStringLiteral("tours"))).filePath(name + QStringLiteral(".json")));
	const QJsonArray steps = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("steps")).toArray() : QJsonArray();
	if (steps.isEmpty() || window == nullptr)
	{
		*error = window == nullptr ? tr("there is no window to show a tour in") : tr("there is no tour '%1'").arg(name);
		return nullptr;
	}
	for (auto* old : window->findChildren<GuidedTour*>()) { old->deleteLater(); }
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
	auto* back = new QPushButton(tr("Back"), m_bubble);
	auto* next = new QPushButton(tr("Next"), m_bubble);
	auto* close = new QPushButton(tr("Close"), m_bubble);
	buttons->addWidget(back);
	buttons->addStretch(1);
	buttons->addWidget(close);
	buttons->addWidget(next);
	column->addLayout(buttons);
	connect(back, &QPushButton::clicked, this, [this] { showStep(std::max(0, m_step - 1)); });
	connect(next, &QPushButton::clicked, this, [this] {
		if (m_step + 1 >= stepCount()) { deleteLater(); return; }
		showStep(m_step + 1);
	});
	connect(close, &QPushButton::clicked, this, &QObject::deleteLater);
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
	QWidget* target = targetOf(m_step);
	const QString panel = step.value(QStringLiteral("panel")).toString();
	if ((target == nullptr || !target->isVisible()) && !panel.isEmpty())
	{
		ControlRegistry::instance()->invoke(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), panel}});
		target = targetOf(m_step);
	}
	setGeometry(parentWidget()->rect());
	m_hole = target != nullptr && target->isVisible() ? QRect(target->mapTo(parentWidget(), QPoint(0, 0)), target->size()) : QRect();
	m_title->setText(QStringLiteral("<b>%1</b>  <small>(%2/%3)</small>").arg(step.value(QStringLiteral("title")).toString().toHtmlEscaped())
		.arg(m_step + 1).arg(stepCount()));
	m_text->setText(step.value(QStringLiteral("text")).toString());
	m_bubble->adjustSize();
	const QPoint beside = m_hole.isNull() ? rect().center() - QPoint(160, 60) : QPoint(m_hole.right() + 12, m_hole.top());
	m_bubble->move(std::clamp(beside.x(), 8, std::max(8, width() - m_bubble->width() - 8)),
		std::clamp(beside.y(), 8, std::max(8, height() - m_bubble->height() - 8)));
	show();
	raise();
	update();
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
	QAction* whatsThis = help->addAction(HelpCenter::tr("What's This?"), [] { QWhatsThis::enterWhatsThisMode(); });
	whatsThis->setObjectName(QStringLiteral("helpWhatsThis"));
	whatsThis->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
	addCommandAction(help, HelpCenter::tr("Manual"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {}, {}, QString(), false}; });
	addCommandAction(help, HelpCenter::tr("Command Reference"), QStringLiteral("app.help"),
		[] { return CommandCall{QStringLiteral("app.help"), {{QStringLiteral("reference"), true}}, {}, QString(), false}; });
	QMenu* tours = help->addMenu(HelpCenter::tr("Tours"));
	for (const QJsonValue& tour : GuidedTour::tours())
	{
		const QString name = tour.toObject().value(QStringLiteral("name")).toString();
		addCommandAction(tours, tour.toObject().value(QStringLiteral("title")).toString(name), QStringLiteral("app.tour"),
			[name] { return CommandCall{QStringLiteral("app.tour"), {{QStringLiteral("name"), name}}, {}, QString(), false}; });
	}
	addCommandAction(help, HelpCenter::tr("Appearance..."), QStringLiteral("window.appearance"),
		[] { return CommandCall{QStringLiteral("window.appearance"), {}, {}, QString(), true}; });
}

} // namespace lmms::gui
