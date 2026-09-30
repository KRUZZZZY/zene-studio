/*
 * StartHub.cpp - M3 item 10: where a session starts, and where to learn the rest
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

#include "StartHub.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "ControlRegistry.h"

namespace lmms::gui
{

QStringList startHubTemplates()
{
	QStringList files;
	for (const QString& dir : {ConfigManager::inst()->userTemplateDir(), ConfigManager::inst()->factoryTemplatesDir()})
	{
		for (const QFileInfo& info : QDir(dir).entryInfoList({QStringLiteral("*.mpt")}, QDir::Files, QDir::Name))
		{
			files << info.absoluteFilePath();
		}
	}
	return files;
}


StartHub::StartHub(const QStringList& recent, const QStringList& templates, QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Start"));
	setObjectName(QStringLiteral("StartHub"));
	auto* grid = new QGridLayout(this);

	auto* startTitle = new QLabel(tr("<b>Start</b>"), this);
	grid->addWidget(startTitle, 0, 0);
	m_new = makeButton(QStringLiteral("project.new"), tr("New empty project"));
	connect(m_new, &QToolButton::clicked, this, [this] { run(QStringLiteral("project.new"), {}); });
	grid->addWidget(m_new, 1, 0);
	m_templates = new QListWidget(this);
	m_templates->setAccessibleName(tr("Templates"));
	m_templates->setProperty("controlCommand", QStringLiteral("project.new"));
	for (const QString& path : templates)
	{
		auto* item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_templates);
		item->setData(Qt::UserRole, path);
	}
	connect(m_templates, &QListWidget::itemActivated, this, [this] { startFromSelectedTemplate(); });
	grid->addWidget(new QLabel(tr("From a template:"), this), 2, 0);
	grid->addWidget(m_templates, 3, 0);

	grid->addWidget(new QLabel(tr("<b>Recent</b>"), this), 0, 1);
	m_recent = new QListWidget(this);
	m_recent->setAccessibleName(tr("Recent projects"));
	m_recent->setProperty("controlCommand", QStringLiteral("project.open"));
	for (const QString& path : recent)
	{
		auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_recent);
		item->setData(Qt::UserRole, path);
		item->setToolTip(path);
	}
	connect(m_recent, &QListWidget::itemActivated, this, [this] { openSelectedRecent(); });
	grid->addWidget(m_recent, 1, 1, 3, 1);

	grid->addWidget(new QLabel(tr("<b>Learn</b>"), this), 0, 2);
	const struct { const char* command; const char* text; } learn[] = {
		{"window.command_palette", QT_TR_NOOP("Find any action: the command palette (Ctrl+Shift+P)")},
		{"window.shortcuts", QT_TR_NOOP("Every keyboard shortcut")},
		{"app.online_help", QT_TR_NOOP("The online manual")},
	};
	int row = 1;
	for (const auto& entry : learn)
	{
		const QString command = QLatin1String(entry.command);
		QToolButton* button = makeButton(command, tr(entry.text));
		connect(button, &QToolButton::clicked, this, [this, command] { run(command, {}); });
		grid->addWidget(button, row++, 2);
	}
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Start hub status"));
	grid->addWidget(m_status, 4, 0, 1, 3);
}


QToolButton* StartHub::makeButton(const QString& command, const QString& text)
{
	auto* button = new QToolButton(this);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	return button;
}


void StartHub::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	m_status->setText(result.ok ? QString() : result.errorMessage);
	if (result.ok && command.startsWith(QLatin1String("project."))) { accept(); }
}


void StartHub::openSelectedRecent()
{
	if (QListWidgetItem* item = m_recent->currentItem())
	{
		run(QStringLiteral("project.open"), {{QStringLiteral("path"), item->data(Qt::UserRole).toString()}});
	}
}


void StartHub::startFromSelectedTemplate()
{
	if (QListWidgetItem* item = m_templates->currentItem())
	{
		run(QStringLiteral("project.new"), {{QStringLiteral("template"), item->data(Qt::UserRole).toString()}});
	}
}


QString StartHub::statusText() const
{
	return m_status->text();
}

} // namespace lmms::gui
