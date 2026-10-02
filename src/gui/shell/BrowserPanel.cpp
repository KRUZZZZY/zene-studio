/*
 * BrowserPanel.cpp - the shell's Browser: devices, presets, samples and projects in one panel
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

#include "BrowserPanel.h"

#include <QComboBox>
#include <QDir>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "ConfigManager.h"
#include "DesignTokens.h"
#include "embed.h"
#include "FileBrowser.h"
#include "PluginBrowser.h"

namespace lmms::gui
{

BrowserPanel::BrowserPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("browserPanel"));
	auto* box = new QVBoxLayout(this);
	box->setContentsMargins(tokens::space::sm, tokens::space::sm, tokens::space::sm, 0);
	box->setSpacing(tokens::space::xs);

	m_selector = new QComboBox(this);
	m_selector->setMinimumHeight(tokens::size::target);
	a11y::announce(m_selector, tr("Browser source"),
		tr("Which content the browser lists: devices, presets, samples, projects or folders."));
	box->addWidget(m_selector);

	m_stack = new QStackedWidget(this);
	box->addWidget(m_stack, 1);
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), m_stack, &QStackedWidget::setCurrentIndex);
}

void BrowserPanel::addSource(SideBarWidget* source)
{
	source->setChromeless(true);
	m_stack->addWidget(source);
	m_selector->addItem(source->title());
}

int BrowserPanel::sourceCount() const { return m_stack->count(); }

QString BrowserPanel::currentSource() const { return m_selector->currentText(); }

bool BrowserPanel::selectSource(const QString& title)
{
	const int index = m_selector->findText(title);
	if (index < 0) { return false; }
	m_selector->setCurrentIndex(index);
	return true;
}

BrowserPanel* createStandardBrowser(QWidget* parent)
{
	auto* panel = new BrowserPanel(parent);
	ConfigManager* conf = ConfigManager::inst();
	auto icon = [](const char* name) { return embed::getIconPixmap(name); };

	panel->addSource(new PluginBrowser(panel));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userPresetsDir() + "*" + conf->factoryPresetsDir(),
		"*.xpf *.cs.xml *.xiz *.lv2", BrowserPanel::tr("Presets"), icon("preset_file"), panel, false,
		conf->userPresetsDir(), conf->factoryPresetsDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userSamplesDir() + "*" + conf->factorySamplesDir(),
		FileItem::defaultFilters(), BrowserPanel::tr("Samples"), icon("sample_file"), panel, false,
		conf->userSamplesDir(), conf->factorySamplesDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userProjectsDir() + "*" + conf->factoryProjectsDir(),
		"*.mmp *.mmpz *.xml *.mid *.mpt", BrowserPanel::tr("Projects"), icon("project_file"), panel, false,
		conf->userProjectsDir(), conf->factoryProjectsDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Favorites, conf->favoriteItems().join("*"),
		FileItem::defaultFilters(), BrowserPanel::tr("Favorites"), icon("star"), panel, false, "", ""));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, QDir::homePath(), FileItem::defaultFilters(),
		BrowserPanel::tr("Home"), icon("home"), panel, false));

	QStringList roots;
	QString title = BrowserPanel::tr("Root Directory");
	bool dirsAsItems = false;
#ifdef LMMS_BUILD_APPLE
	title = BrowserPanel::tr("Volumes");
	roots += "/Volumes";
#elif defined(LMMS_BUILD_WIN32)
	title = BrowserPanel::tr("My Computer");
	dirsAsItems = true;
#endif
#if !defined(LMMS_BUILD_APPLE)
	for (const QFileInfo& drive : QDir::drives()) { roots += drive.absolutePath(); }
#endif
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, roots.join("*"), FileItem::defaultFilters(), title,
		icon("computer"), panel, dirsAsItems));
	return panel;
}

} // namespace lmms::gui
