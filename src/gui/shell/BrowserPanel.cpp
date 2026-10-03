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
#include <QListWidget>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "BrowserCatalog.h"
#include "BrowserSearchView.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "embed.h"
#include "FileBrowser.h"
#include "PluginBrowser.h"
#include "Shell.h"
#include "ShellLayout.h"
#include "WaveformPreview.h"

namespace lmms::gui
{

namespace
{

//! @a path when it names an audio file, else nothing: what the waveform shows for a search result or tile.
QString audioOrNone(const QString& path)
{
	return browserKindForPath(path) == BrowserKind::Audio ? path : QString();
}

//! The sample a list row names: a file list's sample, or a search result's audio file (its path is the row's data).
QString samplePathOf(const QTreeWidgetItem* item)
{
	if (item == nullptr) { return QString(); }
	if (const auto* file = dynamic_cast<const FileItem*>(item))
	{
		return file->type() == FileItem::FileType::Sample ? file->fullName() : QString();
	}
	return audioOrNone(item->data(0, Qt::UserRole).toString());
}

} // namespace

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
	m_wave = new WaveformPreview(this);
	box->addWidget(m_wave);
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), m_stack, &QStackedWidget::setCurrentIndex);
	// WF-010: the waveform belongs to the source it was picked in; another source starts without one.
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), m_wave, [this] { m_wave->setPath(QString()); });
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
		if (m_restoring || m_context.isEmpty()) { return; }
		ControlRegistry::instance()->invoke(QStringLiteral("browser.remember_source"),
			{{QStringLiteral("context"), m_context}, {QStringLiteral("source"), currentSource()}});
	});
}

void BrowserPanel::setContext(const QString& context)
{
	if (context == m_context) { return; }
	m_context = context;
	const QString remembered = ControlRegistry::instance()->invoke(QStringLiteral("browser.sources")).result
		.value(QStringLiteral("sources")).toObject().value(context).toString();
	m_restoring = true;
	if (!remembered.isEmpty()) { selectSource(remembered); }
	m_restoring = false;
}

void BrowserPanel::addSource(SideBarWidget* source)
{
	source->setChromeless(true);
	m_stack->addWidget(source);
	// WF-010: a sample selected in any file list - search results and gallery tiles included - shows its
	// waveform (browser.peaks) below.
	for (auto* tree : source->findChildren<QTreeWidget*>())
	{
		connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { m_wave->setPath(samplePathOf(item)); });
	}
	for (auto* tiles : source->findChildren<QListWidget*>())
	{
		connect(tiles, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* tile) {
			m_wave->setPath(tile != nullptr ? audioOrNone(tile->data(Qt::UserRole).toString()) : QString());
		});
	}
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
	panel->addSource(new BrowserSearchView(panel));  // 0.5.0 L11: search, collections, columns, project media
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
	// WF-023: the browser follows the shell's view, showing the source remembered for it.
	if (auto* shell = qobject_cast<Shell*>(parent))
	{
		const auto follow = [panel, shell] { panel->setContext(ShellLayout::viewName(shell->model().view())); };
		QObject::connect(shell, &Shell::layoutChanged, panel, follow);
		follow();
	}
	return panel;
}

} // namespace lmms::gui
